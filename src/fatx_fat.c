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
 * Read from the FAT.
 */
int fatx_read_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry *entry)
{
    size_t fat_entry_offset, fat_entry_size;

    fatx_debug(fs, "fatx_read_fat(index=%zd)\n", index);

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
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_read(fs, entry, fat_entry_size, 1) != 1)
    {
        fatx_error(fs, "failed to read fat index %zd\n", index);
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Write to the FAT.
 */
int fatx_write_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry entry)
{
    size_t fat_entry_offset, fat_entry_size;

    fatx_debug(fs, "fatx_write_fat(index=%zd, entry=%zx)\n", index, entry);

    if (index >= fs->num_clusters)
    {
        fatx_error(fs, "index number out of bounds\n");
        return -1;
    }

    fat_entry_size   = fs->fat_type == FATX_FAT_TYPE_16 ? 2 : 4;
    fat_entry_offset = fs->fat_offset + index * fat_entry_size;

    if (fatx_dev_seek(fs, fat_entry_offset))
    {
        fatx_error(fs, "failed to seek to index %zd in FAT (offset 0x%zx)\n",
                   index, fat_entry_offset);
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_write(fs, &entry, fat_entry_size, 1) != 1)
    {
        fatx_error(fs, "failed to write fat index %zd\n", index);
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Get the FAT entry for a given cluster.
 */
int fatx_get_fat_entry_for_cluster(struct fatx_fs *fs, size_t cluster, fatx_fat_entry *fat_entry)
{
    int status;

    fatx_debug(fs, "fatx_get_fat_entry_for_cluster(cluster=%zd)\n", cluster);

    if (cluster < FATX_FAT_RESERVED_ENTRIES || cluster >= fs->num_clusters)
    {
        fatx_error(fs, "cluster number out of range\n");
        return -1;
    }

    status = fatx_read_fat(fs, cluster, fat_entry);
    if (status != FATX_STATUS_SUCCESS)
    {
        /* An error occured. */
        return status;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Set the FAT entry for a given cluster.
 */
int fatx_set_fat_entry_for_cluster(struct fatx_fs *fs, size_t cluster, fatx_fat_entry fat_entry)
{
    int status;

    fatx_debug(fs, "fatx_set_fat_entry_for_cluster(cluster=%zd)\n", cluster);

    if (cluster < FATX_FAT_RESERVED_ENTRIES || cluster >= fs->num_clusters)
    {
        fatx_error(fs, "cluster number out of range\n");
        return -1;
    }

    status = fatx_write_fat(fs, cluster, fat_entry);
    if (status != FATX_STATUS_SUCCESS)
    {
        /* An error occured. */
        return status;
    }

    return FATX_STATUS_SUCCESS;
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

    if (cluster == FATX_ROOT_DIR_CLUSTER)
    {
        /* Root directory is desired. */
        *offset = fs->root_offset;
        return FATX_STATUS_SUCCESS;
    }

    if (cluster < FATX_FAT_RESERVED_ENTRIES || cluster >= fs->num_clusters)
    {
        fatx_error(fs, "cluster number out of range %zd\n", cluster);
        return FATX_STATUS_ERROR;
    }

    *offset = fs->cluster_offset
              + (cluster - FATX_FAT_RESERVED_ENTRIES) * fs->bytes_per_cluster;

    return FATX_STATUS_SUCCESS;
}

/*
 * Get the next data cluster.
 */
int fatx_get_next_cluster(struct fatx_fs *fs, size_t *cluster)
{
    int status;
    fatx_fat_entry fat_entry;

    status = fatx_read_fat(fs, *cluster, &fat_entry);
    if (status != FATX_STATUS_SUCCESS)
    {
        fatx_error(fs, "read_fat returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
        return status;
    }

    status = fatx_get_fat_entry_type(fs, fat_entry);
    if (status == FATX_CLUSTER_DATA)
    {
        *cluster = fat_entry;
        return FATX_STATUS_SUCCESS;
    }

    return FATX_STATUS_ERROR;
}

/*
 * Mark a given cluster as available.
 */
int fatx_mark_cluster_available(struct fatx_fs *fs, size_t cluster)
{
    fatx_debug(fs, "fatx_mark_cluster_available(cluster=%zd)\n", cluster);
    fatx_set_fat_entry_for_cluster(fs, cluster, 0);
    return FATX_STATUS_SUCCESS;
}

/*
 * Mark a given cluster as the end of a chain.
 */
int fatx_mark_cluster_end(struct fatx_fs *fs, size_t cluster)
{
    fatx_debug(fs, "fatx_mark_cluster_end(cluster=%zd)\n", cluster);
    if (fs->fat_type == FATX_FAT_TYPE_16)
    {
        fatx_set_fat_entry_for_cluster(fs, cluster, 0xffff);
    }
    else
    {
        fatx_set_fat_entry_for_cluster(fs, cluster, 0x0fffffff);
    }
    return FATX_STATUS_SUCCESS;
}

/*
 * Free a cluster chain.
 */
int fatx_free_cluster_chain(struct fatx_fs *fs, size_t first_cluster)
{
    size_t cluster, next_cluster;
    int status;

    fatx_debug(fs, "fatx_free_cluster_chain(cluster=%zd)\n", first_cluster);

    cluster = next_cluster = first_cluster;

    do
    {
        fatx_debug(fs, "marking cluster %zd as available\n", cluster);

        if (fatx_get_next_cluster(fs, &next_cluster) != FATX_STATUS_SUCCESS)
        {
            /* Reached the end of the cluster chain. */
            next_cluster = 0;
            fatx_debug(fs, "reached end of cluster chain\n");
        }

        status = fatx_mark_cluster_available(fs, cluster);
        if (status != FATX_STATUS_SUCCESS) return status;

        cluster = next_cluster;
    } while (cluster);

    return FATX_STATUS_SUCCESS;
}

/*
 * Find an available cluster.
 */
int fatx_alloc_cluster(struct fatx_fs *fs, size_t *cluster)
{
    int status;
    fatx_fat_entry fat_entry;
    size_t i;

    fatx_debug(fs, "fatx_alloc_cluster()\n");

    for (i=2; 1; i++)
    {
        status = fatx_get_fat_entry_for_cluster(fs, i, &fat_entry);
        if (status != FATX_STATUS_SUCCESS)
        {
            fatx_error(fs, "fatx_get_fat_entry_for_cluster returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
            return status;
        }

        status = fatx_get_fat_entry_type(fs, fat_entry);
        if (status == FATX_CLUSTER_AVAILABLE)
        {
            /* Found a free cluster! */
            break;
        }
    }

    status = fatx_mark_cluster_end(fs, i);
    if (status != FATX_STATUS_SUCCESS) return status;

    *cluster = i;

    return FATX_STATUS_SUCCESS;
}

/*
 * Add a cluster to a chain.
 */
int fatx_attach_cluster(struct fatx_fs *fs, size_t tail, size_t cluster)
{
    int status;
    fatx_fat_entry fat_entry;

    fatx_debug(fs, "fatx_attach_cluster(tail=%zd, cluster=%zd)\n", tail, cluster);

    status = fatx_get_fat_entry_for_cluster(fs, tail, &fat_entry);
    if (status != FATX_STATUS_SUCCESS)
    {
        fatx_error(fs, "fatx_get_fat_entry_for_cluster returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
        return status;
    }

    status = fatx_get_fat_entry_type(fs, fat_entry);
    if (status != FATX_CLUSTER_END)
    {
        fatx_error(fs, "tail was not the last cluster in the chain\n");
        return FATX_STATUS_ERROR;
    }

    status = fatx_set_fat_entry_for_cluster(fs, tail, cluster);
    if (status)
    {
        fatx_error(fs, "failed to set fat entry for cluster\n");
        return status;
    }

    status = fatx_mark_cluster_end(fs, cluster);
    if (status)
    {
        fatx_error(fs, "failed to mark cluster end\n");
        return status;
    }

    return FATX_STATUS_SUCCESS;
}
