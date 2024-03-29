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

#include <stdbool.h>
#include "fatx_internal.h"

static bool fatx_cluster_valid(struct fatx_fs *fs, size_t cluster)
{
    return (cluster >= 0) &&
           (cluster < fs->num_clusters + FATX_FAT_RESERVED_ENTRIES_COUNT);
}

/*
 * Initialize a blank FAT.
 */
int fatx_init_fat(struct fatx_fs *fs)
{
    int64_t bytes_remaining;
    uint8_t *chunk;
    size_t chunk_size;
    int retval = FATX_STATUS_SUCCESS;

    if (fatx_dev_seek(fs, fs->fat_offset))
    {
        fatx_error(fs, "failed to seek to FAT start (offset 0x%zx)\n", fs->fat_offset);
        return FATX_STATUS_ERROR;
    }

    /*
     * A FAT could span multiple gigabytes with a very large partition (tbs)
     * using small clusters, so we want to create a relatively large zero
     * buffer in those cases while still capping how much memory we allocate
     * for the chunked writes to zero out the FAT table.
     */
    chunk_size = MAX(0x4000, (fs->fat_size >> 8));
    chunk = malloc(chunk_size);
    if (!chunk)
    {
        fatx_error(fs, "failed to initialize memory for FAT wipe\n");
        return FATX_STATUS_ERROR;
    }
    memset(chunk, 0x00, chunk_size);

    bytes_remaining = fs->fat_size;
    while (bytes_remaining > 0)
    {
        size_t bytes_to_write = MIN(chunk_size, bytes_remaining);
        if (fatx_dev_write(fs, chunk, bytes_to_write, 1) != 1)
        {
            fatx_error(fs, "failed to clear FAT chunk (offset 0x%zx)\n", fs->fat_offset + (bytes_remaining - fs->fat_size));
            retval = FATX_STATUS_ERROR;
            break;
        }
        bytes_remaining -= bytes_to_write;
    }

    free(chunk);
    return retval;
}

/*
 * Initialize the root directory.
 */
int fatx_init_root(struct fatx_fs *fs)
{
    uint8_t *chunk;
    int retval = FATX_STATUS_SUCCESS;

    if (fatx_write_fat(fs, 0, 0xfffffff8) || fatx_mark_cluster_end(fs, fs->root_cluster))
    {
        fatx_error(fs, "failed to initialize FAT with root entries\n");
        return FATX_STATUS_ERROR;
    }

    chunk = malloc(fs->bytes_per_cluster);
    memset(chunk, FATX_END_OF_DIR_MARKER, fs->bytes_per_cluster);

    if (fatx_dev_seek(fs, fs->cluster_offset))
    {
        fatx_error(fs, " - failed to seek to root directory cluster\n");
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    if (fatx_dev_write(fs, chunk, fs->bytes_per_cluster, 1) != 1)
    {
        fatx_error(fs, " - failed to initialize root cluster\n");
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

cleanup:
    free(chunk);
    return retval;
}

int fatx_flush_fat_cache(struct fatx_fs *fs)
{
    struct fatx_cache *cache = &fs->fat_cache;

    fatx_debug(fs, "fatx_flush_fat_cache()\n");

    if (!cache->data || !cache->dirty)
    {
        return FATX_STATUS_SUCCESS;
    }

    if (fatx_dev_seek(fs, fs->fat_offset + cache->position * cache->entry_size))
    {
        fatx_error(fs, "failed to seek to fat cache start index %zd (offset 0x%zx)\n",
                   cache->position, fs->fat_offset + cache->position * cache->entry_size);
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_write(fs, cache->data, cache->entry_size, cache->entries) != cache->entries)
    {
        fatx_error(fs, "failed to write fatx cache entries to disk\n");
        return FATX_STATUS_ERROR;
    }

    cache->dirty = false;
    return FATX_STATUS_SUCCESS;
}

int fatx_populate_fat_cache(struct fatx_fs *fs, size_t index)
{
    int result;
    struct fatx_cache *cache = &fs->fat_cache;

    fatx_debug(fs, "fatx_populate_fat_cache(index=%zd)\n", index);

    if (!fatx_cluster_valid(fs, index))
    {
        fatx_error(fs, "index number out of bounds\n");
        return FATX_STATUS_ERROR;
    }

    if (fatx_flush_fat_cache(fs)) {
        fatx_error(fs, "failed to flush fat cache\n");
        return FATX_STATUS_ERROR;
    }

    if (cache->data)
    {
        free(cache->data);
    }

    cache->position   = index;
    cache->entries    = MIN(FATX_FAT_CACHE_NUM_ENTRIES, fs->num_clusters + FATX_FAT_RESERVED_ENTRIES_COUNT - index);
    cache->entry_size = fs->fat_type == FATX_FAT_TYPE_16 ? 2 : 4;

    fatx_debug(fs, "populating fat cache: [pos: %zd, entries: %zd, entry_size: %zd]\n",
               cache->position, cache->entries, cache->entry_size);

    cache->data = malloc(cache->entries * cache->entry_size);

    if (fatx_dev_seek(fs, fs->fat_offset + cache->position * cache->entry_size))
    {
        fatx_error(fs, "failed to seek to fat cache start index %zd (offset 0x%zx)\n",
                   cache->position, fs->fat_offset + cache->position * cache->entry_size);
        return FATX_STATUS_ERROR;
    }

    if (fatx_dev_read(fs, cache->data, cache->entry_size, cache->entries) != cache->entries)
    {
        fatx_error(fs, "failed to populate fat cache entries\n");
        return FATX_STATUS_ERROR;
    }

    cache->dirty = false;
    return FATX_STATUS_SUCCESS;
}

/*
 * Read from the FAT.
 */
int fatx_read_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry *entry)
{
    struct fatx_cache *cache = &fs->fat_cache;

    fatx_debug(fs, "fatx_read_fat(index=%zd)\n", index);

    if (!fatx_cluster_valid(fs, index))
    {
        fatx_error(fs, "index number out of bounds\n");
        return FATX_STATUS_ERROR;
    }

    if (!cache->data || index < cache->position || index >= cache->position + cache->entries)
    {
        fatx_debug(fs, "fat cache miss for index %zd\n", index);
        if (fatx_populate_fat_cache(fs, index))
        {
            fatx_error(fs, "failed to populate fat cache\n");
            return FATX_STATUS_ERROR;
        }
    }

    if (fs->fat_type == FATX_FAT_TYPE_16)
    {
        *entry = ((uint16_t*) cache->data)[index - cache->position];
    }
    else
    {
        *entry = ((uint32_t*) cache->data)[index - cache->position];
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Write to the FAT.
 */
int fatx_write_fat(struct fatx_fs *fs, size_t index, fatx_fat_entry entry)
{
    struct fatx_cache *cache = &fs->fat_cache;

    fatx_debug(fs, "fatx_write_fat(index=%zd, entry=%zx)\n", index, entry);

    if (!fatx_cluster_valid(fs, index))
    {
        fatx_error(fs, "index number out of bounds\n");
        return FATX_STATUS_ERROR;
    }

    if (!cache->data || index < cache->position || index >= cache->position + cache->entries)
    {
        fatx_debug(fs, "fat cache miss for index %zd\n", index);
        if (fatx_populate_fat_cache(fs, index))
        {
            fatx_error(fs, "failed to populate fat cache\n");
            return FATX_STATUS_ERROR;
        }
    }

    if (fs->fat_type == FATX_FAT_TYPE_16)
    {
        ((uint16_t*) cache->data)[index - cache->position] = entry;
    }
    else
    {
        ((uint32_t*) cache->data)[index - cache->position] = entry;
    }

    cache->dirty = true;
    return FATX_STATUS_SUCCESS;
}

/*
 * Get the type of entry in the FAT.
 */
int fatx_get_fat_entry_type(struct fatx_fs *fs, fatx_fat_entry entry)
{
    /*
     * Sign-extend a 16bit FATX entry to 32bit so that the same marker
     * checking logic can be used in the switch table below.
     *   eg. 0xFFF8 --> 0xFFFFFFF8
     */
    if (fs->fat_type == FATX_FAT_TYPE_16)
    {
        entry = (int32_t)((int16_t)entry);
    }

    switch (entry)
    {
        case 0x00000000:
            return FATX_CLUSTER_AVAILABLE;
        case 0xfffffff0:
            return FATX_CLUSTER_RESERVED;
        case 0xfffffff7:
            return FATX_CLUSTER_BAD;
        case 0xfffffff8:
            return FATX_CLUSTER_MEDIA;
        case 0xffffffff:
            return FATX_CLUSTER_END;
    }

    if (entry < 0xfffffff0)
    {
        return FATX_CLUSTER_DATA;
    }

    return FATX_CLUSTER_INVALID;
}

/*
 * Get the absolute byte address of the start of the cluster.
 */
int fatx_cluster_number_to_byte_offset(struct fatx_fs *fs, size_t cluster, uint64_t *offset)
{
    fatx_debug(fs, "fatx_cluster_number_to_byte_offset - cluster = %zd\n", cluster);

    if (!fatx_cluster_valid(fs, cluster))
    {
        fatx_error(fs, "cluster number out of range %zd\n", cluster);
        return FATX_STATUS_ERROR;
    }

    *offset = fs->cluster_offset
              + (cluster - FATX_FAT_RESERVED_ENTRIES_COUNT) * fs->bytes_per_cluster;

    if (*offset >= fs->partition_offset + fs->partition_size)
    {
        fatx_error(fs, "Cluster %d has overrun partition limit !!!\n", cluster);
        fatx_error(fs, "Bailing to avoid corruption");
        return FATX_STATUS_ERROR;
    }

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
    fatx_write_fat(fs, cluster, 0);
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
        fatx_write_fat(fs, cluster, 0xffff);
    }
    else
    {
        fatx_write_fat(fs, cluster, 0xffffffff);
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
int fatx_alloc_cluster(struct fatx_fs *fs, size_t *cluster, bool zero)
{
    int status;
    fatx_fat_entry fat_entry;
    static size_t i = 2;
    size_t start_i;
    bool wrapped;
    void *zero_buf;

    fatx_debug(fs, "fatx_alloc_cluster(zero: %s)\n", zero ? "true" : "false");

    start_i = i;
    wrapped = false;

    for (; 1; i++)
    {
        if (i >= fs->num_clusters)
        {
            i = 2;
            wrapped = true;
        }

        /* Check for the case that the FAT was full when mounting initially */
        if (i == start_i && wrapped)
        {
            fatx_error(fs, "no clusters available to allocate\n");
            return FATX_STATUS_ERROR;
        }

        status = fatx_read_fat(fs, i, &fat_entry);
        if (status != FATX_STATUS_SUCCESS)
        {
            fatx_error(fs, "fatx_read_fat returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
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

    status = fatx_dev_seek_cluster(fs, i, 0);
    if (status != FATX_STATUS_SUCCESS) return status;

    if (zero)
    {
        zero_buf = calloc(1, fs->bytes_per_cluster);
        if (!zero_buf) return FATX_STATUS_ERROR;

        status = fatx_dev_write(fs, zero_buf, fs->bytes_per_cluster, 1);
        free(zero_buf);
        if (status != 1) return status;
    }

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

    status = fatx_read_fat(fs, tail, &fat_entry);
    if (status != FATX_STATUS_SUCCESS)
    {
        fatx_error(fs, "fatx_read_fat returned status=%d, fat_entry = 0x%x\n", status, fat_entry);
        return status;
    }

    status = fatx_get_fat_entry_type(fs, fat_entry);
    if (status != FATX_CLUSTER_END)
    {
        fatx_error(fs, "tail was not the last cluster in the chain\n");
        return FATX_STATUS_ERROR;
    }

    status = fatx_write_fat(fs, tail, cluster);
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
