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
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "ext.h"

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
 * Get the dirname for a given path.
 *
 * | path     | dirname |
 * |----------|---------|
 * | /usr/lib | /usr    |
 * | /usr/    | /       |
 * | usr      | .       |
 * | /        | /       |
 * | .        | .       |
 * | ..       | .       |
 *
 */
char *fatx_dirname(const char *path)
{
    char *path_copy = strdup(path);
    char *path_dirname = strdup(ext_dirname(path_copy));
    free(path_copy);
    return path_dirname;
}

/*
 * Get the basename for a given path.
 *
 * | path     | basename |
 * |----------|----------|
 * | /usr/lib | /lib     |
 * | /usr/    | usr      |
 * | usr      | usr      |
 * | /        | /        |
 * | .        | .        |
 * | ..       | ..       |
 *
 */
char *fatx_basename(const char *path)
{
    char *path_copy = strdup(path);
    char *path_basename = strdup(ext_basename(path_copy));
    free(path_copy);
    return path_basename;
}

/*
 * Pack a FATX date.
 */
int fatx_pack_date(struct fatx_ts *in, uint16_t *out)
{
    *out = FATX_DATE(in->day, in->month, in->year);;
    return FATX_STATUS_SUCCESS;
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
 * Pack a FATX time.
 */
int fatx_pack_time(struct fatx_ts *in, uint16_t *out)
{
    *out = FATX_TIME(in->hour, in->minute, in->second);;
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

void fatx_time_t_to_fatx_ts(const time_t in, struct fatx_ts *out)
{
    struct tm *t;

    t = localtime(&in);

    out->second = t->tm_sec;
    out->minute = t->tm_min;
    out->hour   = t->tm_hour;
    out->day    = t->tm_mday;
    out->month  = t->tm_mon;
    out->year   = t->tm_year+1900;
}

time_t fatx_ts_to_time_t(struct fatx_ts *in)
{
    struct tm t;

    t.tm_sec  = in->second;
    t.tm_min  = in->minute;
    t.tm_hour = in->hour;
    t.tm_mday = in->day;
    t.tm_mon  = in->month;
    t.tm_year = in->year-1900;

    return mktime(&t);
}
