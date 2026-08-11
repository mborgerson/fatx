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

#ifndef _WIN32
#include <sys/time.h>
#endif

#include "fatx_internal.h"

/*
 * Check partition signature.
 *
 * When the variant is FATX_VARIANT_AUTO this also determines it: the raw
 * signature word identifies the on-disk byte order, so a successful check
 * leaves fs->variant resolved to a concrete variant.
 */
int fatx_check_partition_signature(struct fatx_fs *fs)
{
    uint32_t signature;

    if (fatx_dev_seek(fs, fs->partition_offset))
    {
        fatx_error(fs, "failed to seek to signature\n");
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_read(fs, &signature, sizeof(uint32_t), 1) != 1)
    {
        fatx_error(fs, "failed to read signature from device\n");
        return FATX_STATUS_ERROR;
    }

    if (fs->variant == FATX_VARIANT_AUTO)
    {
        enum fatx_variant detected;

        detected = fatx_variant_from_raw_signature(signature, FATX_SIGNATURE);
        if (detected == FATX_VARIANT_AUTO)
        {
            fatx_error(fs, "invalid signature (got %.8x, expected %.8x in either byte order)\n",
                       signature, FATX_SIGNATURE);
            return FATX_STATUS_ERROR;
        }

        fs->variant = detected;
        fatx_info(fs, "detected %s filesystem\n", fatx_variant_name(fs->variant));
        return FATX_STATUS_SUCCESS;
    }

    if (fatx_from_disk_u32(fs, signature) != FATX_SIGNATURE)
    {
        fatx_error(fs, "invalid signature for %s filesystem\n", fatx_variant_name(fs->variant));
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Initialize the partition with a new superblock.
 */
int fatx_init_superblock(struct fatx_fs *fs, size_t sectors_per_cluster)
{
#ifndef _WIN32
    struct timeval time;
#endif

    /* Initialize device with existing FATX superblock. */
    if (sectors_per_cluster == FATX_READ_FROM_SUPERBLOCK)
    {
        if (fatx_check_partition_signature(fs) || fatx_read_superblock(fs))
        {
            return FATX_STATUS_ERROR;
        }
    }

    /* Initialize device with a new FATX superblock. */
    else
    {
        /*
         * There is no signature to probe when formatting. Formatting a 360
         * partition is not supported yet, so an unresolved variant means the
         * original Xbox.
         */
        if (fs->variant == FATX_VARIANT_AUTO)
        {
            fs->variant = FATX_VARIANT_XBOX;
        }

        if (fs->variant != FATX_VARIANT_XBOX)
        {
            fatx_error(fs, "formatting a %s filesystem is not supported\n",
                       fatx_variant_name(fs->variant));
            return FATX_STATUS_ERROR;
        }

#ifdef _WIN32
        fs->volume_id = 12345678;
#else
        gettimeofday(&time, NULL);
        fs->volume_id = time.tv_usec;
#endif
        fs->root_cluster = 1;
        fs->sectors_per_cluster = sectors_per_cluster;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Process the partition superblock.
 */
int fatx_read_superblock(struct fatx_fs *fs)
{
    struct fatx_superblock superblock;

    if (fatx_dev_seek(fs, fs->partition_offset))
    {
        fatx_error(fs, "failed to seek to superblock\n");
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_read(fs, &superblock, sizeof(struct fatx_superblock), 1) != 1)
    {
        fatx_error(fs, "failed to read superblock\n");
        return FATX_STATUS_ERROR;
    }

    if (fatx_from_disk_u32(fs, superblock.signature) != FATX_SIGNATURE)
    {
        fatx_error(fs, "invalid signature\n");
        return FATX_STATUS_ERROR;
    }

    fs->volume_id = fatx_from_disk_u32(fs, superblock.volume_id);
    fs->sectors_per_cluster = fatx_from_disk_u32(fs, superblock.sectors_per_cluster);
    fs->root_cluster = fatx_from_disk_u32(fs, superblock.root_cluster);

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

    superblock.signature = fatx_to_disk_u32(fs, FATX_SIGNATURE);
    superblock.sectors_per_cluster = fatx_to_disk_u32(fs, fs->sectors_per_cluster);
    superblock.volume_id = fatx_to_disk_u32(fs, fs->volume_id);
    superblock.root_cluster = fatx_to_disk_u32(fs, fs->root_cluster);
    superblock.unknown1 = fatx_to_disk_u16(fs, 0);

    if (fatx_dev_write(fs, &superblock, sizeof(struct fatx_superblock), 1) != 1)
    {
        fatx_error(fs, "failed to write superblock\n");
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}
