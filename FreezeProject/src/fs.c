#include "fs.h"
#include "disk.h"
#include "memory.h"
#include "vga.h"
#include "fat32.h"

char* strcpy(char* dest, const char* src);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
size_t strlen(const char* s);

static int fs_type = 0;  /* 0 = custom, 1 = FAT32 */

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = 0;
    return dest;
}

static void safe_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = 0;
}

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = dest;
    const char* s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    char* p = s;
    for (size_t i = 0; i < n; i++) {
        p[i] = c;
    }
    return s;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

struct file files[MAX_FILES];
static uint16_t next_free_sector = FS_DATA_START;

static int fs_alloc_slot(const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            safe_strncpy(files[i].name, name, MAX_FILENAME);
            files[i].used = 1;
            files[i].size = 0;
            files[i].disk_size = 0;
            files[i].inode_num = 0;
            files[i].start_sector = 0;
            files[i].sector_count = 0;
            files[i].dirty = 0;
            return i;
        }
    }

    return -1;
}

void fs_init() {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].disk_size = 0;
        files[i].inode_num = 0;
        files[i].start_sector = 0;
        files[i].sector_count = 0;
        files[i].dirty = 0;
    }
    next_free_sector = FS_DATA_START;
}

static void fs_write_header() {
    uint8_t header_buf[512];
    memset(header_buf, 0, 512);
    
    uint32_t* magic_ptr = (uint32_t*)header_buf;
    *magic_ptr = FS_MAGIC;
    
    uint32_t* version_ptr = (uint32_t*)(header_buf + 4);
    *version_ptr = FS_VERSION;
    
    uint32_t count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) count++;
    }
    uint32_t* count_ptr = (uint32_t*)(header_buf + 8);
    *count_ptr = count;
    
    disk_write_sector(FS_HEADER_SECTOR, header_buf);
}

static int fs_calculate_sectors_needed(uint32_t size) {
    if (size == 0) return 0;
    return (size + 511) / 512;
}

void fs_mount() {
    /* Try mounting as FAT32 first */
    if (fat32_mount() == 0) {
        fs_type = 1;
        print("[FS] FAT32 filesystem detected and mounted\n");
        return;
    }
    
    /* Fall back to custom filesystem */
    fs_type = 0;
    uint8_t header_buf[512];
    
    disk_read_sector(FS_HEADER_SECTOR, header_buf);
    
    uint32_t magic = *(uint32_t*)header_buf;
    if (magic != FS_MAGIC) {
        fs_write_header();
        return;
    }
    
    uint32_t file_count = *(uint32_t*)(header_buf + 8);
    
    for (uint32_t i = 0; i < file_count && i < MAX_FILES; i++) {
        uint8_t metadata_buf[512];
        disk_read_sector(FS_METADATA_START + i, metadata_buf);
        
        struct fs_metadata* meta = (struct fs_metadata*)metadata_buf;
        
        if (meta->name[0] == 0) continue;
        
        files[i].used = 1;
        safe_strncpy(files[i].name, meta->name, MAX_FILENAME);
        files[i].size = meta->size;
        files[i].disk_size = meta->size;
        files[i].start_sector = meta->start_sector;
        files[i].sector_count = meta->sector_count;
        files[i].dirty = 0;
        
        if (files[i].size > 0 && files[i].size <= MAX_FILE_SIZE) {
            for (uint16_t s = 0; s < files[i].sector_count && s < 8; s++) {
                uint8_t sector_buf[512];
                disk_read_sector(files[i].start_sector + s, sector_buf);
                
                uint32_t bytes_to_copy = 512;
                if (files[i].size - s * 512 < 512) {
                    bytes_to_copy = files[i].size - s * 512;
                }
                
                memcpy(files[i].content + s * 512, sector_buf, bytes_to_copy);
            }
        }
        
        if (files[i].start_sector + files[i].sector_count > next_free_sector) {
            next_free_sector = files[i].start_sector + files[i].sector_count;
        }
    }
}

int fs_create(const char* name) {
    if (fs_type == 1) {
        int existing = fs_find(name);
        int fd;

        if (existing >= 0) {
            return existing;
        }

        fd = fs_alloc_slot(name);
        if (fd >= 0) {
            files[fd].dirty = 1;
        }
        return fd;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            safe_strncpy(files[i].name, name, MAX_FILENAME);
            files[i].used = 1;
            files[i].size = 0;
            files[i].disk_size = 0;
            files[i].inode_num = 0;
            files[i].start_sector = 0;
            files[i].sector_count = 0;
            files[i].dirty = 1;
            return i;
        }
    }
    return -1;
}

int fs_write(int fd, const char* data, uint32_t size) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;
    
    if (size > MAX_FILE_SIZE) size = MAX_FILE_SIZE;
    
    memcpy(files[fd].content, data, size);
    files[fd].size = size;
    files[fd].dirty = 1;
    
    return size;
}

int fs_read(int fd, char* buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;

    if (fs_type == 1) {
        if (files[fd].dirty) {
            if (size > files[fd].size) size = files[fd].size;
            memcpy(buffer, files[fd].content, size);
            return size;
        }

        if (size > files[fd].size) size = files[fd].size;
        return fat32_read_file(files[fd].name, 0, buffer, size);
    }
    
    if (size > files[fd].size) size = files[fd].size;
    
    memcpy(buffer, files[fd].content, size);
    
    return size;
}

int fs_find(const char* name) {
    if (fs_type == 1) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (files[i].used) {
                const char* a = files[i].name;
                const char* b = name;
                while (*a && *b) {
                    if (*a != *b) break;
                    a++; b++;
                }
                if (*a == *b) return i;
            }
        }

        {
            struct fat32_file_info info;
            if (fat32_find_path(name, &info) == 0 && (info.attrs & 0x10) == 0) {
                int fd = fs_alloc_slot(name);
                if (fd < 0) {
                    return -1;
                }
                files[fd].inode_num = info.first_cluster;
                files[fd].size = info.size;
                files[fd].disk_size = info.size;
                return fd;
            }
        }

        return -1;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            const char* a = files[i].name;
            const char* b = name;
            while (*a && *b) {
                if (*a != *b) break;
                a++; b++;
            }
            if (*a == *b) return i;
        }
    }
    return -1;
}

int fs_save(int fd) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;

    if (fs_type == 1) {
        uint32_t first_cluster = files[fd].inode_num;

        if (fat32_write_file(files[fd].name, files[fd].content, files[fd].size, &first_cluster) != 0) {
            return -1;
        }

        files[fd].inode_num = first_cluster;
        files[fd].dirty = 0;
        files[fd].disk_size = files[fd].size;
        return 0;
    }
    
    uint32_t sectors_needed = fs_calculate_sectors_needed(files[fd].size);
    
    if (files[fd].start_sector == 0 && sectors_needed > 0) {
        files[fd].start_sector = next_free_sector;
        next_free_sector += sectors_needed;
    }
    
    files[fd].sector_count = sectors_needed;
    
    for (uint16_t s = 0; s < sectors_needed; s++) {
        uint8_t sector_buf[512];
        memset(sector_buf, 0, 512);
        
        uint32_t bytes_to_write = 512;
        if (files[fd].size - s * 512 < 512) {
            bytes_to_write = files[fd].size - s * 512;
        }
        
        if (bytes_to_write > 0) {
            memcpy(sector_buf, files[fd].content + s * 512, bytes_to_write);
        }
        
        disk_write_sector(files[fd].start_sector + s, sector_buf);
    }
    
    uint8_t metadata_buf[512];
    memset(metadata_buf, 0, 512);
    struct fs_metadata* meta = (struct fs_metadata*)metadata_buf;
    safe_strncpy(meta->name, files[fd].name, MAX_FILENAME);
    meta->size = files[fd].size;
    meta->start_sector = files[fd].start_sector;
    meta->sector_count = files[fd].sector_count;
    meta->flags = 0;
    
    disk_write_sector(FS_METADATA_START + fd, metadata_buf);
    
    files[fd].dirty = 0;
    files[fd].disk_size = files[fd].size;
    
    return 0;
}

int fs_load(int fd) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].used) return -1;
    if (files[fd].start_sector == 0) return -1;
    
    for (uint16_t s = 0; s < files[fd].sector_count; s++) {
        uint8_t sector_buf[512];
        disk_read_sector(files[fd].start_sector + s, sector_buf);
        
        uint32_t bytes_to_copy = 512;
        if (files[fd].size - s * 512 < 512) {
            bytes_to_copy = files[fd].size - s * 512;
        }
        
        memcpy(files[fd].content + s * 512, sector_buf, bytes_to_copy);
    }
    
    return 0;
}

void fs_sync() {
    if (fs_type == 1) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (files[i].used && files[i].dirty) {
                fs_save(i);
            }
        }
        return;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && files[i].dirty) {
            fs_save(i);
        }
    }
    
    fs_write_header();
}

int fs_delete(const char* name) {
    if (fs_type == 1) {
        return fat32_delete(name);
    }

    int fd = fs_find(name);
    if (fd < 0) return -1;
    
    files[fd].used = 0;
    files[fd].size = 0;
    files[fd].dirty = 0;
    
    uint8_t metadata_buf[512];
    memset(metadata_buf, 0, 512);
    disk_write_sector(FS_METADATA_START + fd, metadata_buf);
    
    return 0;
}

int fs_list_path(const char* path) {
    if (fs_type == 1) {
        if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
            print("[FAT32] Root directory:\n");
        } else {
            print("[FAT32] Directory: ");
            print(path);
            print("\n");
        }
        return fat32_list_path(path && path[0] ? path : "/");
    }

    if (path && path[0]) {
        return -1;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            print(files[i].name);
            print(" (");
            print_int(files[i].size);
            print(" bytes)\n");
        }
    }

    return 0;
}

void fs_list() {
    fs_list_path("");
}