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

#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif /* __STDC_VERSION__ */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

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

struct fatx_fs {
    char const *device_path;
    FILE       *device;
    size_t      partition_offset;
    size_t      partition_size;
    uint32_t    volume_id;
    uint32_t    num_sectors;
    uint32_t    num_clusters;
    uint32_t    cluster_size;
    uint8_t     fat_type;
    size_t      fat_offset;
    size_t      fat_size;
    size_t      root_offset;
    size_t      root_size;
    size_t      cluster_offset;
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

int fatx_open_device(struct fatx_fs *fs, char const *path, size_t offset, size_t size);
int fatx_close_device(struct fatx_fs *fs);
int fatx_open_dir(struct fatx_fs *fs, char const *path, struct fatx_dir *dir);
int fatx_read_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr, struct fatx_dirent **result);
int fatx_next_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir);
int fatx_close_dir(struct fatx_fs *fs, struct fatx_dir *dir);
int fatx_get_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
int fatx_set_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
int fatx_read(struct fatx_fs *fs, char const *path, off_t offset, size_t size, void *buf);
int fatx_unlink(struct fatx_fs *fs, char const *path);
int fatx_mkdir(struct fatx_fs *fs, char const *path);
int fatx_mknod(struct fatx_fs *fs, char const *path);

#endif
