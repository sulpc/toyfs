#include "toyfs.h"

int tf_disk_read_co(int dev, uint32_t sec, uint16_t sec_size, uint8_t* data) {
    if (dev != MY_DISK_ID) {
        return -1;
    }

    FILE* vhd = fopen("../fat32.vhd", "rb");   // vhd: MBR+FAT32
    fseek(vhd, sec * sec_size, SEEK_SET);
    fread(data, 1, sec_size, vhd);
    fclose(vhd);

    return 0;
}
