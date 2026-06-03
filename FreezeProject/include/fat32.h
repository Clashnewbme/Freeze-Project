#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

struct fat32_file_info {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t dir_cluster;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint8_t attrs;
};

int fat32_mount();
int fat32_find_path(const char* path, struct fat32_file_info* info);
int fat32_read_file(const char* path, uint32_t offset, char* buffer, uint32_t size);
int fat32_write_file(const char* path, const char* data, uint32_t size, uint32_t* first_cluster_out);
int fat32_delete(const char* path);
int fat32_list_path(const char* path);

#endif