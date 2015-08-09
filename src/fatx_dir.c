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

/* The root directory is special in that it is the very first cluster, and it
 * cannot be indexed using the typical 2 index convention. It is always the
 * first cluster. When fatx_read_dir sees this in the cluster field of struct
 * fatx_dir, it will read from the root directory cluster.
 */
#define FATX_ROOT_DIR_CLUSTER 0

/*
 * Open a directory.
 */
int fatx_open_dir(struct fatx_fs *fs, char const *path, struct fatx_dir *dir)
{
    struct fatx_dirent dirent, *nextdirent;
    struct fatx_attr attr;
    size_t component, len;
    char const *start;
    int status;

    fatx_debug(fs, "fatx_open_dir(path=\"%s\")\n", path);

    status = fatx_get_path_component(path, 0, &start, &len);
    if (status || !(len == 1 && start[0] == FATX_PATH_SEPERATOR))
    {
        /* Paths should begin with the path separator. */
        fatx_error(fs, "invalid path\n");
        return -1;
    }

    dir->cluster = FATX_ROOT_DIR_CLUSTER;
    dir->entry   = 0;

    for (component=1; 1; component++)
    {
        fatx_spew(fs, "checking component %zd in path %s\n", component, path);

        /* Get the next path component. */
        status = fatx_get_path_component(path, component, &start, &len);
        if (status)
        {
            fatx_error(fs, "invalid path\n");
            return -1;
        }

        if (start == NULL)
        {
            /* Reached the end of the path. At this point, cluster has already
             * been set and the path has been found.
             */
            break;
        }

        /* Iterate over the directory entries in this directory, looking for the
         * path component.
         */
        while (1)
        {
            /* Get the next entry in this directory. */
            status = fatx_read_dir(fs, dir, &dirent, &attr, &nextdirent);

            if (status == FATX_STATUS_SUCCESS)
            {
                /* Fantastic. See below. */
            }
            else if (status == FATX_STATUS_FILE_DELETED)
            {
                /* File deleted. Skip over it. */
                continue;
            }
            else if (status == FATX_STATUS_END_OF_DIR)
            {
                fatx_error(fs, "path not found\n");
                return FATX_STATUS_FILE_NOT_FOUND;
            }
            else
            {
                /* Error occured. */
                return FATX_STATUS_ERROR;
            }

            fatx_debug(fs, "fatx_read_dir found %s\n", dirent.filename);

            /* Check the attributes to see if this is a directory. */
            if ((attr.attributes & FATX_ATTR_DIRECTORY) == 0)
            {
                /* Not a directory. */
                continue;
            }

            /* Trim trailing slash, if present. */
            if (start[len-1] == FATX_PATH_SEPERATOR)
            {
                len -= 1;
            }

            /* Compare the path component to this directory entry. */
            if (memcmp(dirent.filename, start, len) == 0)
            {
                /* Path found. */
                dir->cluster = attr.first_cluster;
                dir->entry = 0;
                break;
            }
        }
    }

    return 0;
}


/*
 * Get the next directory entry.
 *
 * dir should be the directory opened by a call to fatx_open_dir.
 * entry should be a pointer to an allocation of struct fatx_dirent.
 * attr should be a pointer to an allocation of struct fatx_attr.
 * result should be a pointer to a pointer that contains the result of read_dir.
 */
int fatx_read_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr, struct fatx_dirent **result)
{
    struct fatx_raw_directory_entry directory_entry;
    int status;
    size_t items, offset;
    fatx_fat_entry fat_entry;

    fatx_debug(fs, "fatx_read_dir(cluster=%zd, entry=%zd)\n", dir->cluster, dir->entry);

    /* Was last entry at the end of the cluster? */
    if (dir->entry >= fs->bytes_per_cluster/sizeof(struct fatx_raw_directory_entry))
    {
        fatx_debug(fs, "fatx_read_dir - reached last entry in dir, checking for "
                       "additional cluster in FAT\n");

        /* Yes. Check to see if there is another cluster. */
        status = fatx_read_fat(fs, dir->cluster, &fat_entry);
        if (status) return -1;

        status = fatx_get_fat_entry_type(fs, fat_entry);

        if (status == FATX_CLUSTER_DATA)
        {
            /* There is another cluster. Move to it. */
            dir->cluster = fat_entry;
            dir->entry = 0;
        }
        else if (status == FATX_CLUSTER_END)
        {
            /* This is the end of the cluster chain. Should have received a
             * end-of-directory marker before this point. Directory or FAT may
             * be corrupt.
             */
            fatx_error(fs, "did not get end of directory yet and there's no cluster to go to\n");
            return FATX_STATUS_ERROR;
        }
        else
        {
            /* Error. */
            fatx_error(fs, "expected another cluster\n");
            return FATX_STATUS_ERROR;
        }
    }

    /* Seek to directory entry. */
    if (dir->cluster == FATX_ROOT_DIR_CLUSTER)
    {
        /* Root directory is desired. */
        offset = fs->root_offset;
    }
    else
    {
        status = fatx_cluster_number_to_byte_offset(fs, dir->cluster, &offset);
        if (status) return status;
    }

    offset += dir->entry * sizeof(struct fatx_raw_directory_entry);
    status = fatx_dev_seek(fs, offset);
    if (status)
    {
        fatx_error(fs, "failed to seek to directory entry\n");
        return -1;
    }

    /* Get the real directory entry. */
    items = fatx_dev_read(fs, &directory_entry, sizeof(struct fatx_raw_directory_entry), 1);
    if (items != 1)
    {
        fatx_error(fs, "failed to read directory entry\n");
        return -1;
    }

    /* Was this the last directory entry? */
    if (directory_entry.filename_len == FATX_END_OF_DIR_MARKER)
    {
        /* End of directory. */
        fatx_debug(fs, "got end of dir\n");
        *result = NULL;
        return FATX_STATUS_END_OF_DIR;
    }

    /* Was this file deleted? */
    if (directory_entry.filename_len == FATX_DELETED_FILE_MARKER)
    {
        /* This directory entry is no longer in use. */
        fatx_debug(fs, "dirent %zd is a deleted file\n", dir->entry);
        dir->entry += 1;
        return FATX_STATUS_FILE_DELETED;
    }

    fatx_debug(fs, "dirent %zd first cluster is %d\n", dir->entry, directory_entry.first_cluster);

    /* Copy filename. */
    memcpy(entry->filename, directory_entry.filename, directory_entry.filename_len);
    entry->filename[directory_entry.filename_len] = '\0';

    /* Populate attributes. */
    if (attr != NULL)
    {
        status = fatx_dirent_to_attr(fs, &directory_entry, attr);
        if (status)
        {
            fatx_error(fs, "failed to get directory entry attributes\n");
            return status;
        }
    }

    *result = entry;
    dir->entry += 1;
    return FATX_STATUS_SUCCESS;
}

/*
 * Close a directory.
 */
int fatx_close_dir(struct fatx_fs *fs, struct fatx_dir *dir)
{
    /* Nothing to do. */
    return 0;
}
