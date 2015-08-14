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

#include "fatx_log.h"
#include "fatx.h"

#include <stdarg.h>
#include <stdio.h>

/*
 * Initialize logging.
 */
int fatx_log_init(struct fatx_fs *fs, FILE *stream, int level)
{
    fs->log_handle = stream;
    fs->log_level  = level;
    return FATX_STATUS_SUCCESS;
}

/*
 * Print a message.
 */
int fatx_print(struct fatx_fs *fs, int level, char const *format, ...)
{
    int status;
    va_list args;

    va_start(args, format);

    status = 0;

    if (fs->log_handle && level <= fs->log_level)
    {
        status = vfprintf(fs->log_handle, format, args);
    }

    va_end(args);

    return status;
}
