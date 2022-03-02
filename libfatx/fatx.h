/*
 * FATX Filesystem Library
 *
 * Copyright (C) 2015  Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FATX_H
#define FATX_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "fatx_log.h"

#define FATX_MAX_FILENAME_LEN        42
#define FATX_ATTR_READ_ONLY          (1<<0)
#define FATX_ATTR_SYSTEM             (1<<1)
#define FATX_ATTR_HIDDEN             (1<<2)
#define FATX_ATTR_VOLUME             (1<<3)
#define FATX_ATTR_DIRECTORY          (1<<4)

#define FATX_STATUS_FILE_NOT_FOUND  -2
#define FATX_STATUS_ERROR           -1
#define FATX_STATUS_SUCCESS          0
#define FATX_STATUS_FILE_DELETED     1
#define FATX_STATUS_END_OF_DIR       2

#define FATX_RETAIL_CLUSTER_SIZE     (16 * 1024)
#define FATX_RETAIL_PARTITION_COUNT  5

/*
 * This define should always be passed to fatx_open_device(...) as the
 * sectors_per_cluster argument when opening existing FATX filesystems.
 *
 * It is a special value designed to tell fatx_open_device(...) that it
 * must read the superblock of the FATX partition being opened to determine
 * how many sectors_per_cluster the partition was formatted with.
 *
 * The cases where this define is NOT passed to fatx_open_device(...) is
 * when formatting a new disk. In this case, the caller is expected to pass
 * pass a valid non-zero sectors_per_cluster to format the partition with.
 */
#define FATX_READ_FROM_SUPERBLOCK    0

struct fatx_fs {
    char const *device_path;
    FILE       *device;
    size_t      sector_size;
    uint64_t    partition_offset;
    uint64_t    partition_size;
    uint32_t    volume_id;
    uint64_t    num_sectors;
    uint32_t    num_clusters;
    uint32_t    sectors_per_cluster;
    uint8_t     fat_type;
    uint64_t    fat_offset;
    size_t      fat_size;
    size_t      root_cluster;
    uint64_t    cluster_offset;
    size_t      bytes_per_cluster;
    FILE       *log_handle;
    int         log_level;
};

struct fatx_dir {
    size_t cluster;
    size_t entry;
};

struct fatx_ts {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

struct fatx_dirent {
    char filename[FATX_MAX_FILENAME_LEN+1];
};

struct fatx_attr {
    char           filename[FATX_MAX_FILENAME_LEN+1];
    uint8_t        attributes;
    size_t         first_cluster;
    size_t         file_size;
    struct fatx_ts modified;
    struct fatx_ts created;
    struct fatx_ts accessed;
};

/*
 * Xbox Harddisk Partition Map
 */

struct fatx_partition_map_entry {
    char     letter;
    uint64_t offset;
    uint64_t size;
};

enum fatx_format {
    FATX_FORMAT_INVALID,
    FATX_FORMAT_RETAIL,
    FATX_FORMAT_F_TAKES_ALL
};

/* FATX Functions */
int fatx_open_device(struct fatx_fs *fs, char const *path, uint64_t offset, uint64_t size, size_t sector_size, size_t sectors_per_cluster);
int fatx_close_device(struct fatx_fs *fs);
int fatx_open_dir(struct fatx_fs *fs, char const *path, struct fatx_dir *dir);
int fatx_read_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr, struct fatx_dirent **result);
int fatx_write_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr);
int fatx_next_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir);
int fatx_alloc_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir);
int fatx_close_dir(struct fatx_fs *fs, struct fatx_dir *dir);
int fatx_get_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
int fatx_set_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
int fatx_utime(struct fatx_fs *fs, char const *path, struct fatx_ts ts[2]);
int fatx_read(struct fatx_fs *fs, char const *path, off_t offset, size_t size, void *buf);
int fatx_write(struct fatx_fs *fs, char const *path, off_t offset, size_t size, const void *buf);
int fatx_create_dirent(struct fatx_fs *fs, char const *path, struct fatx_dir *dir, uint8_t attributes);
int fatx_unlink(struct fatx_fs *fs, char const *path);
int fatx_mkdir(struct fatx_fs *fs, char const *path);
int fatx_rmdir(struct fatx_fs *fs, char const *path);
int fatx_mknod(struct fatx_fs *fs, char const *path);
int fatx_truncate(struct fatx_fs *fs, char const *path, off_t offset);
int fatx_rename(struct fatx_fs *fs, char const *from, char const *to);
void fatx_time_t_to_fatx_ts(const time_t in, struct fatx_ts *out);
time_t fatx_ts_to_time_t(struct fatx_ts *in);

/* Disk Functions */
int fatx_disk_size(char const *path, uint64_t *size);
int fatx_disk_size_remaining(char const *path, uint64_t offset, uint64_t *size);
int fatx_disk_format(struct fatx_fs *fs, char const *path, size_t sector_size, enum fatx_format format_type, size_t sectors_per_cluster);
int fatx_disk_format_partition(struct fatx_fs *fs, char const *path, uint64_t offset, uint64_t size, size_t sector_size, size_t sectors_per_cluster);
int fatx_drive_to_offset_size(char drive_letter, uint64_t *offset, uint64_t *size);
int fatx_disk_write_refurb_info(char const *path, uint32_t number_of_boots, uint64_t first_power_on);

#ifdef __cplusplus
}
#endif

#endif
