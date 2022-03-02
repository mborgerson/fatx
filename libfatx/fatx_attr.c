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
#include <stdlib.h>

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
 * Populate a fatx_attr struct given a low-level directory entry.
 */
int fatx_attr_to_dirent(struct fatx_fs *fs, struct fatx_attr *attr, struct fatx_raw_directory_entry *entry)
{
    size_t filename_len = strlen(attr->filename);
    entry->filename_len = filename_len;
    memcpy(entry->filename, attr->filename, filename_len);

    entry->attributes    = attr->attributes;
    entry->first_cluster = attr->first_cluster;
    entry->file_size     = attr->file_size;

    fatx_pack_date(&(attr->modified), &(entry->modified_date));
    fatx_pack_time(&(attr->modified), &(entry->modified_time));
    fatx_pack_date(&(attr->created),  &(entry->created_date));
    fatx_pack_time(&(attr->created),  &(entry->created_time));
    fatx_pack_date(&(attr->accessed), &(entry->accessed_date));
    fatx_pack_time(&(attr->accessed), &(entry->accessed_time));

    return FATX_STATUS_SUCCESS;
}

/*
 * Get attributes.
 */
int fatx_get_attr_dir(struct fatx_fs *fs, char const *path, char const *start, struct fatx_dir *dir, struct fatx_dirent *dirent, struct fatx_attr *attr)
{
    struct fatx_dirent *nextdirent;
    int status;

    while (1)
    {
        status = fatx_read_dir(fs, dir, dirent, attr, &nextdirent);
        if (status == FATX_STATUS_SUCCESS)
        {
            if (strcmp(start, dirent->filename) == 0)
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
        status = fatx_next_dir_entry(fs, dir);
        if (status != FATX_STATUS_SUCCESS) break;
    }

    return status;
}

int fatx_get_attr(struct fatx_fs *fs, const char *path, struct fatx_attr *attr)
{
    struct fatx_dir dir;
    struct fatx_dirent dirent;
    int status;
    char *path_dirname, *path_basename;

    fatx_debug(fs, "fatx_get_attr(path=\"%s\")\n", path);

    path_dirname = fatx_dirname(path);
    status = fatx_open_dir(fs, path_dirname, &dir);
    free(path_dirname);
    if (status) return status;

    path_basename = fatx_basename(path);
    status = fatx_get_attr_dir(fs, path, path_basename, &dir, &dirent, attr);
    free(path_basename);

    fatx_close_dir(fs, &dir);
    return status;
}

/*
 * Write attributes to an existing file.
 */
int fatx_set_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr)
{
    struct fatx_dir dir;
    struct fatx_dirent dirent;
    struct fatx_attr old_attr;
    int status;
    char *path_dirname, *path_basename;

    fatx_debug(fs, "fatx_write_attr(path=\"%s\")\n", path);

    path_dirname = fatx_dirname(path);
    status = fatx_open_dir(fs, path_dirname, &dir);
    free(path_dirname);
    if (status) return status;

    path_basename = fatx_basename(path);
    status = fatx_get_attr_dir(fs, path, path_basename, &dir, &dirent, &old_attr);
    free(path_basename);
    if (status) return status;

    strcpy(dirent.filename, attr->filename);

    status = fatx_write_dir(fs, &dir, &dirent, attr);
    fatx_close_dir(fs, &dir);
    return status;
}

int fatx_utime(struct fatx_fs *fs, char const *path, struct fatx_ts ts[2])
{
    int status;
    struct fatx_attr attr;

    fatx_debug(fs, "fatx_utime(path=\"%s\")\n", path);

    status = fatx_get_attr(fs, path, &attr);
    if (status) return status;

    attr.accessed = ts[0];
    attr.modified = ts[1];

    status = fatx_set_attr(fs, path, &attr);
    return status;
}

