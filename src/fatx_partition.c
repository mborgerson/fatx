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

#include "fatx_internal.h"

/*
 * Check partition signature.
 */
int fatx_check_partition_signature(struct fatx_fs *fs)
{
    uint32_t signature;
    size_t   read;

    if (fseek(fs->device, fs->partition_offset+FATX_SIGNATURE_OFFSET, SEEK_SET))
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

    return 0;
}

/*
 * Process the partition superblock.
 */
int fatx_process_superblock(struct fatx_fs *fs)
{
    struct fatx_superblock superblock;
    size_t read;

    if (fseek(fs->device, fs->partition_offset+FATX_SUPERBLOCK_OFFSET, SEEK_SET))
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

    if (superblock.cluster_size != 32)
    {
        fatx_error(fs, "expected cluster size to be 32, got %d\n",
                   superblock.cluster_size);
        return -1;
    }

    if (superblock.num_fat_copies != 1)
    {
        fatx_error(fs, "expected number of FAT copies to be 1, got %d\n",
                   superblock.num_fat_copies);
        return -1;
    }

    fs->volume_id    = superblock.volume_id;
    fs->cluster_size = superblock.cluster_size;

    return 0;
}