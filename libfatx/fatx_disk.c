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

struct fatx_partition_map_entry const fatx_partition_map[] = {

    /* Retail partitions */
    { .letter = 'x',    .offset = 0x00080000, .size = 0x02ee00000 },
    { .letter = 'y',    .offset = 0x2ee80000, .size = 0x02ee00000 },
    { .letter = 'z',    .offset = 0x5dc80000, .size = 0x02ee00000 },
    { .letter = 'c',    .offset = 0x8ca80000, .size = 0x01f400000 },
    { .letter = 'e',    .offset = 0xabe80000, .size = 0x1312d6000 },

    /* Extended (non-retail) partitions commonly used in homebrew */
    { .letter = 'f',    .offset = 0x1dd156000, .size = -1 },

};

/*
 * Given a drive letter, determine partition offset and size (in bytes).
 */
int fatx_drive_to_offset_size(char drive_letter, uint64_t *offset, uint64_t *size)
{
    struct fatx_partition_map_entry const *pi;

    for (int i = 0; i < ARRAY_SIZE(fatx_partition_map); i++)
    {
        pi = &fatx_partition_map[i];

        if (pi->letter == drive_letter)
        {
            *offset = pi->offset;
            *size   = pi->size;
            return FATX_STATUS_SUCCESS;
        }
    }

    return FATX_STATUS_ERROR;
}

/*
 * Determine the disk size (in bytes).
 */
int fatx_disk_size(char const *path, uint64_t *size)
{
    FILE * device;
    int retval;

    device = fopen(path, "r");
    if (!device)
    {
        fprintf(stderr, "failed to open %s for size query\n", path);
        return FATX_STATUS_ERROR;
    }

    if (fseek(device, 0, SEEK_END))
    {
        fprintf(stderr, "failed to seek to end of disk\n");
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

#ifdef _WIN32
    *size = _ftelli64(device);
#else
    *size = ftello(device);
#endif
    retval = FATX_STATUS_SUCCESS;

cleanup:
    fclose(device);
    return retval;
}

/*
 * Determine the remaining disk size (in bytes) from disk offset.
 */
int fatx_disk_size_remaining(char const *path, uint64_t offset, uint64_t *remaining_size)
{
    uint64_t disk_size;

    if (fatx_disk_size(path, &disk_size))
    {
        return FATX_STATUS_ERROR;
    }

    if (offset > disk_size)
    {
        fprintf(stderr, "invalid disk offset\n");
        return FATX_STATUS_ERROR;
    }

    *remaining_size = disk_size - offset;
    return FATX_STATUS_SUCCESS;
}

/*
 * Reformat a disk as FATX.
 */
int fatx_disk_format(struct fatx_fs *fs, char const *path, size_t sector_size, enum fatx_format format_type, size_t sectors_per_cluster)
{
    struct fatx_partition_map_entry const *pi;
    uint64_t f_offset, f_size;

    if (format_type == FATX_FORMAT_INVALID)
    {
        return FATX_STATUS_ERROR;
    }

    fatx_info(fs, "Writing refurb info...\n");
    if (fatx_disk_write_refurb_info(path, 0, 0))
    {
        return FATX_STATUS_ERROR;
    }

    for (int i = 0; i < FATX_RETAIL_PARTITION_COUNT; i++)
    {
        pi = &fatx_partition_map[i];

        fatx_info(fs, "-------------------------------------------\n");
        fatx_info(fs, "Formatting partition %d (%c drive) ...\n", i, pi->letter);

        /*
         * Xapi initialization validates that the cluster size of retail
         * partitions is 16kb when a game begins loading.
         *
         * For this reason, it is imperative that we do not let users
         * configure the cluster size on retail partitions or many games
         * will not load. Adjusting sector sizes, however, is okay.
         */
        if (fatx_disk_format_partition(fs, path, pi->offset, pi->size, sector_size, FATX_RETAIL_CLUSTER_SIZE / sector_size))
        {
            fatx_error(fs, " - failed to format partition %d\n", i);
            return FATX_STATUS_ERROR;
        }
    }

    if (format_type == FATX_FORMAT_RETAIL)
    {
        return FATX_STATUS_SUCCESS;
    }
    else if (format_type == FATX_FORMAT_F_TAKES_ALL)
    {
        fatx_drive_to_offset_size('f', &f_offset, &f_size);

        fatx_info(fs, "-------------------------------------------\n");
        fatx_info(fs, "Formatting partition %d (%c drive) ...\n", 5, 'f');

        if (fatx_disk_format_partition(fs, path, f_offset, f_size, sector_size, sectors_per_cluster))
        {
            fatx_error(fs, " - failed to format partition %d (f-takes-all)\n", 5);
            return FATX_STATUS_ERROR;
        }
    }

    return FATX_STATUS_SUCCESS;
}

/*
 * Format partition.
 */
int fatx_disk_format_partition(struct fatx_fs *fs, char const *path, uint64_t offset, uint64_t size, size_t sector_size, size_t sectors_per_cluster)
{
    int retval;

    if (fatx_open_device(fs, path, offset, size, sector_size, sectors_per_cluster))
    {
        return FATX_STATUS_ERROR;
    }

    if (fatx_write_superblock(fs))
    {
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    if (fatx_init_fat(fs))
    {
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    if (fatx_init_root(fs))
    {
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    retval = FATX_STATUS_SUCCESS;

cleanup:
    fatx_close_device(fs);
    return retval;
}

/*
 * Write refurb sector.
 */
int fatx_disk_write_refurb_info(char const *path, uint32_t number_of_boots, uint64_t first_power_on)
{
    struct fatx_refurb_info refurb_info;
    FILE * device;
    int retval;

    device = fopen(path, "r+b");
    if (!device)
    {
        fprintf(stderr, "failed to open %s to write refurb info\n", path);
        return FATX_STATUS_ERROR;
    }

    if (fseek(device, FATX_REFURB_OFFSET, SEEK_CUR))
    {
        fprintf(stderr, "failed to seek to the refurb info (offset 0x%x)\n", FATX_REFURB_OFFSET);
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    memset(&refurb_info, 0, sizeof(struct fatx_refurb_info));
    refurb_info.signature = FATX_REFURB_SIGNATURE;
    refurb_info.number_of_boots = number_of_boots;
    refurb_info.first_power_on = first_power_on;

    if (fwrite(&refurb_info, sizeof(struct fatx_refurb_info), 1, device) != 1)
    {
        fprintf(stderr, "failed to write refurb info\n");
        retval = FATX_STATUS_ERROR;
        goto cleanup;
    }

    retval = FATX_STATUS_SUCCESS;

cleanup:
    fclose(device);
    return retval;
}