#include "toyfs.h"


#define TF_DIRITEM_SIZE           32
#define TF_CLUSTER_ID_VALID(clus) (clus < 0x0FFFFFF8)

#define TF_FILEATTR_READ_ONLY      TF_ATTR_READ_ONLY
#define TF_FILEATTR_HIDDEN         TF_ATTR_HIDDEN
#define TF_FILEATTR_SYSTEM         TF_ATTR_SYSTEM
#define TF_FILEATTR_VOLUME_ID      TF_ATTR_VOLUME_ID
#define TF_FILEATTR_DIRECTORY      TF_ATTR_DIRECTORY
#define TF_FILEATTR_ARCHIVE        TF_ATTR_ARCHIVE
#define TF_FILEATTR_LONG_FILE_NAME 0x0F   // lfn not support
#define TF_FILEATTR_DELETED        0x40   // not for user
#define TF_FILEATTR_EMPTY          0xFF   // not for user
#define TF_MASK_MATCH(attr, mask)  (((attr) & (mask)) == (mask))


// global
static tf_fs_t fs_pool[TF_MAX_FS_NUM] = {0};


/**
 * @brief get next cluster id from fat table, use cache
 *
 * @param fs
 * @param clus_id
 * @return uint32_t
 */
static uint32_t tf_next_cluster(tf_fs_t* fs, uint32_t clus_id) {
    if (!fs->fatcache_inited ||   // cache not inited
        (clus_id < fs->fatcache_start) || (clus_id - fs->fatcache_start >= (TF_DEFALUT_SECTOR_SIZE / 4))) {
        // read fat from disk
        fs->fatcache_start  = clus_id & (~(uint32_t)(TF_DEFALUT_SECTOR_SIZE / 4 - 1));
        fs->fatcache_inited = true;
        tf_disk_read_co(fs->dev, fs->fat_sec_ofs + fs->fatcache_start / (TF_DEFALUT_SECTOR_SIZE / 4), fs->sec_size,
                        (uint8_t*)fs->fatcache);
    }

    return fs->fatcache[clus_id - fs->fatcache_start];
}


/**
 * @brief read sector from disk, use cache
 *
 * @param fs
 * @param sec
 */
static void tf_fs_disk_read(tf_fs_t* fs, uint32_t sec) {
    if (!fs->cache_inited || sec != fs->cache_sec) {
        fs->cache_sec    = sec;
        fs->cache_inited = true;
        tf_disk_read_co(fs->dev, sec, fs->sec_size, fs->cache);
    }
}


/**
 * @brief prefetch file data from disk to ram cache
 *
 * @param item: file or dir
 * @return bool: return false when no data can prefetch (all cluster has been read)
 */
static bool tf_item_data_prefetch(tf_item_t* item) {
    tf_fs_t* fs = item->fs;

    // byte offset in current cluster
    uint16_t cur_clus_ofs = item->cur_ofs % (fs->sec_size * fs->clus_sec_num);

    // if current cluster read finished, try find next cluster
    if (item->cur_ofs != 0 && cur_clus_ofs == 0) {
        // find next cluster
        uint32_t next_clus = tf_next_cluster(fs, item->cur_clus);

        if (TF_CLUSTER_ID_VALID(next_clus)) {
            item->cur_clus = next_clus;
        } else {
            // no next cluster
            return false;
        }
    }

    tf_fs_disk_read(fs, fs->dat_sec_ofs + fs->clus_sec_num * (item->cur_clus - 2) + (cur_clus_ofs / fs->sec_size));

    return true;
}


/**
 * @brief parse a directory item from raw data
 *
 * @param raw
 * @param item
 */
static void tf_item_parse(uint8_t* raw, tf_item_t* item) {
    uint8_t attr = util_get_value_from_block(raw, 11, 1);   // DIR_Attr  11  1

    if (attr == 0) {
        item->attr = TF_FILEATTR_EMPTY;
    } else if (raw[0] == 0xE5) {
        item->attr = TF_FILEATTR_DELETED;
    } else if (TF_MASK_MATCH(attr, TF_FILEATTR_LONG_FILE_NAME)) {
        // lfn
        item->attr = attr;
#if TF_LFN_SUPPORTTED
        item->name[TF_LFN_LEN_MAX - 1] = util_get_value_from_block(raw, 0, 1);   // LDIR_Ord 0   1
        memcpy(item->name + 0, raw + 1, 10);                                     // p1       1   10
        memcpy(item->name + 10, raw + 14, 12);                                   // p2       14  12
        memcpy(item->name + 22, raw + 28, 4);                                    // p3       28  4
#endif
    } else {
        // sfn
        item->attr = attr;

        memcpy(item->sfn, raw + 0, 11);   // DIR_Name  0   11
        item->sfn[TF_SFN_LEN - 1] = '\0';

        uint16_t temp          = util_get_value_from_block(raw, 24, 2);      // DIR_WrtDate    24 2
        item->write_time.year  = 1980 + (temp >> 9);                         // [15:9]
        item->write_time.month = (temp >> 9) & 0x0F;                         // [8:5]
        item->write_time.day   = temp & 0x1F;                                // [4:0]

        temp                   = util_get_value_from_block(raw, 22, 2);      // DIR_WrtTime    22 2
        item->write_time.year  = temp >> 11;                                 // [15:11]
        item->write_time.month = (temp >> 5) & 0x3F;                         // [10:5]
        item->write_time.day   = temp & 0x1F;                                // [4:0]

        temp                    = util_get_value_from_block(raw, 16, 2);     // DIR_CrtDate    16 2
        item->create_time.year  = 1980 + (temp >> 9);                        // [15:9]
        item->create_time.month = (temp >> 9) & 0x0F;                        // [8:5]
        item->create_time.day   = temp & 0x1F;                               // [4:0]

        temp                    = util_get_value_from_block(raw, 14, 2);     // DIR_CrtTime    14 2
        item->create_time.year  = temp >> 11;                                // [15:11]
        item->create_time.month = (temp >> 5) & 0x3F;                        // [10:5]
        item->create_time.day   = temp & 0x1F;                               // [4:0]

        item->first_clus = (util_get_value_from_block(raw, 20, 2) << 16) |   // DIR_FstClusHI  20 2
                           util_get_value_from_block(raw, 26, 2);            // DIR_FstClusLO  26 2
        item->size = util_get_value_from_block(raw, 28, 4);                  // DIR_FileSize   28 4

        item->cur_clus = item->first_clus;
    }
}


/**
 * @brief mount a device to file system
 *
 * @param dev device id
 * @param label
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_MOUNT_LABEL_USED, TF_ERR_NO_FREE_FS, TF_ERR_NO_FAT32LBA
 */
int tf_mount(int dev, char label) {
    uint16_t volume_ofs = 0;

    if (label == 0) {
        return TF_ERR_WRONG_PARAM;
    }

    int free = TF_MAX_FS_NUM;
    for (int i = 0; i < TF_MAX_FS_NUM; i++) {
        if (fs_pool[i].label == label) {
            return TF_ERR_MOUNT_LABEL_USED;
        }

        if (free == TF_MAX_FS_NUM && fs_pool[i].label == 0) {
            free = i;
        }
    }
    if (free == TF_MAX_FS_NUM) {
        return TF_ERR_NO_FREE_FS;
    }

    tf_fs_t* fs = &fs_pool[free];
    memset(fs, 0, sizeof(tf_fs_t));
    fs->label    = label;
    fs->dev      = dev;
    fs->sec_size = TF_DEFALUT_SECTOR_SIZE;

    // read first sector
    tf_fs_disk_read(fs, 0);

#if TF_WITH_MBR
    // find first FAT32(LBA) partition
    int i = 0;
    for (i = 0; i < 4; i++) {
        if (util_get_value_from_block(fs->cache, 446 + 16 * i + 4, 1) == 0x0C) {   // 0x0C: FAT32 (LBA)
            volume_ofs = util_get_value_from_block(fs->cache, 446 + 16 * i + 8, 4);
            break;
        }
    }
    if (i == 4) {        // no fat32lba partition
        fs->label = 0;   // free
        return TF_ERR_NO_FAT32LBA;
    }
#else
    // no MBR
#endif

    // read Boot sector
    tf_fs_disk_read(fs, volume_ofs);

    // check partition at volume_ofs is FAT32LBA
    // todo

    fs->sec_size            = util_get_value_from_block(fs->cache, 11, 2);   // BPB_BytsPerSec
    fs->clus_sec_num        = util_get_value_from_block(fs->cache, 13, 1);   // BPB_SecPerClus
    uint16_t resv_sec_num   = util_get_value_from_block(fs->cache, 14, 2);   // BPB_RsvdSecCnt
    uint8_t  fat_num        = util_get_value_from_block(fs->cache, 16, 1);   // BPB_NumFATs
    // uint32_t hidden_sec_num = util_get_value_from_block(fs->cache, 28, 4);   // BPB_HiddSec
    fs->sec_num_total       = util_get_value_from_block(fs->cache, 32, 4);   // BPB_TotSec32
    uint32_t fat_sec_num    = util_get_value_from_block(fs->cache, 36, 4);   // BPB_FATSz32
    uint16_t fsinfo_sec     = util_get_value_from_block(fs->cache, 48, 2);   // BPB_FSInfo

    // read FSInfo sector
    tf_fs_disk_read(fs, volume_ofs + fsinfo_sec);

    fs->free_clus_num  = util_get_value_from_block(fs->cache, 488, 4);   // FSI_Free_Count
    fs->next_free_clus = util_get_value_from_block(fs->cache, 492, 4);   // FSI_Nxt_Free

    fs->fat_sec_ofs = volume_ofs + resv_sec_num;
    fs->dat_sec_ofs = fs->fat_sec_ofs + fat_sec_num * fat_num;

    return 0;
}

/**
 * @brief unmount device
 *
 * @param dev id
 * @return int 0, TF_ERR_FS_UNMOUNT
 */
int tf_unmount(int dev) {
    int i;
    for (i = 0; i < TF_MAX_FS_NUM; i++) {
        if (fs_pool[i].dev == dev) {
            break;
        }
    }
    if (i == TF_MAX_FS_NUM) {
        return TF_ERR_FS_UNMOUNT;
    }

    fs_pool[i].label = 0;

    return 0;
}


/**
 * @brief open a file or dir
 *
 * @param path absolute path, like "/xxx" or "X:/xxx"
 * @param item the file or dir at the path, result value
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_PATH_INVALID, TF_ERR_PATH_NOT_FOUND, TF_ERR_ITEM_NOT_DIR
 */
int tf_item_open(const char* path, tf_item_t* item) {
    if (path == nullptr || item == nullptr) {
        return TF_ERR_WRONG_PARAM;
    }

    int pathlen = strlen(path);

    if (pathlen == 0) {
        return TF_ERR_PATH_INVALID;
    }

    // path could be like "/a/b/c" or "x:/a/b/c"
    if (!(path[0] == '/') && !(pathlen >= 3 && path[1] == ':' && path[2] == '/')) {
        return TF_ERR_PATH_INVALID;
    }

    const char* subpath = nullptr;
    int         i;

    // which fs of this path
    for (i = 0; i < TF_MAX_FS_NUM; i++) {
        if (fs_pool[i].label == 0) {
            continue;
        }

        // path like "/a/b/c", first fs
        if (path[0] == '/') {
            subpath = &path[1];
            break;
        }

        // path like "x:/a/b/c"
        if (path[0] == fs_pool[i].label) {
            subpath = &path[3];
            break;
        }
    }
    if (i == TF_MAX_FS_NUM) {
        return TF_ERR_PATH_NOT_FOUND;
    }

    // set item as root dir, cluster id start at 2
    item->fs     = &fs_pool[i];
    item->attr   = TF_FILEATTR_DIRECTORY;
    item->sfn[0] = fs_pool[i].label;
    item->sfn[1] = '\0';

    item->first_clus = 2;
    item->cur_clus   = item->first_clus;
    item->cur_ofs    = 0;

    // search subpath
    return tf_dir_find(item, subpath, item);
}


/**
 * @brief close a file or dir
 *
 * @param dir
 * @return int 0, TF_ERR_WRONG_PARAM
 */
int tf_item_close(tf_item_t* item) {
    if (item == nullptr) {
        return TF_ERR_WRONG_PARAM;
    }
    return 0;
};


/**
 * @brief read item from dir
 *
 * @param dir should be dir really
 * @param item the item read from the dir, result value
 * @return int 0, TF_STA_READDIR_END, TF_ERR_WRONG_PARAM, TF_ERR_ITEM_NOT_DIR
 */
int tf_dir_read(tf_item_t* dir, tf_item_t* item) {
    if (dir == nullptr || item == nullptr) {
        return TF_ERR_WRONG_PARAM;
    }

    if (!TF_MASK_MATCH(dir->attr, TF_FILEATTR_DIRECTORY)) {
        return TF_ERR_ITEM_NOT_DIR;
    }

    tf_fs_t* fs = dir->fs;

    while (true) {
        if (!tf_item_data_prefetch(dir)) {
            return TF_STA_READDIR_END;
        }

        tf_item_parse(fs->cache + (dir->cur_ofs % fs->sec_size), item);
        dir->cur_ofs += TF_DIRITEM_SIZE;

        item->fs = dir->fs;

        if (TF_MASK_MATCH(item->attr, TF_FILEATTR_EMPTY)) {
            return TF_STA_READDIR_END;
        }

        if (TF_MASK_MATCH(item->attr, TF_FILEATTR_DELETED)) {   // deleted item, ignore it
            continue;
        }

        if (TF_MASK_MATCH(item->attr, TF_FILEATTR_LONG_FILE_NAME)) {
            // lfn item, ignore
            continue;
#if TF_LFN_SUPPORTTED
            // for lfn
            uint8_t ord = item->name[TF_LFN_LEN_MAX - 1];

            if (TF_MASK_MATCH(ord, 0x40)) {   // first lfn
                seq = (ord & 0x3F) - 1;
            } else if (seq != ord) {          // invalid lfn item
                seq = 0;
                continue;
            } else {
                seq = ord - 1;
            }

            if (seq >= TF_LFN_LEN_MAX / 26 - 1) {   // lfn too long, not supported
                seq = 0;
                continue;
            }

            if (seq != 0) {
                // current sequense, 13 wchar in lfn item
                for (int i = 0; i < 26; i++) {
                    item->name[i + 26 * seq] = item->name[i];
                }
            } else {
                // name: utf-16 -> ascii
                int i = 0;
                for (i = 1; i < TF_LFN_LEN_MAX / 2; i++) {
                    item->name[i] = item->name[i << 1];
                }

                with_lfn = true;
                seq      = 0;
                continue;
            }
#endif
        } else {
            // sfn
            return 0;
        }
    }
}


/**
 * @brief find an item from dir of subpath
 *
 * @param dir should be dir really
 * @param subpath should not start by '/'
 * @param item the item found in the dir, result value
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_ITEM_NOT_DIR, TF_ERR_PATH_INVALID, TF_ERR_PATH_NOT_FOUND
 */
int tf_dir_find(tf_item_t* dir, const char* subpath, tf_item_t* item) {
    if (dir == nullptr || subpath == nullptr || item == nullptr) {
        return TF_ERR_WRONG_PARAM;
    }
    if (!TF_MASK_MATCH(dir->attr, TF_FILEATTR_DIRECTORY)) {
        return TF_ERR_ITEM_NOT_DIR;
    }

    static tf_item_t tempbase;
    static char      name[TF_FN_LEN_MAX] = {0};
    static char      sfn[TF_SFN_LEN]     = {0};
    bool             part_found;

    memcpy(&tempbase, dir, sizeof(tf_item_t));

    while (true) {
        util_logger("<tf_dir_find> enter dir `%s`, try find `%s`\n", dir->sfn, subpath);

        if (subpath[0] == '\0') {
            // subpath like "a/b/c/"
            util_logger("<tf_dir_find> found `%s`\n", subpath);
            memcpy(item, &tempbase, sizeof(tf_item_t));
            return 0;
        }

        if (subpath[0] == '/') {
            return TF_ERR_PATH_INVALID;
        }

        // first part of subpath
        int sep = util_get_1st_subpath(subpath, name);
        util_name2sfn(name, sfn);

        part_found = false;
        while (tf_dir_read(&tempbase, item) == 0) {
            if (strcmp(item->sfn, sfn) == 0) {
                if (strcmp(name, "..") == 0 && item->first_clus == 0) {
                    // ".." is the root cluster
                    item->cur_clus = item->first_clus = 2;
                }

                if (subpath[sep] == '\0') {
                    // item is the wanted file/dir
                    util_logger("<tf_dir_find> found `%s`\n", subpath);
                    return 0;
                }

                // subpath[sep] == '/'
                // item is a subdir contains the wanted file/dir

                // confirm item is a dir
                if (!TF_MASK_MATCH(item->attr, TF_FILEATTR_DIRECTORY)) {
                    return TF_ERR_PATH_NOT_DIR;
                }

                memcpy(&tempbase, item, sizeof(tf_item_t));
                subpath += sep + 1;
                part_found = true;
                break;
            }
        }
        if (!part_found) {
            // path part not found
            return TF_ERR_PATH_NOT_FOUND;
        }
    }
}


/**
 * @brief read file content, once read, the file ptr will move
 *
 * @param file should be really file
 * @param buffer should be large enough to store the data you want
 * @param size the data size wanted
 * @return int the data size really read
 */
int tf_file_read(tf_file_t* file, uint8_t* buffer, uint32_t size) {
    if (file == nullptr || buffer == nullptr) {
        return TF_ERR_WRONG_PARAM;
    }

    if (TF_MASK_MATCH(file->attr, TF_FILEATTR_ARCHIVE) && (size > file->size - file->cur_ofs)) {
        size = file->size - file->cur_ofs;
    }

    uint32_t size_read = 0;
    tf_fs_t* fs        = file->fs;

    while (size > 0) {
        if (!tf_item_data_prefetch(file)) {
            break;
        }
        // size may larger than a sector
        // if the wanted data all in this sector, read all
        // or read remain data in this sector this time

        uint16_t ofs     = file->cur_ofs % fs->sec_size;
        uint16_t readnow = ofs + size < fs->sec_size ? size : fs->sec_size - ofs;

        memcpy(&buffer[size_read], &fs->cache[ofs], readnow);
        file->cur_ofs += readnow;
        size_read += readnow;
        size -= readnow;
    }

    return size_read;
}
