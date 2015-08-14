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
int fatx_open_device(struct fatx_fs *fs, char const *path, size_t offset, size_t size)
{
    int retval;

    retval = 0;

    /* Sanity check partition offset and size. */
    if (offset % FATX_BYTES_PER_SECTOR)
    {
        fatx_error(fs, "specified partition offset does not reside on sector boundary (%d bytes)\n", FATX_BYTES_PER_SECTOR);
        return -1;
    }

    if (size % FATX_BYTES_PER_SECTOR)
    {
        fatx_error(fs, "specified partition size does not reside on sector boundary (%d bytes)\n", FATX_BYTES_PER_SECTOR);
        return -1;
    }

    fs->device_path      = path;
    fs->partition_offset = offset;
    fs->partition_size   = size;

    fs->device = fopen(fs->device_path, "rb");
    if (!fs->device)
    {
        fatx_error(fs, "failed to open %s for reading\n", path);
        return -1;
    }

    /* Check signature. */
    if (fatx_check_partition_signature(fs))
    {
        retval = -1;
        goto cleanup;
    }

    /* Process superblock. */
    if (fatx_process_superblock(fs))
    {
        retval = -1;
        goto cleanup;
    }

    fs->num_sectors       = fs->partition_size / FATX_BYTES_PER_SECTOR;
    fs->num_clusters      = fs->num_sectors / fs->cluster_size;
    fs->bytes_per_cluster = fs->cluster_size * FATX_BYTES_PER_SECTOR;
    fs->fat_offset        = fs->partition_offset+FATX_FAT_OFFSET;

    if (fs->num_clusters < 65525)
    {
        fs->fat_type = FATX_FAT_TYPE_16;
        fs->fat_size = fs->num_clusters*2;
    }
    else
    {
        fs->fat_type = FATX_FAT_TYPE_32;
        fs->fat_size = fs->num_clusters*4;
    }

    /* Round FAT size up to nearest 4k boundary. */
    if (fs->fat_size % 4096)
    {
        fs->fat_size += 4096 - fs->fat_size % 4096;
    }

    /* Calculate start and size of the root directory. */
    fs->root_offset = fs->fat_offset + fs->fat_size;
    fs->root_size   = 1 * fs->bytes_per_cluster;

    /* Calculate start of data clusters. */
    fs->cluster_offset = fs->root_offset + fs->root_size;

    fatx_info(fs, "Partition Info:\n");
    fatx_info(fs, "  Device Path:         %s\n",          fs->device_path);
    fatx_info(fs, "  Partition Offset:    0x%zx bytes\n", fs->partition_offset);
    fatx_info(fs, "  Partition Size:      0x%zx bytes\n", fs->partition_size);
    fatx_info(fs, "  Volume Id:           %.8x\n",        fs->volume_id);
    fatx_info(fs, "  Bytes per Sector:    %d\n",          FATX_BYTES_PER_SECTOR);
    fatx_info(fs, "  # of Sectors:        %d\n",          fs->num_sectors);
    fatx_info(fs, "  Sectors per Cluster: %d\n",          fs->cluster_size);
    fatx_info(fs, "  # of Clusters:       %d\n",          fs->num_clusters);
    fatx_info(fs, "  FAT Offset:          0x%zx bytes\n", fs->fat_offset);
    fatx_info(fs, "  FAT Size:            0x%zx bytes\n", fs->fat_size);
    fatx_info(fs, "  FAT Type:            %s\n",          fs->fat_type == FATX_FAT_TYPE_16 ? "16" : "32");
    fatx_info(fs, "  Root Offset:         0x%zx bytes\n", fs->root_offset);
    fatx_info(fs, "  Root Size:           0x%zx bytes\n", fs->root_size);
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



