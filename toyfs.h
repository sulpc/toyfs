#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toyfs_cfg.h"
#include "toyfs_utils.h"


#define TF_ERR_WRONG_PARAM       -1
#define TF_ERR_NO_FAT32LBA       -2
#define TF_ERR_NO_FREE_FS        -3
#define TF_ERR_FS_UNMOUNT        -4
#define TF_ERR_PATH_INVALID      -5
#define TF_ERR_PATH_NOT_DIR      -10
#define TF_ERR_PATH_NOT_FOUND    -6
#define TF_ERR_MOUNT_LABEL_USED  -7
#define TF_ERR_ITEM_NOT_DIR      -8
#define TF_ERR_LFN_NOT_SUPPORTED -9
#define TF_STA_READDIR_END       -101
#define TF_STA_READFILE_END      -102
#define TF_ATTR_READ_ONLY        0x01
#define TF_ATTR_HIDDEN           0x02
#define TF_ATTR_SYSTEM           0x04
#define TF_ATTR_VOLUME_ID        0x08
#define TF_ATTR_DIRECTORY        0x10
#define TF_ATTR_ARCHIVE          0x20


typedef struct {
    uint8_t dev;     // physical disk id
    char    label;   // label, like: 'C', 'D', '0', '1'; '\0' means not used

    // BS
    uint16_t sec_size;        // sector size
    uint8_t  clus_sec_num;    // sector count of a cluster
    uint32_t sec_num_total;   // sector count of volume

    // FSInfo
    uint32_t free_clus_num;    // FSI_Free_Count
    uint32_t next_free_clus;   // FSI_Nxt_Free

    // for convenience
    int fat_sec_ofs;   // sector offset of FAT area in all DISK
    int dat_sec_ofs;   // sector offset of DATA area in DISK

    uint8_t  cache[512];
    uint32_t cache_sec;   // cache sector id
    bool     cache_inited;

    uint32_t fatcache[512 / 4];
    uint32_t fatcache_start;   // fatcache start cluster id
    bool     fatcache_inited;
} tf_fs_t;

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minite;
    uint8_t  second;
} tf_time_t;

typedef struct {
    uint8_t   attr;              // bitmap of TF_ATTR_*
    char      sfn[TF_SFN_LEN];   //
    uint32_t  size;              // size of file
    uint32_t  first_clus;        // first cluster id (start at 2)
    uint32_t  cur_clus;          //
    uint32_t  cur_ofs;           // current byte offset
    tf_time_t write_time;
    tf_time_t create_time;
    tf_fs_t*  fs;
} tf_item_t;


#define tf_dir_t      tf_item_t
#define tf_file_t     tf_item_t
#define tf_dir_open   tf_item_open
#define tf_dir_close  tf_item_open
#define tf_file_open  tf_item_open
#define tf_file_close tf_item_open


/**
 * @brief mount a device to file system
 *
 * @param dev device id
 * @param label
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_MOUNT_LABEL_USED, TF_ERR_NO_FREE_FS, TF_ERR_NO_FAT32LBA
 */
int tf_mount(int dev, char label);

/**
 * @brief unmount device
 *
 * @param dev id
 * @return int 0, TF_ERR_FS_UNMOUNT
 */
int tf_unmount(int dev);

/**
 * @brief open a file or dir
 *
 * @param path absolute path, like "/xxx" or "X:/xxx"
 * @param item the file or dir at the path, result value
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_PATH_INVALID, TF_ERR_PATH_NOT_FOUND, TF_ERR_ITEM_NOT_DIR
 */
int tf_item_open(const char* path, tf_item_t* item);

/**
 * @brief close a file or dir
 *
 * @param dir
 * @return int 0, TF_ERR_WRONG_PARAM
 */
int tf_item_close(tf_item_t* item);

/**
 * @brief read item from dir
 *
 * @param dir should be dir really
 * @param item the item read from the dir, result value
 * @return int 0, TF_STA_READDIR_END, TF_ERR_WRONG_PARAM, TF_ERR_ITEM_NOT_DIR
 */
int tf_dir_read(tf_item_t* dir, tf_item_t* item);

/**
 * @brief find an item from dir of subpath
 *
 * @param dir should be dir really
 * @param subpath should not start by '/'
 * @param item the item found in the dir, result value
 * @return int 0, TF_ERR_WRONG_PARAM, TF_ERR_ITEM_NOT_DIR, TF_ERR_PATH_INVALID, TF_ERR_PATH_NOT_FOUND
 */
int tf_dir_find(tf_item_t* dir, const char* subpath, tf_item_t* item);   // dir&item may be same object

/**
 * @brief read file content, once read, the file ptr will move
 *
 * @param file should be really file
 * @param buffer should be large enough to store the data you want
 * @param size the data size wanted
 * @return int the data size really read
 */
int tf_file_read(tf_file_t* file, uint8_t* buffer, uint32_t size);

/**
 * @brief read a sector from disk
 *
 * @param dev device id
 * @param sec sector id
 * @param sec_size sector size
 * @param data data buffer
 * @return int 0，-1
 */
int tf_disk_read_co(int dev, uint32_t sec, uint16_t sec_size, uint8_t* data);


// tbd
/*
int tf_format();
int tf_dir_create();
int tf_file_seek();
int tf_file_write();
*/
