/*
 * FATX Filesystem Library
 *
 * Copyright (C) 2015  Matt Borgerson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef FATX_INTERNAL_H
#define FATX_INTERNAL_H

#include "fatx.h"

/* Offset of the filesystem signature, in bytes. */
#define FATX_SIGNATURE_OFFSET        0

/* FATX filesystem signature ('FATX') */
#define FATX_SIGNATURE               0x58544146

/* Offset of the superblock, in bytes. */
#define FATX_SUPERBLOCK_OFFSET       4

/* Size of the superblock, in bytes. */
#define FATX_SUPERBLOCK_SIZE         4096

/* Defines the type of FAT in use. */
#define FATX_FAT_TYPE_16             1
#define FATX_FAT_TYPE_32             2

/* Offset of the File Allocation Table (FAT). */
#define FATX_FAT_OFFSET              4096

/* Number of reserved entries in the FAT. */
#define FATX_FAT_RESERVED_ENTRIES    2

/* Mask to be applied when reading FAT entry values. */
#define FATX_FAT16_ENTRY_MASK        0x0000ffff
#define FATX_FAT32_ENTRY_MASK        0x0fffffff

/* Default number of bytes per sector. */
#define FATX_BYTES_PER_SECTOR        512

/* Markers used in the filename_size field of the directory entry. */
#define FATX_DELETED_FILE_MARKER     0xe5
#define FATX_END_OF_DIR_MARKER       0xff

/* Mask to be applied when reading directory entry attributes. */
#define FATX_ATTR_MASK               0x0f

/* The epoch. */
#define FATX_EPOCH                   2000

/* Macros to unpack date/time values. */
#define FATX_TIME_TO_HOUR(t)         (((t)>>11)&0xf)
#define FATX_TIME_TO_MINUTE(t)       (((t)>>5)&0x1f)
#define FATX_TIME_TO_SECOND(t)       (((t)&0x1f)*2)
#define FATX_DATE_TO_YEAR(d)         ((((d)>>9)&0x7f)+FATX_EPOCH)
#define FATX_DATE_TO_MONTH(d)        (((d)>>5)&0xf)
#define FATX_DATE_TO_DAY(d)          ((d)&0x1f)

/* Default seperator. */
#define FATX_PATH_SEPERATOR          '/'

/* FAT entry types (not the actual value of the entry). */
#define FATX_CLUSTER_AVAILABLE       0
#define FATX_CLUSTER_RESERVED        1
#define FATX_CLUSTER_DATA            2
#define FATX_CLUSTER_BAD             3
#define FATX_CLUSTER_END             4
#define FATX_CLUSTER_INVALID         5

/* Helpful macros. */
#define MIN(a,b) ( ( (a) <= (b) ) ? (a) : (b) )

/*
 * The superblock, as it appears on disk.
 */
#pragma pack(1)
struct fatx_superblock {
    uint32_t volume_id;
    uint32_t cluster_size;
    uint16_t num_fat_copies;
    uint32_t unknown1;
};
#pragma pack()

/*
 * The directory entry as it appears on disk.
 */
#pragma pack(1)
struct fatx_raw_directory_entry {
    uint8_t  filename_len;
    uint8_t  attributes;
    char     filename[FATX_MAX_FILENAME_LEN];
    uint32_t first_cluster;
    uint32_t file_size;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t created_time;
    uint16_t created_date;
    uint16_t accessed_time;
    uint16_t accessed_date;
};
#pragma pack()

typedef uint32_t fatx_fat_entry;

/* Partition Functions */
int fatx_check_partition_signature(struct fatx_fs *fs);
int fatx_process_superblock(struct fatx_fs *fs);

/* Device Functions */
int fatx_dev_seek(struct fatx_fs *fs, off_t offset);
int fatx_dev_seek_cluster(struct fatx_fs *fs, size_t cluster, off_t offset);
size_t fatx_dev_read(struct fatx_fs *fs, void *buf, size_t size, size_t items);

/* FAT Functions */
int fatx_read_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry *entry);
int fatx_cluster_number_to_byte_offset(struct fatx_fs *fs, size_t cluster, size_t *offset);
int fatx_get_fat_entry_for_cluster(struct fatx_fs *fs, size_t cluster, fatx_fat_entry *fat_entry);
int fatx_get_fat_entry_type(struct fatx_fs *fs, fatx_fat_entry entry);
int fatx_get_next_cluster(struct fatx_fs *fs, size_t *cluster);

/* Directory Functions */
int fatx_read_dir_ll(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_raw_directory_entry *dirent, struct fatx_raw_directory_entry **nextdirent);
int fatx_dirent_to_attr(struct fatx_fs *fs, struct fatx_raw_directory_entry *entry, struct fatx_attr *attr);

/* Misc Functions */
int fatx_get_path_component(char const *path, size_t component, char const **start, size_t *len);
int fatx_unpack_date(uint16_t in, struct fatx_ts *out);
int fatx_unpack_time(uint16_t in, struct fatx_ts *out);

#endif