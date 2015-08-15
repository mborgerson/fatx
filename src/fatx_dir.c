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
                goto continue_to_next_entry;
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
                goto continue_to_next_entry;
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

            continue_to_next_entry:

            /* Get the next directory entry. */
            status = fatx_next_dir_entry(fs, dir);
            if (status != FATX_STATUS_SUCCESS) break;
        }
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Move to the next directory entry.
 */
int fatx_next_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir)
{
    fatx_fat_entry fat_entry;
    int status;

    fatx_debug(fs, "fatx_next_dir_entry()\n");

    dir->entry += 1;

    if (dir->entry < fs->bytes_per_cluster/sizeof(struct fatx_raw_directory_entry))
    {
        /* Not the last possible entry at the end of the cluster. */
        return FATX_STATUS_SUCCESS;
    }

    /* If we've reached this point, we need to find the next cluster, if any,
     * that contains additional directory entries.
     */
    status = fatx_read_fat(fs, dir->cluster, &fat_entry);
    if (status != FATX_STATUS_SUCCESS) return status;

    switch (fatx_get_fat_entry_type(fs, fat_entry))
    {
    case FATX_CLUSTER_DATA:
        dir->cluster = fat_entry;
        dir->entry = 0;
        fatx_info(fs, "found additional directory entries at cluster %zd\n", dir->cluster);
        return FATX_STATUS_SUCCESS;

    case FATX_CLUSTER_END:
        fatx_error(fs, "got end of cluster before end of directory\n");
        return FATX_STATUS_ERROR;

    default:
        fatx_error(fs, "expected another cluster with additional directory entries\n");
        return FATX_STATUS_ERROR;
    }
}

/*
 * Read the current directory entry.
 *
 * dir should be the directory opened by a call to fatx_open_dir.
 * entry should be a pointer to an allocation of struct fatx_dirent.
 * attr should be a pointer to an allocation of struct fatx_attr.
 * result should be a pointer to a pointer that contains the result of read_dir.
 */
int fatx_read_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr, struct fatx_dirent **result)
{
    struct fatx_raw_directory_entry directory_entry;
    size_t items, offset;
    int status;

    fatx_debug(fs, "fatx_read_dir(cluster=%zd, entry=%zd)\n", dir->cluster, dir->entry);

    /* Seek to the current cluster. */
    offset = dir->entry * sizeof(struct fatx_raw_directory_entry);
    status = fatx_dev_seek_cluster(fs, dir->cluster, offset);
    if (status)
    {
        fatx_error(fs, "failed to seek to directory entry\n");
        return FATX_STATUS_ERROR;
    }

    /* Read in the raw directory entry. */
    items = fatx_dev_read(fs, &directory_entry, sizeof(struct fatx_raw_directory_entry), 1);
    if (items != 1)
    {
        fatx_error(fs, "failed to read directory entry\n");
        return FATX_STATUS_ERROR;
    }

    /* Was this the last directory entry? */
    if (directory_entry.filename_len == FATX_END_OF_DIR_MARKER)
    {
        /* End of directory. */
        fatx_debug(fs, "reached the end of the directory\n");
        *result = NULL;
        return FATX_STATUS_END_OF_DIR;
    }

    /* Was this file deleted? */
    if (directory_entry.filename_len == FATX_DELETED_FILE_MARKER)
    {
        /* This directory entry is no longer in use. */
        fatx_debug(fs, "dirent %zd of cluster %zd is a deleted file\n", dir->entry, dir->cluster);
        return FATX_STATUS_FILE_DELETED;
    }

    fatx_debug(fs, "dirent %zd of cluster %zd data starts at %d\n", dir->entry, dir->cluster, directory_entry.first_cluster);

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
    return FATX_STATUS_SUCCESS;
}

/*
 * Close a directory.
 */
int fatx_close_dir(struct fatx_fs *fs, struct fatx_dir *dir)
{
    /* Nothing to do. */
    fatx_debug(fs, "fatx_close_dir()\n");
    return FATX_STATUS_SUCCESS;
}

/*
 * Mark a directory entry as deleted.
 */
int fatx_mark_dir_entry_deleted(struct fatx_fs *fs, struct fatx_dir *dir)
{
    struct fatx_raw_directory_entry raw_dirent;
    size_t offset, items;
    int status;

    fatx_debug(fs, "fatx_mark_dir_entry_deleted(cluster=%zd, entry=%zd)\n", dir->cluster, dir->entry);

    /* Seek to the directory entry. */
    offset = dir->entry * sizeof(struct fatx_raw_directory_entry);
    status = fatx_dev_seek_cluster(fs, dir->cluster, offset);
    if (status)
    {
        fatx_error(fs, "failed to seek to directory entry\n");
        return status;
    }

    /* Read in the raw directory entry. */
    items = fatx_dev_read(fs, &raw_dirent, sizeof(struct fatx_raw_directory_entry), 1);
    if (items != 1)
    {
        fatx_error(fs, "failed to read directory entry\n");
        return FATX_STATUS_ERROR;
    }

    /* Read advanced the pointer. Seek back. */
    status = fatx_dev_seek_cluster(fs, dir->cluster, offset);
    if (status != FATX_STATUS_SUCCESS)
    {
        fatx_error(fs, "failed to seek to directory entry\n");
        return status;
    }

    /* Finally, mark the file as deleted. */
    raw_dirent.filename_len = FATX_END_OF_DIR_MARKER;
    items = fatx_dev_write(fs, &raw_dirent, sizeof(struct fatx_raw_directory_entry), 1);
    if (items != 1)
    {
        fatx_error(fs, "failed to write directory entry\n");
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Remove a directory entry.
 */
int fatx_unlink(struct fatx_fs *fs, char const *path)
{
    struct fatx_dirent entry, *result;
    struct fatx_attr attr;
    struct fatx_dir dir;
    char path_buf[strlen(path)+1];
    char *filename;
    int status;

    fatx_debug(fs, "fatx_unlink(path=\"%s\")\n", path);

    /* Open the directory that contains this file. */
    strcpy(path_buf, path);
    status = fatx_open_dir(fs, fatx_dirname(path_buf), &dir);
    if (status != FATX_STATUS_SUCCESS) return status;

    strcpy(path_buf, path);
    filename = fatx_basename(path_buf);

    while (1)
    {
        status = fatx_read_dir(fs, &dir, &entry, &attr, &result);

        if (status == FATX_STATUS_SUCCESS)
        {
            /* Is this the file we're looking for? */
            if (strcmp(attr.filename, filename) == 0) break;
        }
        else if (status == FATX_STATUS_END_OF_DIR)
        {
            fatx_debug(fs, "reached end of dir\n");
            status = FATX_STATUS_FILE_NOT_FOUND;
            break;
        }
        else if (status == FATX_STATUS_FILE_DELETED)
        {
            /* Skip over the deleted file. */
            fatx_debug(fs, "skipping over deleted file\n");
        }
        else
        {
            /* Error */
            fatx_debug(fs, "error!\n");
            break;
        }

        /* Seek to the directory entry. */
        status = fatx_next_dir_entry(fs, &dir);
        if (status != FATX_STATUS_SUCCESS) break;
    }

    if (status != FATX_STATUS_SUCCESS) goto cleanup;

    fatx_debug(fs, "found file!\n");

    /* Traverse the cluster chain, marking each cluster as available. */
    status = fatx_free_cluster_chain(fs, attr.first_cluster);
    if (status != FATX_STATUS_SUCCESS) goto cleanup;

    status = fatx_mark_dir_entry_deleted(fs, &dir);
    if (status != FATX_STATUS_SUCCESS) goto cleanup;

cleanup:
    fatx_close_dir(fs, &dir);
    return status;
}
