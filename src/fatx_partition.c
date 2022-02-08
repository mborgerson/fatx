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

#include "fatx_internal.h"

/*
 * Check partition signature.
 */
int fatx_check_partition_signature(struct fatx_fs *fs)
{
    uint32_t signature;
    size_t   read;

    if (fatx_dev_seek(fs, fs->partition_offset+FATX_SIGNATURE_OFFSET))
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
 * Process the partition superblock.
 */
int fatx_process_superblock(struct fatx_fs *fs)
{
    struct fatx_superblock superblock;
    size_t read;

    if (fatx_dev_seek(fs, fs->partition_offset+FATX_SUPERBLOCK_OFFSET))
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

    fs->volume_id = superblock.volume_id;
    fs->sectors_per_cluster = superblock.sectors_per_cluster;
    fs->root_cluster = superblock.root_cluster;

    return FATX_STATUS_SUCCESS;
}
