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
 * Populate a fatx_attr struct given a low-level directory entry.
 */
int fatx_dirent_to_attr(struct fatx_fs *fs, struct fatx_raw_directory_entry *entry, struct fatx_attr *attr)
{
    memcpy(attr->filename, entry->filename, entry->filename_len);
    attr->filename[entry->filename_len] = '\0';

    attr->attributes    = entry->attributes;
    attr->first_cluster = entry->first_cluster;
    attr->file_size     = entry->file_size;

    fatx_unpack_date(entry->modified_date, &(attr->modified));
    fatx_unpack_time(entry->modified_time, &(attr->modified));
    fatx_unpack_date(entry->created_date,  &(attr->created));
    fatx_unpack_time(entry->created_time,  &(attr->created));
    fatx_unpack_date(entry->accessed_date, &(attr->accessed));
    fatx_unpack_time(entry->accessed_time, &(attr->accessed));

    return FATX_STATUS_SUCCESS;
}

/*
 * Get attributes.
 */
int fatx_get_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr)
{
    struct fatx_dir dir;
    struct fatx_dirent dirent, *nextdirent;
    int status;
    char const *start;
    size_t len, component;

    fatx_debug(fs, "fatx_get_attr(path=\"%s\")\n", path);

    char subpath[strlen(path)+1];

    /* Get up to last path component. */
    for (component=0; 1; component++)
    {
        status = fatx_get_path_component(path, component, &start, &len);
        if (status) return status;

        if (start == NULL)
        {
            /* Reached last path component. */
            fatx_get_path_component(path, component-1, &start, &len);
            len = start-path;
            memcpy(subpath, path, len);
            subpath[len] = '\0';
            break;
        }
    }

    status = fatx_open_dir(fs, subpath, &dir);
    if (status) return status;

    while (1)
    {
        status = fatx_read_dir(fs, &dir, &dirent, attr, &nextdirent);
        if (status == FATX_STATUS_SUCCESS)
        {
            if (strcmp(start, dirent.filename) == 0)
            {
                /* Found! */
                status = FATX_STATUS_SUCCESS;
                break;
            }
        }
        else if (status == FATX_STATUS_FILE_DELETED)
        {
            /* Read a deleted file entry. Skip over it... */
        }
        else if (status == FATX_STATUS_END_OF_DIR)
        {
            /* Path not found! */
            status = FATX_STATUS_FILE_NOT_FOUND;
            break;
        }
        else
        {
            /* Error */
            status = FATX_STATUS_ERROR;
            break;
        }

        /* Get the next directory entry. */
        status = fatx_next_dir_entry(fs, &dir);
        if (status != FATX_STATUS_SUCCESS) break;
    }

    fatx_close_dir(fs, &dir);
    return status;
}

