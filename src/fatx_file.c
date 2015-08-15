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
 * Determine the cluster which contains a byte offset of a file.
 */
int fatx_find_cluster_for_file_offset(struct fatx_fs *fs, struct fatx_attr *attr, size_t offset, size_t *result)
{
    fatx_fat_entry fat_entry;
    size_t cluster;
    int status;

    /* Sanity check the offset. */
    if (offset >= attr->file_size)
    {
        fatx_error(fs, "offset out of range\n");
        return FATX_STATUS_ERROR;
    }

    /* Start at the first cluster and seek through the chain. */
    cluster = attr->first_cluster;

    while (offset >= fs->bytes_per_cluster)
    {
        fatx_error(fs, "seeking... cluster = %zx\n", cluster);

        /*
         * There is at least one more cluster.
         * Look in the FAT to find the next cluster number.
         */
        status = fatx_read_fat(fs, cluster, &fat_entry);
        if (status) return status;

        status = fatx_get_fat_entry_type(fs, fat_entry);
        if (status == FATX_CLUSTER_DATA)
        {
            /* Great, there is another cluster. Move to it. */
            cluster = fat_entry;
        }
        else
        {
            /* Error. */
            fatx_error(fs, "expected another cluster while seeking to file offset\n");
            return FATX_STATUS_ERROR;
        }

        /* Consume the bytes of the last cluster. */
        offset -= fs->bytes_per_cluster;
    }

    *result = cluster;
    return FATX_STATUS_SUCCESS;
}

/*
 * Read from a file
 *
 * Returns the number of bytes read.
 * Returns 0 on EOF.
 */
int fatx_read(struct fatx_fs *fs, char const *path, off_t offset, size_t size, void *buf)
{
    size_t total_bytes_read;
    size_t bytes_remaining_in_file;
    size_t cluster;
    off_t cluster_offset;
    struct fatx_attr attr;
    int status;

    fatx_debug(fs, "fatx_read(path=\"%s\", offset=0x%zx, size=0x%zx, buf=%p)\n", path, offset, size, buf);

    /* Get file attributes. */
    status = fatx_get_attr(fs, path, &attr);
    if (status) return status;

    /* Find the cluster, device byte offset containing the file offset. */
    status = fatx_find_cluster_for_file_offset(fs, &attr, offset, &cluster);
    if (status) return 0;

    /* Seek to the offset. */
    cluster_offset = offset % fs->bytes_per_cluster;
    status = fatx_dev_seek_cluster(fs, cluster, cluster_offset);
    if (status) return status;

    total_bytes_read = 0;
    bytes_remaining_in_file = attr.file_size - offset;

    while (1)
    {
        size_t bytes_to_read;
        size_t bytes_read;

        if (bytes_remaining_in_file == 0)
        {
            fatx_error(fs, "eof 0\n");
            break;
        }

        bytes_to_read = MIN(fs->bytes_per_cluster, size-total_bytes_read);
        bytes_to_read = MIN(bytes_to_read, bytes_remaining_in_file);

        if (bytes_to_read == 0)
        {
            fatx_error(fs, "eof 1\n");
            break;
        }

        /* Read from current cluster. */
        bytes_read = fatx_dev_read(fs, buf, 1, bytes_to_read);
        if (bytes_read == 0)
        {
            fatx_error(fs, "failed to read from device\n");
            return FATX_STATUS_ERROR;
        }

        total_bytes_read += bytes_read;
        bytes_remaining_in_file -= bytes_read;
        buf = (uint8_t *)buf + bytes_read;
        cluster_offset += bytes_read;

        if (bytes_remaining_in_file == 0)
        {
            fatx_error(fs, "eof 2\n");
            break;
        }

        /* Move to next cluster? */
        fatx_error(fs, "cluster offset = %zx\n", cluster_offset);
        if (cluster_offset >= fs->bytes_per_cluster)
        {
            fatx_error(fs, "looking for next cluster...\n");
            status = fatx_get_next_cluster(fs, &cluster);
            if (status)
            {
                fatx_error(fs, "expected another cluster\n");
                return status;
            }

            status = fatx_dev_seek_cluster(fs, cluster, 0);
            if (status) return status;

            cluster_offset = 0;
        }
    }

    fatx_debug(fs, "bytes read: %zx\n", total_bytes_read);

    return total_bytes_read;
}

/*
 * Remove a file.
 */
int fatx_unlink(struct fatx_fs *fs, char const *path)
{
    struct fatx_attr attr;
    size_t cluster, next_cluster;
    int status;

    fatx_debug(fs, "fatx_unlink(path=\"%s\")\n", path);

    status = fatx_get_attr(fs, path, &attr);
    if (status != FATX_STATUS_SUCCESS) return status;

    /* Traverse the cluster chain, marking each cluster as available. */
    cluster = attr.first_cluster;
    next_cluster = cluster;

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

    /* TODO: Mark the file as deleted. */

    return FATX_STATUS_ERROR;
}
