#include "stdlib.h"
#include "toyfs.h"


#define exit_if_error(ret)                                                                                             \
    do {                                                                                                               \
        if (ret != 0) {                                                                                                \
            printf("ERROR %d\n", ret);                                                                                 \
            exit(0);                                                                                                   \
        }                                                                                                              \
    } while (0)

int main(int argc, char* argv[]) {
    int         ret;
    tf_item_t   dir, item;
    const char* path;
    uint8_t     buffer[4096] = {0};
    char        name[16]     = {0};

    if (argc < 2) {
        printf("usage: cmd <path>\n");
        path = "/dir/././././..";
        // return 0;
    } else {
        path = argv[1];
    }

    ret = tf_mount(MY_DISK_ID, 'X');
    exit_if_error(ret);

    ret = tf_item_open(path, &dir);
    exit_if_error(ret);

    if (dir.attr & TF_ATTR_ARCHIVE) {
        printf("file<%s>:\n", path);
        int read;

        while ((read = tf_file_read(&dir, buffer, 10)) > 0) {
            buffer[read] = '\0';
            printf("%s", buffer);
        }
        printf("\n");
    } else {
        printf("dir<%s>:\n", path);

        while (tf_dir_read(&dir, &item) == 0) {
            if ((item.attr & TF_ATTR_HIDDEN) || (item.attr & TF_ATTR_SYSTEM)) {
                continue;
            }

            if (item.attr & TF_ATTR_DIRECTORY) {
                printf("\033[34m");
            } else {   // (item.attr & TF_ATTR_ARCHIVE)
                printf("\033[32m");
            }

            util_sfn2name(item.sfn, name);
            // printf("`%s` %s\n", item.sfn, name);
            printf("%s\033[0m ", name);
        }
        printf("\n");
    }

    tf_unmount(MY_DISK_ID);

    printf("bye.\n");
}
