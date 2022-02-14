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

#include <sys/time.h>
#include "fatx_internal.h"

/*
 * Check partition signature.
 */
int fatx_check_partition_signature(struct fatx_fs *fs)
{
    uint32_t signature;
    size_t   read;

    if (fatx_dev_seek(fs, fs->partition_offset))
    {
        fatx_error(fs, "failed to seek to signature\n");
        return -1;
    }

    read = fatx_dev_read(fs, &signature, sizeof(uint32_t), 1);

    if (read != 1)
    {
        fatx_error(fs, "failed to read signature from device\n");
        return -1;
    }

    if (signature != FATX_SIGNATURE)
    {
        fatx_error(fs, "invalid signature\n");
        return -1;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Initialize the partition with a new superblock.
 */
int fatx_init_superblock(struct fatx_fs *fs, size_t sectors_per_cluster)
{
    struct timeval time;

    gettimeofday(&time, NULL);
    fs->volume_id = time.tv_usec;
    fs->root_cluster = 1;
    fs->sectors_per_cluster = sectors_per_cluster;

    return FATX_STATUS_SUCCESS;
}

/*
 * Process the partition superblock.
 */
int fatx_read_superblock(struct fatx_fs *fs)
{
    struct fatx_superblock superblock;
    size_t read;

    if (fatx_dev_seek(fs, fs->partition_offset))
    {
        fatx_error(fs, "failed to seek to superblock\n");
        return -1;
    }

    read = fatx_dev_read(fs, &superblock, sizeof(struct fatx_superblock), 1);
    if (read != 1)
    {
        fatx_error(fs, "failed to read superblock\n");
        return -1;
    }

    if (superblock.signature != FATX_SIGNATURE)
    {
        fatx_error(fs, "invalid signature\n");
        return -1;
    }

    fs->volume_id = superblock.volume_id;
    fs->sectors_per_cluster = superblock.sectors_per_cluster;
    fs->root_cluster = superblock.root_cluster;

    return FATX_STATUS_SUCCESS;
}

/*
 * Write the partition superblock.
 */
int fatx_write_superblock(struct fatx_fs *fs)
{
    struct fatx_superblock superblock;

    if (fatx_dev_seek(fs, fs->partition_offset))
    {
        fatx_error(fs, "failed to seek to superblock\n");
        return FATX_STATUS_ERROR;
    }

    memset(&superblock, 0xFF, sizeof(struct fatx_superblock));

    superblock.signature = FATX_SIGNATURE;
    superblock.sectors_per_cluster = fs->sectors_per_cluster;
    superblock.volume_id = fs->volume_id;
    superblock.root_cluster = fs->root_cluster;
    superblock.unknown1 = 0;

    if (fatx_dev_write(fs, &superblock, sizeof(struct fatx_superblock), 1) != 1)
    {
        fatx_error(fs, "failed to write superblock\n");
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}
