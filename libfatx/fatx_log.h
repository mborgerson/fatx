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

#ifndef FATX_LOG
#define FATX_LOG

#include <stdio.h>

struct fatx_fs;

#define FATX_LOG_LEVEL_NONE    0
#define FATX_LOG_LEVEL_FATAL   1
#define FATX_LOG_LEVEL_ERROR   2
#define FATX_LOG_LEVEL_WARNING 3
#define FATX_LOG_LEVEL_INFO    4
#define FATX_LOG_LEVEL_DEBUG   5
#define FATX_LOG_LEVEL_SPEW    6

#define fatx_fatal(fs, ...)   fatx_print (fs, FATX_LOG_LEVEL_FATAL,   __VA_ARGS__)
#define fatx_error(fs, ...)   fatx_print (fs, FATX_LOG_LEVEL_ERROR,   __VA_ARGS__)
#define fatx_warning(fs, ...) fatx_print (fs, FATX_LOG_LEVEL_WARNING, __VA_ARGS__)
#define fatx_info(fs, ...)    fatx_print (fs, FATX_LOG_LEVEL_INFO,    __VA_ARGS__)
#define fatx_debug(fs, ...)   fatx_print (fs, FATX_LOG_LEVEL_DEBUG,   __VA_ARGS__)
#define fatx_spew(fs, ...)    fatx_print (fs, FATX_LOG_LEVEL_SPEW,    __VA_ARGS__)

int fatx_log_init(struct fatx_fs *fs, FILE *stream, int level);
int fatx_print(struct fatx_fs *fs, int level, char const *format, ...);

#endif
