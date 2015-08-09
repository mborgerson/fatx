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

/*
 * Seek to a cluster, byte offset in the device.
 */
int fatx_dev_seek(struct fatx_fs *fs, size_t cluster, off_t offset)
{
    int status;
    size_t pos;

    fatx_debug(fs, "fatx_dev_seek(cluster=%zd, offset=0x%zx)\n", cluster, offset);

    /* Seek to cluster containing offset. */
    status = fatx_cluster_number_to_byte_offset(fs, cluster, &pos);
    if (status) return status;

    pos += offset;

    status = fseek(fs->device, pos, SEEK_SET);
    if (status)
    {
        fatx_error(fs, "failed to seek\n");
        return -1;
    }

    return 0;
}

/*
 * Read from the device.
 */
size_t fatx_dev_read(struct fatx_fs *fs, void *buf, size_t size, size_t items)
{
    fatx_debug(fs, "fatx_dev_read(buf=0x%p, size=0x%zx, items=0x%zx)\n", buf, size, items);
    return fread(buf, size, items, fs->device);
}