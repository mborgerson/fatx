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
 * Seek to a byte offset in the device.
 */
int fatx_dev_seek(struct fatx_fs *fs, uint64_t offset)
{
    int status;

#ifdef _WIN32
    status = _fseeki64(fs->device, offset, SEEK_SET);
#else
    status = fseeko(fs->device, offset, SEEK_SET);
#endif
    if (status)
    {
        fatx_error(fs, "failed to seek\n");
        return FATX_STATUS_ERROR;
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Seek to a cluster + byte offset in the device.
 */
int fatx_dev_seek_cluster(struct fatx_fs *fs, size_t cluster, off_t offset)
{
    int status;
    uint64_t pos;

    fatx_debug(fs, "fatx_dev_seek_cluster(cluster=%zd, offset=0x%zx)\n", cluster, offset);

    status = fatx_cluster_number_to_byte_offset(fs, cluster, &pos);
    if (status) return status;

    pos += offset;

    return fatx_dev_seek(fs, pos);
}

/*
 * Read from the device.
 */
size_t fatx_dev_read(struct fatx_fs *fs, void *buf, size_t size, size_t items)
{
    fatx_debug(fs, "fatx_dev_read(buf=0x%p, size=0x%zx, items=0x%zx)\n", buf, size, items);
    return fread(buf, size, items, fs->device);
}

/*
 * Write to the device.
 */
size_t fatx_dev_write(struct fatx_fs *fs, const void *buf, size_t size, size_t items)
{
    fatx_debug(fs, "fatx_dev_write(buf=0x%p, size=0x%zx, items=0x%zx)\n", buf, size, items);
    return fwrite(buf, size, items, fs->device);
}
