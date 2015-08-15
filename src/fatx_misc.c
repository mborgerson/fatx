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

#include <stddef.h>

/*
 * Get the specified path component.
 *
 * Note: path should be given relative to start on the filesystem boundary.
 * Note: trailing path separators are always included in the result.
 *
 * Example:
 *     const char *path = "/foo/bar/baz";
 *     char *start; size_t len;
 *     fatx_get_path_component(path, 0, &start, &len); --> /
 *     fatx_get_path_component(path, 1, &start, &len); --> foo/
 *     fatx_get_path_component(path, 2, &start, &len); --> bar/
 *     fatx_get_path_component(path, 3, &start, &len); --> baz
 *
 */
int fatx_get_path_component(char const *path, size_t component, char const **start, size_t *len)
{
    off_t i, j;

    i = 0;
    *start = NULL;
    *len = 0;

    while (1)
    {
        if (path[i] == '\0')
        {
            /* Nothing left to parse */
            break;
        }

        /* Get path name */
        for (j=i; 1; j++)
        {
            if (path[j] == '\0' || path[j] == FATX_PATH_SEPERATOR)
            {
                /* Found path separator (or end of line) */
                break;
            }
        }

        if (component == 0)
        {
            /* This is the component we are looking for */
            *start = path+i;
            *len = j-i+1;
            break;
        }

        if (path[j] == '\0')
        {
            /* Reached end of path before the desired component could be found */
            break;
        }

        i = j+1;
        component -= 1;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Get length of the directory part of path.
 *
 * Given a path such as: /path/to/file
 * Return the length of the string: /path/to/
 */
int fatx_get_dirname_len(char const *path)
{
    /* TODO */
    return 0;
}

/*
 * Get the starting character position of the filename from a path.
 */
int fatx_get_basename_index(char const *path)
{
    /* TODO */
    return 0;
}

/*
 * Unpack a FATX date.
 */
int fatx_unpack_date(uint16_t in, struct fatx_ts *out)
{
    out->year  = FATX_DATE_TO_YEAR(in);
    out->month = FATX_DATE_TO_MONTH(in);
    out->day   = FATX_DATE_TO_DAY(in);
    return FATX_STATUS_SUCCESS;
}

/*
 * Unpack a FATX time.
 */
int fatx_unpack_time(uint16_t in, struct fatx_ts *out)
{
    out->hour   = FATX_TIME_TO_HOUR(in);
    out->minute = FATX_TIME_TO_MINUTE(in);
    out->second = FATX_TIME_TO_SECOND(in);
    return FATX_STATUS_SUCCESS;
}
