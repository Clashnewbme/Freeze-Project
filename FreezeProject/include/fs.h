#ifndef FS_H
#define FS_H

#include <stdint.h>
#include "fat32.h"

#define MAX_FILES 32
#define MAX_FILENAME 32
#define MAX_FILE_SIZE 4096

#define FS_MAGIC 0x46525A45
#define FS_VERSION 1

struct fs_metadata {
    char name[MAX_FILENAME];
    uint32_t size;
    uint16_t start_sector;
    uint16_t sector_count;
    uint32_t flags;
};

struct file {
    char name[MAX_FILENAME];
    char content[MAX_FILE_SIZE];
    uint32_t size;
    uint32_t disk_size;
    uint32_t inode_num;
    uint16_t start_sector;
    uint16_t sector_count;
    int used;
    int dirty;
};

extern struct file files[MAX_FILES];

void fs_init();
void fs_mount();
void fs_sync();
int fs_create(const char* name);
int fs_write(int fd, const char* data, uint32_t size);
int fs_read(int fd, char* buffer, uint32_t size);
int fs_find(const char* name);
void fs_list();
int fs_list_path(const char* path);
int fs_delete(const char* name);
int fs_save(int fd);
int fs_load(int fd);

#endif