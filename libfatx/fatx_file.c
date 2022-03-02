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
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>

/*
 * Determine the cluster which contains a byte offset of a file.
 */
int fatx_find_cluster_for_file_offset_alloc(struct fatx_fs *fs, struct fatx_attr *attr, size_t offset, size_t *result, bool alloc)
{
    fatx_fat_entry fat_entry;
    size_t cluster, alloc_cluster;
    int status;

    /* Sanity check the offset. */
    if (offset > attr->file_size)
    {
        fatx_error(fs, "offset out of range\n");
        return FATX_STATUS_ERROR;
    }

    /* Start at the first cluster and seek through the chain. */
    cluster = attr->first_cluster;

    while (offset >= fs->bytes_per_cluster)
    {
        fatx_debug(fs, "seeking... cluster = %zx\n", cluster);

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
        else if (alloc && status == FATX_CLUSTER_END)
        {
            fatx_debug(fs, "out of clusters, allocating new one\n");

            status = fatx_alloc_cluster(fs, &alloc_cluster);
            if (status) return status;

            status = fatx_attach_cluster(fs, cluster, alloc_cluster);
            if (status) return status;

            cluster = alloc_cluster;
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

int fatx_find_cluster_for_file_offset(struct fatx_fs *fs, struct fatx_attr *attr, size_t offset, size_t *result)
{
    return fatx_find_cluster_for_file_offset_alloc(fs, attr, offset, result, false);
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

    if (offset >= attr.file_size)
    {
        fatx_error(fs, "eof\n");
        return 0;
    }

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
        fatx_debug(fs, "cluster offset = %zx\n", cluster_offset);
        if (cluster_offset >= fs->bytes_per_cluster)
        {
            fatx_debug(fs, "looking for next cluster...\n");
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
 * Write to a file
 *
 * Returns the number of bytes written.
 */
int fatx_write(struct fatx_fs *fs, char const *path, off_t offset, size_t size, const void *buf)
{
    size_t total_bytes_written;
    size_t cluster;
    off_t cluster_offset;
    struct fatx_attr attr;
    int status;

    fatx_debug(fs, "fatx_write(path=\"%s\", offset=0x%zx, size=0x%zx, buf=%p)\n", path, offset, size, buf);

    /* Get file attributes. */
    status = fatx_get_attr(fs, path, &attr);
    if (status) return status;

    /* If the file offset is invalid, truncate the file to the correct size */
    if (offset > attr.file_size)
    {
        status = fatx_truncate(fs, path, offset+1);
        if (status) return status;

        /* Truncate modifies attr, so fetch it again */
        status = fatx_get_attr(fs, path, &attr);
        if (status) return status;
    }

    /* Find the cluster, device byte offset containing the file offset. */
    status = fatx_find_cluster_for_file_offset_alloc(fs, &attr, offset, &cluster, true);
    if (status)
    {
        fatx_error(fs, "failed to find cluster for offset\n");
        return 0;
    }

    /* Seek to the offset. */
    cluster_offset = offset % fs->bytes_per_cluster;
    status = fatx_dev_seek_cluster(fs, cluster, cluster_offset);
    if (status) return status;

    total_bytes_written = 0;

    while (1)
    {
        size_t bytes_to_write;
        size_t bytes_written;

        /* Careful not to overflow */
        bytes_to_write = size - total_bytes_written;
        if(fs->bytes_per_cluster >= cluster_offset)
        {
            bytes_to_write = MIN(fs->bytes_per_cluster - cluster_offset, bytes_to_write);   
        }

        /* Write to the current cluster if we have space. */
        if (bytes_to_write > 0)
        {
            bytes_written = fatx_dev_write(fs, buf, 1, bytes_to_write);
            if (bytes_written == 0)
            {
                fatx_error(fs, "failed to write to device\n");
                return FATX_STATUS_ERROR;
            }

            total_bytes_written += bytes_written;
            buf = (uint8_t *)buf + bytes_written;
            cluster_offset += bytes_written;
        }

        if (size - total_bytes_written == 0)
        {
            fatx_debug(fs, "finished writing\n");
            break;
        }

        /* Move to next cluster? */
        fatx_debug(fs, "cluster offset = %zx\n", cluster_offset);
        if (cluster_offset >= fs->bytes_per_cluster)
        {
            fatx_debug(fs, "looking for next cluster...\n");
            status = fatx_get_next_cluster(fs, &cluster);
            if (status)
            {
                fatx_debug(fs, "EOF, allocating new cluster\n");

                size_t new_cluster;
                status = fatx_alloc_cluster(fs, &new_cluster);
                if (status) return status;

                status = fatx_attach_cluster(fs, cluster, new_cluster);
                if (status) return status;
            }

            status = fatx_dev_seek_cluster(fs, cluster, 0);
            if (status) return status;

            cluster_offset = 0;
        }
    }

    fatx_debug(fs, "bytes written: %zx\n", total_bytes_written);

    /* Update file size if it has increased. */
    if(offset + size > attr.file_size)
    {
        attr.file_size += offset + size - attr.file_size;
        status = fatx_set_attr(fs, path, &attr);
        if (status) return status;
    }

    return total_bytes_written;
}

/*
 * Create a file.
 */
int fatx_mknod(struct fatx_fs *fs, char const *path)
{
    struct fatx_attr attr;
    struct fatx_dir dir;
    char *path_dirname;
    int status;

    fatx_debug(fs, "fatx_mknod(path=\"%s\")\n", path);

    /* Check for existence */
    status = fatx_get_attr(fs, path, &attr);
    if (!status)
    {
        fatx_error(fs, "file already exists\n");
        return FATX_STATUS_ERROR;
    }

    /* Open the directory. */
    path_dirname = fatx_dirname(path);
    status = fatx_open_dir(fs, path_dirname, &dir);
    free(path_dirname);
    if (status) return status;

    /* Create the file node */
    status = fatx_create_dirent(fs, path, &dir, 0);

    /* Cleanup. */
    fatx_close_dir(fs, &dir);
    return status;
}

/*
 * Truncate a file to the specified size
 */
int fatx_truncate(struct fatx_fs *fs, char const *path, off_t offset)
{
    fatx_debug(fs, "fatx_truncate(path=\"%s\", offset=0x%zx)\n", path, offset);

    struct fatx_attr attr;
    int status;

    /* Get file attributes. */
    status = fatx_get_attr(fs, path, &attr);
    if (status) return status;

    size_t enc_clusters = 1;
    size_t cluster = attr.first_cluster;
    while (enc_clusters * fs->bytes_per_cluster < offset)
    {
        status = fatx_get_next_cluster(fs, &cluster);
        if (status == FATX_STATUS_ERROR)
        {
            /* Out of clusters, alloc more */
            size_t new_cluster;
            status = fatx_alloc_cluster(fs, &new_cluster);
            if (status) return status;

            status = fatx_attach_cluster(fs, cluster, new_cluster);
            if (status) return status;

            cluster = new_cluster;
            ++enc_clusters;
        }
        else if (status == FATX_STATUS_SUCCESS)
        {
            /* Found next cluster, continue */
            ++enc_clusters;
        }
        else return status;
    }

    /* If there are more clusters, then free them */
    size_t next_cluster = cluster;
    status = fatx_get_next_cluster(fs, &next_cluster);
    if(status == FATX_STATUS_SUCCESS)
    {
        status = fatx_free_cluster_chain(fs, next_cluster);
        if (status) return status;
    }

    /* Mark new end of cluster */
    status = fatx_mark_cluster_end(fs, cluster);
    if (status) return status;

    /* Now update the file size */
    attr.file_size = offset;
    status = fatx_set_attr(fs, path, &attr);
    if (status) return status;

    return FATX_STATUS_SUCCESS;
}

/*
 * Rename a file
 */
int fatx_rename(struct fatx_fs *fs, char const *from, char const *to)
{
    fatx_debug(fs, "fatx_rename(from=\"%s\", to=\"%s\")\n", from, to);

    struct fatx_attr attr;
    char *from_dirname, *to_dirname;
    char *to_basename;
    int path_dif, status;

    /* Sanity check that we're not trying to move the file */
    from_dirname = fatx_dirname(from);
    to_dirname = fatx_dirname(to);
    path_dif = strcmp(from_dirname, to_dirname);
    free(from_dirname);
    free(to_dirname);
    if (path_dif)
    {
        fatx_error(fs, "rename directories do not match\n");
        return FATX_STATUS_ERROR;
    }

    /* Get file attributes. */
    status = fatx_get_attr(fs, from, &attr);
    if (status) return status;

    /* Check that the new filename is not too long */
    to_basename = fatx_basename(to);
    if (strlen(to_basename) >= FATX_MAX_FILENAME_LEN)
    {
        free(to_basename);
        fatx_error(fs, "destination name too long\n");
        return FATX_STATUS_ERROR;
    }

    /* Rename the file */
    strcpy(attr.filename, to_basename);
    free(to_basename);

    /* Save new attributes */
    return fatx_set_attr(fs, from, &attr);
}
