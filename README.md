# toyfs

> "just for fun!"

A toy file system.

FAT32 is ugly, choose it only for convenience to debug or use.

Unfinished, only implement the file read functions simply.

Files need to be modified for migration:

- `toyfs_disk.c`: base functions to read and write disks, should be implemented
- `toyfs_cfg.h`: some configs
- `main.c`: main test file, use a vhd (MBR+FAT32)