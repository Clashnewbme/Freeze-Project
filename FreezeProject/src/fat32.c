#include "fat32.h"

#include "disk.h"
#include "vga.h"

#include <stddef.h>
#include <stdint.h>

extern void* memcpy(void* dest, const void* src, size_t n);
extern void* memset(void* s, int c, size_t n);

#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_LFN 0x0F
#define FAT32_ATTR_ARCHIVE 0x20

#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_BAD 0x0FFFFFF7u

#define FAT32_DEFAULT_TOTAL_SECTORS 20480u
#define FAT32_DEFAULT_RESERVED 32u
#define FAT32_DEFAULT_SECTORS_PER_CLUSTER 1u
#define FAT32_DEFAULT_NUM_FATS 2u

struct fat32_bpb {
    uint8_t jmp[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved0[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
    uint8_t boot_code[420];
    uint16_t signature;
} __attribute__((packed));

struct fat32_dirent {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

static struct fat32_bpb bpb;
static uint32_t bytes_per_sector = 512;
static uint32_t sectors_per_cluster = 1;
static uint32_t num_fats = 2;
static uint32_t sectors_per_fat = 0;
static uint32_t fat_start_sector = 0;
static uint32_t data_start_sector = 0;
static uint32_t total_sectors = 0;
static uint32_t root_cluster = 2;
static uint32_t cluster_count = 0;
static uint32_t next_alloc_hint = 3;

static int fat32_read_sector(uint32_t sector, uint8_t* buf) {
    return disk_read_sector(sector, buf);
}

static int fat32_write_sector(uint32_t sector, const uint8_t* buf) {
    return disk_write_sector(sector, buf);
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static uint32_t dirent_first_cluster(const struct fat32_dirent* e) {
    return ((uint32_t)e->first_cluster_high << 16) | e->first_cluster_low;
}

static void dirent_set_first_cluster(struct fat32_dirent* e, uint32_t cluster) {
    e->first_cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    e->first_cluster_low = (uint16_t)(cluster & 0xFFFF);
}

static int fat32_is_eoc(uint32_t v) {
    return v >= FAT32_EOC;
}

static uint32_t cluster_to_sector(uint32_t cluster) {
    return data_start_sector + (cluster - 2) * sectors_per_cluster;
}

static int fat32_read_fat(uint32_t cluster, uint32_t* value_out) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_num = fat_start_sector + (fat_offset / bytes_per_sector);
    uint32_t within = fat_offset % bytes_per_sector;
    uint32_t value;

    if (fat32_read_sector(sector_num, sector) != 0) {
        return -1;
    }

    value = *(uint32_t*)(sector + within) & 0x0FFFFFFF;
    *value_out = value;
    return 0;
}

static int fat32_write_fat_single(uint32_t fat_base, uint32_t cluster, uint32_t value) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t sector_num = fat_base + (fat_offset / bytes_per_sector);
    uint32_t within = fat_offset % bytes_per_sector;
    uint32_t oldv;

    if (fat32_read_sector(sector_num, sector) != 0) {
        return -1;
    }

    oldv = *(uint32_t*)(sector + within);
    oldv &= 0xF0000000u;
    oldv |= (value & 0x0FFFFFFFu);
    *(uint32_t*)(sector + within) = oldv;

    return fat32_write_sector(sector_num, sector);
}

static int fat32_write_fat(uint32_t cluster, uint32_t value) {
    for (uint32_t f = 0; f < num_fats; f++) {
        uint32_t fat_base = fat_start_sector + f * sectors_per_fat;
        if (fat32_write_fat_single(fat_base, cluster, value) != 0) {
            return -1;
        }
    }
    return 0;
}

static void name_to_short83(const char* name, uint8_t out[11]) {
    uint32_t i;
    uint32_t o = 0;
    int in_ext = 0;

    for (i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    for (i = 0; name[i]; i++) {
        char c = to_upper(name[i]);
        if (c == '.') {
            if (!in_ext) {
                in_ext = 1;
                o = 8;
            }
            continue;
        }

        if (!(is_alpha(c) || is_digit(c) || c == '_' || c == '-')) {
            c = '_';
        }

        if (!in_ext) {
            if (o < 8) out[o++] = (uint8_t)c;
        } else {
            if (o < 11) out[o++] = (uint8_t)c;
        }
    }
}

static void short83_to_name(const uint8_t in[11], char* out, uint32_t out_size) {
    uint32_t p = 0;
    uint32_t i;
    int has_ext = 0;

    if (out_size == 0) return;

    for (i = 8; i < 11; i++) {
        if (in[i] != ' ') {
            has_ext = 1;
            break;
        }
    }

    for (i = 0; i < 8 && p + 1 < out_size; i++) {
        if (in[i] == ' ') break;
        out[p++] = (char)in[i];
    }

    if (has_ext && p + 2 < out_size) {
        out[p++] = '.';
        for (i = 8; i < 11 && p + 1 < out_size; i++) {
            if (in[i] == ' ') break;
            out[p++] = (char)in[i];
        }
    }

    out[p] = 0;
}

static int dirent_name_matches(const struct fat32_dirent* e, const uint8_t short_name[11]) {
    for (uint32_t i = 0; i < 11; i++) {
        if (e->name[i] != short_name[i]) return 0;
    }
    return 1;
}

static int fat32_allocate_cluster(uint32_t* cluster_out) {
    uint32_t start = next_alloc_hint;
    uint32_t c;

    if (start < 2) start = 2;
    c = start;

    for (;;) {
        uint32_t v;

        if (c > cluster_count + 1) c = 2;

        if (fat32_read_fat(c, &v) != 0) return -1;
        if (v == 0) {
            uint8_t zero[512];
            memset(zero, 0, sizeof(zero));

            if (fat32_write_fat(c, 0x0FFFFFFF) != 0) return -1;
            for (uint32_t s = 0; s < sectors_per_cluster; s++) {
                if (fat32_write_sector(cluster_to_sector(c) + s, zero) != 0) return -1;
            }

            *cluster_out = c;
            next_alloc_hint = c + 1;
            return 0;
        }

        c++;
        if (c == start) break;
    }

    return -1;
}

static int fat32_free_chain(uint32_t start_cluster) {
    uint32_t c = start_cluster;

    while (c >= 2 && c <= cluster_count + 1) {
        uint32_t next;

        if (fat32_read_fat(c, &next) != 0) return -1;
        if (fat32_write_fat(c, 0) != 0) return -1;

        if (fat32_is_eoc(next) || next == FAT32_BAD || next == 0) break;
        c = next;
    }

    return 0;
}

static int fat32_find_entry_in_dir(uint32_t dir_cluster,
                                   const uint8_t short_name[11],
                                   struct fat32_dirent* entry_out,
                                   uint32_t* sector_out,
                                   uint16_t* offset_out) {
    uint32_t c = dir_cluster;
    uint8_t sector[512];

    while (c >= 2 && c <= cluster_count + 1) {
        uint32_t base = cluster_to_sector(c);

        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (fat32_read_sector(base + s, sector) != 0) return -1;

            for (uint32_t off = 0; off < bytes_per_sector; off += sizeof(struct fat32_dirent)) {
                struct fat32_dirent* e = (struct fat32_dirent*)(sector + off);

                if (e->name[0] == 0x00) return -1;
                if (e->name[0] == 0xE5 || e->attr == FAT32_ATTR_LFN) continue;

                if (dirent_name_matches(e, short_name)) {
                    if (entry_out) *entry_out = *e;
                    if (sector_out) *sector_out = base + s;
                    if (offset_out) *offset_out = (uint16_t)off;
                    return 0;
                }
            }
        }

        {
            uint32_t next;
            if (fat32_read_fat(c, &next) != 0) return -1;
            if (fat32_is_eoc(next) || next == 0) break;
            c = next;
        }
    }

    return -1;
}

static int fat32_find_free_dir_slot(uint32_t dir_cluster, uint32_t* sector_out, uint16_t* offset_out) {
    uint32_t c = dir_cluster;
    uint8_t sector[512];

    while (c >= 2 && c <= cluster_count + 1) {
        uint32_t base = cluster_to_sector(c);

        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (fat32_read_sector(base + s, sector) != 0) return -1;

            for (uint32_t off = 0; off < bytes_per_sector; off += sizeof(struct fat32_dirent)) {
                struct fat32_dirent* e = (struct fat32_dirent*)(sector + off);
                if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                    *sector_out = base + s;
                    *offset_out = (uint16_t)off;
                    return 0;
                }
            }
        }

        {
            uint32_t next;
            if (fat32_read_fat(c, &next) != 0) return -1;
            if (fat32_is_eoc(next) || next == 0) {
                uint32_t new_cluster;
                if (fat32_allocate_cluster(&new_cluster) != 0) return -1;
                if (fat32_write_fat(c, new_cluster) != 0) return -1;
                if (fat32_write_fat(new_cluster, 0x0FFFFFFF) != 0) return -1;
                *sector_out = cluster_to_sector(new_cluster);
                *offset_out = 0;
                return 0;
            }
            c = next;
        }
    }

    return -1;
}

static int fat32_write_dirent(uint32_t sector_num, uint16_t offset, const struct fat32_dirent* entry) {
    uint8_t sector[512];

    if (fat32_read_sector(sector_num, sector) != 0) return -1;
    memcpy(sector + offset, entry, sizeof(*entry));
    return fat32_write_sector(sector_num, sector);
}

static int fat32_split_component(const char* path, uint32_t* index, char* component, uint32_t component_size) {
    uint32_t i = *index;
    uint32_t c = 0;

    while (path[i] == '/') i++;

    if (!path[i]) {
        *index = i;
        component[0] = 0;
        return 0;
    }

    while (path[i] && path[i] != '/') {
        if (c + 1 < component_size) component[c++] = path[i];
        i++;
    }

    component[c] = 0;
    *index = i;
    return 1;
}

static int fat32_mkdir_in_dir(uint32_t parent_cluster, const uint8_t short_name[11], uint32_t* new_cluster_out) {
    struct fat32_dirent entry;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint32_t new_cluster;
    uint8_t sector[512];

    if (fat32_allocate_cluster(&new_cluster) != 0) return -1;

    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, short_name, 11);
    entry.attr = FAT32_ATTR_DIRECTORY;
    dirent_set_first_cluster(&entry, new_cluster);

    if (fat32_find_free_dir_slot(parent_cluster, &dir_sector, &dir_offset) != 0) return -1;
    if (fat32_write_dirent(dir_sector, dir_offset, &entry) != 0) return -1;

    memset(sector, 0, sizeof(sector));
    {
        struct fat32_dirent* dot = (struct fat32_dirent*)sector;
        struct fat32_dirent* dotdot = (struct fat32_dirent*)(sector + sizeof(struct fat32_dirent));

        memset(dot->name, ' ', 11);
        dot->name[0] = '.';
        dot->attr = FAT32_ATTR_DIRECTORY;
        dirent_set_first_cluster(dot, new_cluster);

        memset(dotdot->name, ' ', 11);
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        dotdot->attr = FAT32_ATTR_DIRECTORY;
        dirent_set_first_cluster(dotdot, parent_cluster);
    }

    if (fat32_write_sector(cluster_to_sector(new_cluster), sector) != 0) return -1;

    *new_cluster_out = new_cluster;
    return 0;
}

static int fat32_resolve_path(const char* path,
                              int want_parent,
                              int create_dirs,
                              uint32_t* dir_cluster_out,
                              char* leaf_out,
                              struct fat32_dirent* entry_out,
                              uint32_t* entry_sector_out,
                              uint16_t* entry_offset_out) {
    uint32_t idx = 0;
    uint32_t current = root_cluster;
    char component[64];

    if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        if (want_parent) return -1;
        if (dir_cluster_out) *dir_cluster_out = root_cluster;
        if (leaf_out) leaf_out[0] = 0;
        return 0;
    }

    while (1) {
        int has_component = fat32_split_component(path, &idx, component, sizeof(component));
        uint8_t short_name[11];
        struct fat32_dirent found;
        uint32_t found_sector;
        uint16_t found_offset;

        if (!has_component) return -1;

        while (path[idx] == '/') idx++;
        name_to_short83(component, short_name);

        if (want_parent && path[idx] == 0) {
            if (dir_cluster_out) *dir_cluster_out = current;
            if (leaf_out) {
                uint32_t j = 0;
                while (component[j] && j < 63) {
                    leaf_out[j] = component[j];
                    j++;
                }
                leaf_out[j] = 0;
            }
            return 0;
        }

        if (fat32_find_entry_in_dir(current, short_name, &found, &found_sector, &found_offset) != 0) {
            if (path[idx] == 0) return -1;
            if (!create_dirs) return -1;

            if (fat32_mkdir_in_dir(current, short_name, &current) != 0) return -1;
            continue;
        }

        if (path[idx] == 0) {
            if (entry_out) *entry_out = found;
            if (entry_sector_out) *entry_sector_out = found_sector;
            if (entry_offset_out) *entry_offset_out = found_offset;
            if (dir_cluster_out) *dir_cluster_out = current;
            return 0;
        }

        if ((found.attr & FAT32_ATTR_DIRECTORY) == 0) return -1;

        current = dirent_first_cluster(&found);
        if (current < 2) return -1;
    }
}

static int fat32_create_default_layout() {
    uint8_t sector[512];
    uint32_t total = FAT32_DEFAULT_TOTAL_SECTORS;
    uint32_t spc = FAT32_DEFAULT_SECTORS_PER_CLUSTER;
    uint32_t reserved = FAT32_DEFAULT_RESERVED;
    uint32_t fats = FAT32_DEFAULT_NUM_FATS;
    uint32_t spf = 1;
    uint32_t prev = 0;

    while (spf != prev) {
        uint32_t data_sectors;
        uint32_t clusters;
        prev = spf;
        data_sectors = total - reserved - fats * spf;
        clusters = data_sectors / spc;
        spf = ((clusters + 2) * 4 + 511) / 512;
    }

    for (uint32_t i = 0; i < total; i++) {
        memset(sector, 0, sizeof(sector));
        if (fat32_write_sector(i, sector) != 0) return -1;
    }

    memset(&bpb, 0, sizeof(bpb));
    bpb.jmp[0] = 0xEB;
    bpb.jmp[1] = 0x58;
    bpb.jmp[2] = 0x90;
    memcpy(bpb.oem, "FREEZEOS", 8);
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = (uint8_t)spc;
    bpb.reserved_sector_count = (uint16_t)reserved;
    bpb.num_fats = (uint8_t)fats;
    bpb.media = 0xF8;
    bpb.total_sectors_32 = total;
    bpb.fat_size_32 = spf;
    bpb.root_cluster = 2;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = 0x80;
    bpb.boot_signature = 0x29;
    bpb.volume_id = 0x46525A32;
    memcpy(bpb.volume_label, "FREEZEOS   ", 11);
    memcpy(bpb.fs_type, "FAT32   ", 8);
    bpb.signature = 0xAA55;

    if (fat32_write_sector(0, (const uint8_t*)&bpb) != 0) return -1;

    memset(sector, 0, sizeof(sector));
    *(uint32_t*)(sector + 0) = 0x41615252;
    *(uint32_t*)(sector + 484) = 0x61417272;
    *(uint32_t*)(sector + 488) = total - reserved - fats * spf;
    *(uint32_t*)(sector + 492) = 3;
    *(uint16_t*)(sector + 510) = 0xAA55;
    if (fat32_write_sector(1, sector) != 0) return -1;

    if (fat32_write_sector(6, (const uint8_t*)&bpb) != 0) return -1;

    fat_start_sector = reserved;
    sectors_per_fat = spf;
    num_fats = fats;

    if (fat32_write_fat(0, 0x0FFFFFF8) != 0) return -1;
    if (fat32_write_fat(1, 0x0FFFFFFF) != 0) return -1;
    if (fat32_write_fat(2, 0x0FFFFFFF) != 0) return -1;
    if (fat32_write_fat(3, 0x0FFFFFFF) != 0) return -1;

    data_start_sector = reserved + fats * spf;

    memset(sector, 0, sizeof(sector));
    {
        struct fat32_dirent* home = (struct fat32_dirent*)sector;
        memcpy(home->name, "HOME       ", 11);
        home->attr = FAT32_ATTR_DIRECTORY;
        dirent_set_first_cluster(home, 3);
    }
    if (fat32_write_sector(cluster_to_sector(2), sector) != 0) return -1;

    memset(sector, 0, sizeof(sector));
    {
        struct fat32_dirent* dot = (struct fat32_dirent*)sector;
        struct fat32_dirent* dotdot = (struct fat32_dirent*)(sector + sizeof(struct fat32_dirent));

        memset(dot->name, ' ', 11);
        dot->name[0] = '.';
        dot->attr = FAT32_ATTR_DIRECTORY;
        dirent_set_first_cluster(dot, 3);

        memset(dotdot->name, ' ', 11);
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        dotdot->attr = FAT32_ATTR_DIRECTORY;
        dirent_set_first_cluster(dotdot, 2);
    }
    if (fat32_write_sector(cluster_to_sector(3), sector) != 0) return -1;

    return 0;
}

int fat32_mount() {
    uint8_t sector[512];

    if (fat32_read_sector(0, sector) != 0) return -1;
    memcpy(&bpb, sector, sizeof(bpb));

    if (bpb.signature != 0xAA55 || bpb.bytes_per_sector != 512 || bpb.sectors_per_cluster == 0 || bpb.fat_size_32 == 0) {
        print("[FAT32] No valid FAT32 found, formatting default 10MB volume...\n");
        if (fat32_create_default_layout() != 0) {
            print("[FAT32] Format failed\n");
            return -1;
        }
        if (fat32_read_sector(0, sector) != 0) return -1;
        memcpy(&bpb, sector, sizeof(bpb));
    }

    bytes_per_sector = bpb.bytes_per_sector;
    sectors_per_cluster = bpb.sectors_per_cluster;
    num_fats = bpb.num_fats;
    sectors_per_fat = bpb.fat_size_32;
    total_sectors = bpb.total_sectors_32 ? bpb.total_sectors_32 : bpb.total_sectors_16;
    root_cluster = bpb.root_cluster;

    fat_start_sector = bpb.hidden_sectors + bpb.reserved_sector_count;
    data_start_sector = fat_start_sector + num_fats * sectors_per_fat;

    if (sectors_per_cluster == 0 || total_sectors <= data_start_sector || root_cluster < 2) {
        print("[FAT32] Invalid layout\n");
        return -1;
    }

    cluster_count = (total_sectors - data_start_sector) / sectors_per_cluster;
    if (cluster_count < 1) {
        print("[FAT32] Empty data region\n");
        return -1;
    }

    if (next_alloc_hint < 2) next_alloc_hint = 2;

    print("[FS] FAT32 filesystem mounted\n");
    return 0;
}

int fat32_find_path(const char* path, struct fat32_file_info* info) {
    struct fat32_dirent entry;
    uint32_t sector;
    uint16_t offset;

    if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        if (info) {
            info->first_cluster = root_cluster;
            info->size = 0;
            info->dir_cluster = 0;
            info->dir_sector = 0;
            info->dir_offset = 0;
            info->attrs = FAT32_ATTR_DIRECTORY;
        }
        return 0;
    }

    if (fat32_resolve_path(path, 0, 0, 0, 0, &entry, &sector, &offset) != 0) return -1;

    if (info) {
        info->first_cluster = dirent_first_cluster(&entry);
        info->size = entry.file_size;
        info->dir_cluster = 0;
        info->dir_sector = sector;
        info->dir_offset = offset;
        info->attrs = entry.attr;
    }

    return 0;
}

int fat32_read_file(const char* path, uint32_t offset, char* buffer, uint32_t size) {
    struct fat32_file_info info;
    uint32_t cluster_bytes;
    uint32_t skip_clusters;
    uint32_t cluster_offset;
    uint32_t c;
    uint8_t sector_buf[512];
    uint32_t copied = 0;

    if (fat32_find_path(path, &info) != 0) return -1;
    if (info.attrs & FAT32_ATTR_DIRECTORY) return -1;
    if (info.first_cluster < 2 || info.size == 0) return 0;

    if (offset >= info.size) return 0;
    if (offset + size > info.size) size = info.size - offset;

    cluster_bytes = sectors_per_cluster * bytes_per_sector;
    skip_clusters = offset / cluster_bytes;
    cluster_offset = offset % cluster_bytes;
    c = info.first_cluster;

    for (uint32_t i = 0; i < skip_clusters; i++) {
        uint32_t next;
        if (fat32_read_fat(c, &next) != 0) return -1;
        if (fat32_is_eoc(next) || next == 0) return 0;
        c = next;
    }

    while (copied < size && c >= 2) {
        uint32_t base_sector = cluster_to_sector(c);
        uint32_t pos = 0;

        for (uint32_t s = 0; s < sectors_per_cluster && copied < size; s++) {
            uint32_t local_off = 0;
            uint32_t to_copy = bytes_per_sector;

            if (fat32_read_sector(base_sector + s, sector_buf) != 0) return -1;

            if (pos + bytes_per_sector <= cluster_offset) {
                pos += bytes_per_sector;
                continue;
            }

            if (cluster_offset > pos) {
                local_off = cluster_offset - pos;
                to_copy -= local_off;
            }

            if (to_copy > size - copied) to_copy = size - copied;

            memcpy(buffer + copied, sector_buf + local_off, to_copy);
            copied += to_copy;
            pos += bytes_per_sector;
        }

        cluster_offset = 0;
        {
            uint32_t next;
            if (fat32_read_fat(c, &next) != 0) return -1;
            if (fat32_is_eoc(next) || next == 0) break;
            c = next;
        }
    }

    return (int)copied;
}

int fat32_write_file(const char* path, const char* data, uint32_t size, uint32_t* first_cluster_out) {
    uint32_t parent_cluster;
    char leaf[64];
    uint8_t short_name[11];
    struct fat32_dirent existing;
    uint32_t existing_sector;
    uint16_t existing_offset;
    int exists = 0;
    struct fat32_dirent new_entry;
    uint32_t start_cluster = 0;
    uint32_t prev_cluster = 0;
    uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;
    uint32_t remaining = size;
    const char* p = data;

    if (fat32_resolve_path(path, 1, 1, &parent_cluster, leaf, 0, 0, 0) != 0) return -1;
    name_to_short83(leaf, short_name);

    if (fat32_find_entry_in_dir(parent_cluster, short_name, &existing, &existing_sector, &existing_offset) == 0) {
        exists = 1;
        if (existing.attr & FAT32_ATTR_DIRECTORY) return -1;
        if (dirent_first_cluster(&existing) >= 2) {
            if (fat32_free_chain(dirent_first_cluster(&existing)) != 0) return -1;
        }
    }

    while (remaining > 0) {
        uint32_t c;
        uint32_t chunk = remaining;
        uint32_t written = 0;

        if (chunk > cluster_bytes) chunk = cluster_bytes;

        if (fat32_allocate_cluster(&c) != 0) {
            if (start_cluster >= 2) fat32_free_chain(start_cluster);
            return -1;
        }

        if (start_cluster == 0) start_cluster = c;
        if (prev_cluster != 0) {
            if (fat32_write_fat(prev_cluster, c) != 0) {
                fat32_free_chain(start_cluster);
                return -1;
            }
        }

        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            uint8_t sector[512];
            uint32_t copy = bytes_per_sector;

            memset(sector, 0, sizeof(sector));
            if (written < chunk) {
                if (copy > chunk - written) copy = chunk - written;
                memcpy(sector, p + written, copy);
                written += copy;
            }

            if (fat32_write_sector(cluster_to_sector(c) + s, sector) != 0) {
                fat32_free_chain(start_cluster);
                return -1;
            }
        }

        prev_cluster = c;
        p += chunk;
        remaining -= chunk;
    }

    if (prev_cluster != 0) {
        if (fat32_write_fat(prev_cluster, 0x0FFFFFFF) != 0) {
            fat32_free_chain(start_cluster);
            return -1;
        }
    }

    memset(&new_entry, 0, sizeof(new_entry));
    memcpy(new_entry.name, short_name, 11);
    new_entry.attr = FAT32_ATTR_ARCHIVE;
    dirent_set_first_cluster(&new_entry, start_cluster);
    new_entry.file_size = size;

    if (exists) {
        if (fat32_write_dirent(existing_sector, existing_offset, &new_entry) != 0) return -1;
    } else {
        uint32_t slot_sector;
        uint16_t slot_offset;
        if (fat32_find_free_dir_slot(parent_cluster, &slot_sector, &slot_offset) != 0) return -1;
        if (fat32_write_dirent(slot_sector, slot_offset, &new_entry) != 0) return -1;
    }

    if (first_cluster_out) *first_cluster_out = start_cluster;

    return 0;
}

int fat32_delete(const char* path) {
    struct fat32_dirent entry;
    uint32_t sector;
    uint16_t offset;
    uint8_t sector_buf[512];

    if (fat32_resolve_path(path, 0, 0, 0, 0, &entry, &sector, &offset) != 0) return -1;
    if (entry.attr & FAT32_ATTR_DIRECTORY) return -1;

    if (dirent_first_cluster(&entry) >= 2) {
        if (fat32_free_chain(dirent_first_cluster(&entry)) != 0) return -1;
    }

    if (fat32_read_sector(sector, sector_buf) != 0) return -1;
    sector_buf[offset] = 0xE5;
    return fat32_write_sector(sector, sector_buf);
}

int fat32_list_path(const char* path) {
    uint32_t dir_cluster = root_cluster;
    struct fat32_dirent entry;
    uint32_t c;
    uint8_t sector[512];

    if (path && path[0] && !(path[0] == '/' && path[1] == 0)) {
        if (fat32_resolve_path(path, 0, 0, 0, 0, &entry, 0, 0) != 0) return -1;
        if ((entry.attr & FAT32_ATTR_DIRECTORY) == 0) return -1;
        dir_cluster = dirent_first_cluster(&entry);
    }

    c = dir_cluster;
    while (c >= 2 && c <= cluster_count + 1) {
        uint32_t base = cluster_to_sector(c);

        for (uint32_t s = 0; s < sectors_per_cluster; s++) {
            if (fat32_read_sector(base + s, sector) != 0) return -1;

            for (uint32_t off = 0; off < bytes_per_sector; off += sizeof(struct fat32_dirent)) {
                char name[20];
                struct fat32_dirent* e = (struct fat32_dirent*)(sector + off);

                if (e->name[0] == 0x00) return 0;
                if (e->name[0] == 0xE5 || e->attr == FAT32_ATTR_LFN) continue;
                if (e->name[0] == '.') continue;

                short83_to_name(e->name, name, sizeof(name));
                print(name);
                if (e->attr & FAT32_ATTR_DIRECTORY) print("/\n");
                else print("\n");
            }
        }

        {
            uint32_t next;
            if (fat32_read_fat(c, &next) != 0) return -1;
            if (fat32_is_eoc(next) || next == 0) break;
            c = next;
        }
    }

    return 0;
}
