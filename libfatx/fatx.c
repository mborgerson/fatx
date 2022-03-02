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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatx_internal.h"

/*
 * Open a device.
 */
int fatx_open_device(struct fatx_fs *fs, char const *path, uint64_t offset, uint64_t size, size_t sector_size, size_t sectors_per_cluster)
{
    int retval = 0;
    size_t cluster_limit = 0;

    if (sector_size != 512 && sector_size != 4096)
    {
        fatx_error(fs, "expected sector size to be 512 or 4096, got %d\n", sector_size);
        return FATX_STATUS_ERROR;
    }

    if (offset % sector_size)
    {
        fatx_error(fs, "specified partition offset (0x%zx) does not reside on sector boundary (%d bytes)\n", offset, sector_size);
        return FATX_STATUS_ERROR;
    }

    /* Compute partition size using remaining disk space and align down to nearest sector */
    if (size == -1)
    {
        if (fatx_disk_size_remaining(path, offset, &size))
        {
            fatx_error(fs, "failed to resolve partition size");
            return FATX_STATUS_ERROR;
        }
        size &= ~(sector_size - 1);
    }

    if (size % sector_size)
    {
        fatx_error(fs, "specified partition size does not reside on sector boundary (%d bytes)\n", sector_size);
        return FATX_STATUS_ERROR;
    }

    fs->device_path      = path;
    fs->sector_size      = sector_size;
    fs->partition_offset = offset;
    fs->partition_size   = size;

    fs->device = fopen(fs->device_path, "r+b");
    if (!fs->device)
    {
        fatx_error(fs, "failed to open %s for reading and writing\n", path);
        return FATX_STATUS_ERROR;
    }

    if (fatx_init_superblock(fs, sectors_per_cluster))
    {
        return FATX_STATUS_ERROR;
    }

    /* Validate that an acceptable cluster+sector combo was configured */
    switch (fs->sectors_per_cluster)
    {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
        case 64:
        case 128: // retail max
        case 256:
        case 512:
        case 1024:
            break;

        default:
            fatx_error(fs, "invalid sectors per cluster %d\n", fs->sectors_per_cluster);
            retval = FATX_STATUS_ERROR;
            goto cleanup;
    }

    fs->num_sectors       = fs->partition_size / fs->sector_size;
    fs->num_clusters      = fs->num_sectors / fs->sectors_per_cluster;
    fs->bytes_per_cluster = fs->sectors_per_cluster * fs->sector_size;
    fs->fat_offset        = fs->partition_offset + FATX_FAT_OFFSET;

    cluster_limit = fs->num_clusters + FATX_FAT_RESERVED_ENTRIES_COUNT;

    if (fs->root_cluster >= cluster_limit)
    {
        fatx_error(fs, "root cluster %d exceeds cluster limit\n", fs->root_cluster);
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    /* NOTE: this *MUST* be kept below the Cluster Reserved marker for FAT16 */
    if (fs->num_clusters < 0xfff0)
    {
        fs->fat_type = FATX_FAT_TYPE_16;
        fs->fat_size = cluster_limit*2;
    }
    else
    {
        fs->fat_type = FATX_FAT_TYPE_32;
        fs->fat_size = cluster_limit*4;
    }

    /* Round FAT size up to nearest 4k boundary. */
    if (fs->fat_size % 4096)
    {
        fs->fat_size += 4096 - fs->fat_size % 4096;
    }

    /* Calculate start of data clusters. */
    fs->cluster_offset = fs->fat_offset + fs->fat_size;

    fatx_info(fs, "Partition Info:\n");
    fatx_info(fs, "  Device Path:         %s\n",          fs->device_path);
    fatx_info(fs, "  Partition Offset:    0x%zx bytes\n", fs->partition_offset);
    fatx_info(fs, "  Partition Size:      0x%zx bytes\n", fs->partition_size);
    fatx_info(fs, "  Volume Id:           %.8x\n",        fs->volume_id);
    fatx_info(fs, "  Bytes per Sector:    %d\n",          fs->sector_size);
    fatx_info(fs, "  # of Sectors:        %llu\n",        fs->num_sectors);
    fatx_info(fs, "  Sectors per Cluster: %d\n",          fs->sectors_per_cluster);
    fatx_info(fs, "  # of Clusters:       %d\n",          fs->num_clusters);
    fatx_info(fs, "  Bytes per Cluster:   %d\n",          fs->bytes_per_cluster);
    fatx_info(fs, "  FAT Offset:          0x%zx bytes\n", fs->fat_offset);
    fatx_info(fs, "  FAT Size:            0x%zx bytes\n", fs->fat_size);
    fatx_info(fs, "  FAT Type:            %s\n",          fs->fat_type == FATX_FAT_TYPE_16 ? "16" : "32");
    fatx_info(fs, "  Root Cluster:        %d\n",          fs->root_cluster);
    fatx_info(fs, "  Cluster Offset:      0x%zx bytes\n", fs->cluster_offset);
    return FATX_STATUS_SUCCESS;

    /* Close device. */
cleanup:
    fclose(fs->device);
    return retval;
}

/*
 * Close an open device.
 */
int fatx_close_device(struct fatx_fs *fs)
{
    fclose(fs->device);
    return FATX_STATUS_SUCCESS;
}
