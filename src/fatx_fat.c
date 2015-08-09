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
 * Read from the FAT.
 */
int fatx_read_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry *entry)
{
    size_t fat_entry_offset, fat_entry_size;

    fatx_debug(fs, "fatx_read_fat - index = %zd\n", index);

    if (index >= fs->num_clusters)
    {
        fatx_error(fs, "index number out of bounds\n");
        return -1;
    }

    fat_entry_size   = fs->fat_type == FATX_FAT_TYPE_16 ? 2 : 4;
    fat_entry_offset = fs->fat_offset + index * fat_entry_size;
    *entry           = 0;

    if (fatx_dev_seek(fs, fat_entry_offset))
    {
        fatx_error(fs, "failed to seek to index %zd in FAT (offset 0x%zx)\n",
                   index, fat_entry_offset);
        return -1;
    }

    if (fatx_dev_read(fs, entry, fat_entry_size, 1) != 1)
    {
        fatx_error(fs, "failed to read fat index %zd\n", index);
        return -1;
    }

    return 0;
}

/*
 * Get the FAT entry for a given cluster.
 */
int fatx_get_fat_entry_for_cluster(struct fatx_fs *fs, size_t cluster, fatx_fat_entry *fat_entry)
{
    int status;

    fatx_debug(fs, "fatx_get_fat_entry_for_cluster - cluster = %zd\n", cluster);

    if (cluster < 2 || cluster >= fs->num_clusters)
    {
        fatx_error(fs, "cluster number out of range\n");
        return -1;
    }

    status = fatx_read_fat(fs, cluster, fat_entry);
    if (status)
    {
        /* An error occured. */
        return status;
    }

    return 0;
}

/*
 * Get the type of entry in the FAT.
 */
int fatx_get_fat_entry_type(struct fatx_fs *fs, fatx_fat_entry entry)
{
    if (fs->fat_type == FATX_FAT_TYPE_16)
    {
        entry &= FATX_FAT16_ENTRY_MASK;

        if (entry == 0)
            return FATX_CLUSTER_AVAILABLE;

        if (entry == 1)
            return FATX_CLUSTER_RESERVED;

        if (entry == 0xfff7)
            return FATX_CLUSTER_BAD;

        if (entry >= 0x0002 && entry <= 0xfff6)
            return FATX_CLUSTER_DATA;

        if (entry >= 0xfff8 && entry <= 0xffff)
            return FATX_CLUSTER_END;
    }
    else
    {
        entry &= FATX_FAT32_ENTRY_MASK;

        if (entry == 0)
            return FATX_CLUSTER_AVAILABLE;

        if (entry == 1)
            return FATX_CLUSTER_RESERVED;

        if (entry == 0x0ffffff7)
            return FATX_CLUSTER_BAD;

        if (entry >= 0x00000002 && entry <= 0x0ffffff6)
            return FATX_CLUSTER_DATA;

        if (entry >= 0x0ffffff8 && entry <= 0x0fffffff)
            return FATX_CLUSTER_END;
    }

    return FATX_CLUSTER_INVALID;
}

/*
 * Get the absolute byte address of the start of the cluster.
 */
int fatx_cluster_number_to_byte_offset(struct fatx_fs *fs, size_t cluster, size_t *offset)
{
    fatx_debug(fs, "fatx_cluster_number_to_byte_offset - cluster = %zd\n", cluster);

    if (cluster < 2 || cluster >= fs->num_clusters)
    {
        fatx_error(fs, "cluster number out of range %zd\n", cluster);
        return -1;
    }

    /* Clusters begin at index 2 */
    cluster -= 2;

    *offset  = fs->cluster_offset;
    *offset += cluster * fs->bytes_per_cluster;

    return 0;
}

/*
 * Get the next data cluster.
 */
int fatx_get_next_cluster(struct fatx_fs *fs, size_t *cluster)
{
    int status;
    fatx_fat_entry fat_entry;

    status = fatx_read_fat(fs, *cluster, &fat_entry);
    if (status)
    {
        fatx_error(fs, "read_fat returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
        return status;
    }

    status = fatx_get_fat_entry_type(fs, fat_entry);
    if (status == FATX_CLUSTER_DATA)
    {
        *cluster = fat_entry;
        return 0;
    }

    return -1;
}
