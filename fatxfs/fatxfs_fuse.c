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

#define _XOPEN_SOURCE 600

#include "fatx.h"
#include "fatx_version.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>

/* Define the desired FUSE API (required before including fuse.h) */
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <fuse_opt.h>

#include <time.h>

enum {
    FATX_FUSE_OPT_KEY_HELP,
    FATX_FUSE_OPT_KEY_VERSION,
    FATX_FUSE_OPT_KEY_DRIVE,
    FATX_FUSE_OPT_KEY_OFFSET,
    FATX_FUSE_OPT_KEY_SIZE,
    FATX_FUSE_OPT_KEY_SECTOR_SIZE,
    FATX_FUSE_OPT_KEY_SECTORS_PER_CLUSTER,
    FATX_FUSE_OPT_KEY_FORMAT,
    FATX_FUSE_OPT_KEY_DESTROY_DATA,
    FATX_FUSE_OPT_KEY_LOG,
    FATX_FUSE_OPT_KEY_LOGLEVEL,
};

struct fatx_fuse_private_data {
    struct fatx_fs   *fs;
    char const       *device_path;
    char const       *log_path;
    char             *mount_point;
    char              mount_partition_drive;
    size_t            mount_partition_offset;
    size_t            mount_partition_size;
    size_t            device_sector_size;
    size_t            device_sectors_per_cluster;
    FILE             *log_handle;
    int               log_level;
    enum fatx_format  format;
    int               format_confirm;
};

/*
 * Filesystem operation functions.
 */
int fatx_fuse_get_attr(const char *path, struct stat *stbuf);
int fatx_fuse_mkdir(const char *path, mode_t mode);
int fatx_fuse_rmdir(const char *path);
int fatx_fuse_mknod(const char *path, mode_t mode, dev_t dev);
int fatx_fuse_open(const char *path, struct fuse_file_info *fi);
int fatx_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int fatx_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int fatx_fuse_read_dir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int fatx_fuse_unlink(char const *path);
int fatx_fuse_truncate(const char *path, off_t size);
int fatx_fuse_rename(const char *from, const char *to);
int fatx_fuse_utimens(const char *path, const struct timespec ts[2]);
void *fatx_fuse_init(struct fuse_conn_info *conn);
void fatx_fuse_destroy(void *data);

/*
 * Command line processing functions.
 */
char const *fatx_fuse_opt_consume_key(char const *arg);
int fatx_fuse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs);

/*
 * Helper functions.
 */
struct fatx_fuse_private_data *fatx_fuse_get_private_data(void);
void fatx_fuse_print_usage(void);
void fatx_fuse_print_version(void);

/* Define the operations supported by this filesystem */
static struct fuse_operations fatx_fuse_oper = {
    .destroy  = fatx_fuse_destroy,
    .getattr  = fatx_fuse_get_attr,
    .init     = fatx_fuse_init,
    .mkdir    = fatx_fuse_mkdir,
    .rmdir    = fatx_fuse_rmdir,
    .mknod    = fatx_fuse_mknod,
    .open     = fatx_fuse_open,
    .read     = fatx_fuse_read,
    .write    = fatx_fuse_write,
    .readdir  = fatx_fuse_read_dir,
    .unlink   = fatx_fuse_unlink,
    .truncate = fatx_fuse_truncate,
    .rename   = fatx_fuse_rename,
    .utimens  = fatx_fuse_utimens,
};

/*
 * Simple convenince function to get the private data struct.
 */
struct fatx_fuse_private_data *fatx_fuse_get_private_data(void)
{
    struct fuse_context *context;
    context = fuse_get_context();
    if (context == NULL) return NULL;
    return (struct fatx_fuse_private_data *)(context->private_data);
}

/*
 * Initialize the filesystem
 */
void *fatx_fuse_init(struct fuse_conn_info *conn)
{
    return fatx_fuse_get_private_data();
}

/*
 * Clean up the filesystem.
 */
void fatx_fuse_destroy(void *data)
{
    struct fatx_fuse_private_data *pd = data;

    fatx_close_device(pd->fs);
    free(pd->fs);
    pd->fs = NULL;

    free(pd->mount_point);

    if (pd->log_handle)
    {
        fclose(pd->log_handle);
    }
}

void fatx_attr_to_stat(const struct fatx_attr *attr, struct stat *stbuf)
{
    struct fuse_context *context;
    context = fuse_get_context();

    stbuf->st_mode   = 0777;
    stbuf->st_nlink  = 1;
    stbuf->st_size   = attr->file_size;
    stbuf->st_mtime  = fatx_ts_to_time_t(&(attr->modified));
    stbuf->st_atime  = fatx_ts_to_time_t(&(attr->accessed));
    stbuf->st_ctime  = fatx_ts_to_time_t(&(attr->created));

    if (attr->attributes & FATX_ATTR_DIRECTORY)
    {
        stbuf->st_mode |= S_IFDIR;
    }
    else
    {
        stbuf->st_mode |= S_IFREG;
    }


    stbuf->st_uid = context->uid;
    stbuf->st_gid = context->gid;
}

/*
 * Read the next directory entry.
 */
int fatx_fuse_read_dir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct fatx_fuse_private_data *pd;
    struct fatx_dirent dirent, *nextdirent;
    struct fatx_attr attr;
    struct fatx_dir dir;
    struct stat stat_buf;
    struct fuse_context *context;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_read_dir(path=\"%s\", buf=0x%p, offset=0x%zx)\n", path, buf, offset);

    /* Return record for '.' */
    memset(&stat_buf, 0, sizeof(stat_buf));
    if (strcmp(path, "/") != 0)
    {
        status = fatx_get_attr(pd->fs, path, &attr);
        if (status) return status;

        fatx_attr_to_stat(&attr, &stat_buf);
    }
    
    status = filler(buf, ".", &stat_buf, 0);
    if (status) return -ENOMEM;

    /* Return record for '..' */
    if (strcmp(path, "/") == 0)
    {
        /* On the root path, stat the mount point's parent */
        char *parent = fatx_dirname(pd->mount_point);
        status = stat(parent, &stat_buf);
        free(parent);
        if (status) return -errno;
    }
    else
    {
        /* If not the root, stat the fs (or handle the root parent case) */
        char *parent = fatx_dirname(path);
        memset(&stat_buf, 0, sizeof(stat_buf));
        if (strcmp(parent, "/") != 0)
        {
            status = fatx_get_attr(pd->fs, parent, &attr);
            free(parent);
            if (status) return status;

            fatx_attr_to_stat(&attr, &stat_buf);
        }
        else
        {
            context = fuse_get_context();
            stat_buf.st_uid = context->uid;
            stat_buf.st_gid = context->gid;
            stat_buf.st_mode = S_IFDIR | 0777;
            stat_buf.st_nlink = 1;
            free(parent);
        }
    }

    status = filler(buf, "..", &stat_buf, 0);
    if (status) return -ENOMEM;

    /* Open the directory. */
    status = fatx_open_dir(pd->fs, path, &dir);
    if (status) return status;

    /* Iterate over directory entries, calling filler() for each. */
    while (1)
    {
        /* Get the current directory entry. */
        status = fatx_read_dir(pd->fs, &dir, &dirent, &attr, &nextdirent);

        if (status == FATX_STATUS_SUCCESS)
        {
            memset(&stat_buf, 0, sizeof(stat_buf));
            fatx_attr_to_stat(&attr, &stat_buf);

            /* Found a directory entry, use the filler function to add it. */
            status = filler(buf, nextdirent->filename, &stat_buf, 0);

            if (status)
            {
                /* Could not add directory entry. */
                status = -ENOMEM;
                break;
            }
        }
        else if (status == FATX_STATUS_FILE_DELETED)
        {
            /* File deleted. Skip over it... */
        }
        else if (status == FATX_STATUS_END_OF_DIR)
        {
            /* End of directory entries. */
            status = 0;
            break;
        }
        else
        {
            /* Error */
            break;
        }

        /* Get the next directory entry. */
        status = fatx_next_dir_entry(pd->fs, &dir);
        if (status != FATX_STATUS_SUCCESS) break;
    }

    fatx_close_dir(pd->fs, &dir);
    return status;
}

/*
 * Get file attributes.
 */
int fatx_fuse_get_attr(const char  *path, struct stat *stbuf)
{
    struct fatx_fuse_private_data *pd;
    struct fatx_attr attr;
    struct fuse_context *context;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_get_attr(path=\"%s\")\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0)
    {

        context = fuse_get_context();
        stbuf->st_uid = context->uid;
        stbuf->st_gid = context->gid;
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 1;
        return 0;
    }

    status = fatx_get_attr(pd->fs, path, &attr);

    switch (status)
    {
    case FATX_STATUS_SUCCESS:
        break;

    case FATX_STATUS_FILE_NOT_FOUND:
        return -ENOENT;

    default:
        return -1;
    }

    fatx_attr_to_stat(&attr, stbuf);

    return 0;
}

/*
 * Open a file.
 */
int fatx_fuse_open(const char *path, struct fuse_file_info *fi)
{
    struct fatx_fuse_private_data *pd;
    struct fatx_attr attr;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_open(path=\"%s\")\n", path);

    if((fi->flags & O_CREAT) == O_CREAT)
    {
        status = fatx_mknod(pd->fs, path);
        if (status) return -ENFILE;
    }

    status = fatx_get_attr(pd->fs, path, &attr);

    switch (status)
    {
    case FATX_STATUS_SUCCESS:
        return 0;

    case FATX_STATUS_FILE_NOT_FOUND:
        return -ENOENT;

    default:
        return -1;
    }
}

/*
 * Read from a file.
 */
int fatx_fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fatx_fuse_private_data *pd;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_read(path=\"%s\", buf=0x%p, size=0x%zx, offset=0x%zx)\n", path, (void*)buf, size, offset);

    return fatx_read(pd->fs, path, offset, size, buf);
}

/*
 * Write to a file.
 */
int fatx_fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fatx_fuse_private_data *pd;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_write(path=\"%s\", buf=0x%p, size=0x%zx, offset=0x%zx)\n", path, (void*)buf, size, offset);

    return fatx_write(pd->fs, path, offset, size, buf);
}

/*
 * Remove a file.
 */
int fatx_fuse_unlink(char const *path)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_unlink(path=\"%s\")\n", path);
    status = fatx_unlink(pd->fs, path);

    switch (status)
    {
    case FATX_STATUS_SUCCESS:
        return 0;

    case FATX_STATUS_FILE_NOT_FOUND:
        return -ENOENT;

    default:
        return -1;
    }
}

/*
 * Create a directory.
 */
int fatx_fuse_mkdir(const char *path, mode_t mode)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_mkdir(path=\"%s\", mode=0%o)\n", path, mode);

    status = fatx_mkdir(pd->fs, path);
    return (status == FATX_STATUS_SUCCESS ? 0 : -1);
}

/*
 * Remove a directory.
 */
int fatx_fuse_rmdir(const char *path)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_mkdir(path=\"%s\")\n", path);

    status = fatx_rmdir(pd->fs, path);
    switch (status)
    {
    case FATX_STATUS_SUCCESS:
        return 0;
    case FATX_STATUS_END_OF_DIR:
        return -ENOTEMPTY;
    case FATX_STATUS_ERROR:
    default:
        return -1;
    }
}

/*
 * Create a file.
 */
int fatx_fuse_mknod(const char *path, mode_t mode, dev_t dev)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if (pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_mknod(path=\"%s\", mode=0%o, dev=0x%x)\n", path, mode, dev);

    status = fatx_mknod(pd->fs, path);
    return (status == FATX_STATUS_SUCCESS ? 0 : -1);
}

/*
 * Truncate a file.
 */
int fatx_fuse_truncate(const char *path, off_t size)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if(pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_truncate(path=\"%s\", size=0x%x)\n", path, size);

    status = fatx_truncate(pd->fs, path, size);
    return (status == FATX_STATUS_SUCCESS ? 0 : -1);
}

/*
 * Rename a file.
 */
int fatx_fuse_rename(const char *from, const char *to)
{
    struct fatx_fuse_private_data *pd;
    int status;

    pd = fatx_fuse_get_private_data();
    if(pd == NULL) return -1;

    fatx_debug(pd->fs, "fatx_fuse_rename(from=\"%s\", to=\"%s\")\n", from, to);

    status = fatx_rename(pd->fs, from, to, false, false);
    return (status == FATX_STATUS_SUCCESS ? 0 : -1);
}

/*
 * Set access and modification time for a file
 */
int fatx_fuse_utimens(const char *path, const struct timespec ts[2])
{
    struct fatx_fuse_private_data *pd;
    struct fatx_ts fat_time[2];
    int status;

    pd = fatx_fuse_get_private_data();
    if(pd == NULL) return -1;

    fatx_time_t_to_fatx_ts(ts[0].tv_sec, &(fat_time[0]));
    fatx_time_t_to_fatx_ts(ts[1].tv_sec, &(fat_time[1]));
    status = fatx_utime(pd->fs, path, fat_time);
    return (status == FATX_STATUS_SUCCESS ? 0 : -1);
}

/*
 * Given a string of the form --key=value, return a pointer discarding --key=.
 */
char const *fatx_fuse_opt_consume_key(char const *arg)
{
    for (; *arg != '\0'; ++arg)
    {
        if (*arg == '=')
        {
            /* Found = in --key=. Skip over it and return the ptr. */
            return ++arg;
        }
    }

    /* Reached end of string. */
    return arg;
}

/*
 * Option process routine for FUSE argument parser.
 */
int fatx_fuse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    struct fatx_fuse_private_data *pd = data;

    switch (key)
    {
    case FATX_FUSE_OPT_KEY_HELP:
        fatx_fuse_print_usage();
        return -1;

    case FATX_FUSE_OPT_KEY_VERSION:
        fatx_fuse_print_version();
        return -1;

    case FUSE_OPT_KEY_NONOPT:
        /* Consume first non option arg (device path) */
        if (pd->device_path == NULL)
        {
            pd->device_path = arg;
            return 0;
        }
        /* Copy second non option arg (mount point) */
        else if (pd->mount_point == NULL)
        {
            pd->mount_point = malloc(strlen(arg)+1);
            strcpy(pd->mount_point, arg);
            /* Pass it to FUSE */
            return 1;
        }
        else
        {
            /* Pass it on to FUSE. */
            return 1;
        }
        break;

    case FATX_FUSE_OPT_KEY_DRIVE:
        arg = fatx_fuse_opt_consume_key(arg);
        if (strlen(arg) != 1)
        {
            fprintf(stderr, "invalid drive letter\n");
            return -1;
        }
        pd->mount_partition_drive = arg[0];
        return 0;

    case FATX_FUSE_OPT_KEY_OFFSET:
        arg = fatx_fuse_opt_consume_key(arg);
        pd->mount_partition_offset = strtol(arg, NULL, 0);
        return 0;

    case FATX_FUSE_OPT_KEY_SIZE:
        arg = fatx_fuse_opt_consume_key(arg);
        pd->mount_partition_size = strtol(arg, NULL, 0);
        return 0;

    case FATX_FUSE_OPT_KEY_SECTOR_SIZE:
        arg = fatx_fuse_opt_consume_key(arg);
        pd->device_sector_size = strtol(arg, NULL, 0);
        return 0;

    case FATX_FUSE_OPT_KEY_SECTORS_PER_CLUSTER:
        arg = fatx_fuse_opt_consume_key(arg);
        pd->device_sectors_per_cluster = strtol(arg, NULL, 0);
        return 0;

    case FATX_FUSE_OPT_KEY_FORMAT:
        arg = fatx_fuse_opt_consume_key(arg);
        if (!strcmp(arg, "retail"))
        {
            pd->format = FATX_FORMAT_RETAIL;
        }
        else if (!strcmp(arg, "f-takes-all"))
        {
            pd->format = FATX_FORMAT_F_TAKES_ALL;
        }
        else
        {
            fprintf(stderr, "invalid format '%s' specified\n", arg);
            return -1;
        }
        return 0;

    case FATX_FUSE_OPT_KEY_DESTROY_DATA:
        pd->format_confirm = 1;
        return 0;

    case FATX_FUSE_OPT_KEY_LOG:
        pd->log_path = fatx_fuse_opt_consume_key(arg);
        return 0;

    case FATX_FUSE_OPT_KEY_LOGLEVEL:
        arg = fatx_fuse_opt_consume_key(arg);
        pd->log_level = strtol(arg, NULL, 0);
        return 0;

    default:
        /* Pass it on to FUSE. */
        return 1;
    }
    return 0;
}

char *prog_short_name = "fatxfs";

/*
 * Print program version.
 */
void fatx_fuse_print_version(void)
{
    fprintf(stderr, "FATXFS Version %d.%d.%d\n", FATX_VERSION_MAJ,
                                                 FATX_VERSION_MIN,
                                                 FATX_VERSION_BLD);
    fprintf(stderr, "Copyright (c) %d  Matt Borgerson\n", FATX_COPYRIGHT_YEAR);
}

/*
 * Print program usage.
 */
void fatx_fuse_print_usage(void)
{
    char *argv[2];

    /* Print basic usage */
    fprintf(stderr, "FATXFS - Userspace FATX Filesystem Driver\n\n");
    fprintf(stderr, "Usage: %s <device> <mountpoint> [<options>]\n", prog_short_name);
    fprintf(stderr, "   or: %s <device> <mountpoint> --drive=c|e|x|y|z|f [<options>]\n", prog_short_name);
    fprintf(stderr, "   or: %s <device> <mountpoint> --offset=<offset> --size=<size> [<options>]\n\n", prog_short_name);
    fprintf(stderr, "General options:\n"
                    "    -o opt, [opt...]               mount options\n"
                    "    -h --help                      print help\n"
                    "    -V --version                   print version\n\n"
                    "FATXFS options:\n"
                    "    --drive=<letter>               mount a partition by its drive letter\n"
                    "    --offset=<offset>              specify the offset (in bytes) of a partition manually\n"
                    "    --size=<size>                  specify the size (in bytes) of a partition manually\n"
                    "    --sector-size=<size>           specify the size (in bytes) of a device sector (default is 512)\n"
                    "    --log=<log path>               enable fatxfs logging\n"
                    "    --loglevel=<level>             control the log output level (a higher value yields more output)\n\n"
                    "Disk formatting options:\n"
                    "    --format=<format>              specify the format (retail, f-takes-all) to initialize the device to\n"
                    "    --sectors-per-cluster=<size>   specify the sectors per cluster when initializing non-retail partitions (default is 128)\n"
                    "    --destroy-all-existing-data    acknowledge that device formatting will destroy all existing data\n\n");

    /* Print FUSE options */
    argv[0] = prog_short_name;
    argv[1] = "-ho";
    fuse_main(2, argv, &fatx_fuse_oper, NULL);
}

/*
 * Program entry point.
 */
int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fatx_fuse_private_data pd;
    int status;

    prog_short_name = basename(argv[0]);

    /* Define command line options. */
    struct fuse_opt const opts [] = {
        FUSE_OPT_KEY("-h",                           FATX_FUSE_OPT_KEY_HELP),
        FUSE_OPT_KEY("--help",                       FATX_FUSE_OPT_KEY_HELP),
        FUSE_OPT_KEY("-V",                           FATX_FUSE_OPT_KEY_VERSION),
        FUSE_OPT_KEY("--version",                    FATX_FUSE_OPT_KEY_VERSION),
        FUSE_OPT_KEY("--drive=",                     FATX_FUSE_OPT_KEY_DRIVE),
        FUSE_OPT_KEY("--offset=",                    FATX_FUSE_OPT_KEY_OFFSET),
        FUSE_OPT_KEY("--size=",                      FATX_FUSE_OPT_KEY_SIZE),
        FUSE_OPT_KEY("--sector-size=",               FATX_FUSE_OPT_KEY_SECTOR_SIZE),
        FUSE_OPT_KEY("--format=" ,                   FATX_FUSE_OPT_KEY_FORMAT),
        FUSE_OPT_KEY("--sectors-per-cluster=" ,      FATX_FUSE_OPT_KEY_SECTORS_PER_CLUSTER),
        FUSE_OPT_KEY("--destroy-all-existing-data",  FATX_FUSE_OPT_KEY_DESTROY_DATA),
        FUSE_OPT_KEY("--log=",                       FATX_FUSE_OPT_KEY_LOG),
        FUSE_OPT_KEY("--loglevel=",                  FATX_FUSE_OPT_KEY_LOGLEVEL),
        FUSE_OPT_END,
    };

    /* Initialize private data. */
    memset(&pd, 0, sizeof(struct fatx_fuse_private_data));
    pd.mount_partition_size       = -1;
    pd.mount_partition_offset     = -1;
    pd.device_sector_size         = 512;
    pd.device_sectors_per_cluster = 128;
    pd.log_level                  = FATX_LOG_LEVEL_INFO;

    /* Parse command line arguments. */
    if (fuse_opt_parse(&args, &pd, opts, &fatx_fuse_opt_proc) != 0)
    {
        return -1;
    }

    /* Check */
    if (pd.device_path == NULL)
    {
        fprintf(stderr, "please specify device path\n");
        goto error_nofs;
    }

    if (pd.mount_partition_offset != -1 || pd.mount_partition_size != -1)
    {
        /* Partition Specified Manually */

        if (pd.mount_partition_drive != 0x00)
        {
            fprintf(stderr, "--drive cannot be used with --offset or --size\n");
            goto error_nofs;
        }

        if (pd.mount_partition_offset == -1)
        {
            fprintf(stderr, "please specify partition offset\n");
            goto error_nofs;
        }

        if (pd.mount_partition_size == -1)
        {
            fprintf(stderr, "please specify partition size\n");
            goto error_nofs;
        }
    }
    else
    {
        /* Drive Letter Specified */
        if (pd.mount_partition_drive == 0x00)
        {
            pd.mount_partition_drive = 'c';
        }

        status = fatx_drive_to_offset_size(pd.mount_partition_drive,
                                           &pd.mount_partition_offset,
                                           &pd.mount_partition_size);
        if (status)
        {
            fprintf(stderr, "unknown drive letter '%c'\n", pd.mount_partition_drive);
            goto error_nofs;
        }
    }

    pd.fs = malloc(sizeof(struct fatx_fs));
    if (pd.fs == NULL)
    {
        fprintf(stderr, "no memory\n");
        goto error_nofs;
    }

    /*
     * Open logfile (if desired)
     */
    if (pd.log_path != NULL)
    {
        /* Open log file and setup logging */
        pd.log_handle = fopen(pd.log_path, "wb+");
        if (pd.log_handle == NULL)
        {
            fprintf(stderr, "failed to open %s for writing\n", pd.log_path);
            goto error_fs;
        }
        setbuf(pd.log_handle, NULL);
        fatx_log_init(pd.fs, pd.log_handle, pd.log_level);
    }

    /* Reformat the drive (if desired) */
    if (pd.format)
    {
        if (pd.format_confirm)
        {
            return fatx_disk_format(pd.fs, pd.device_path, pd.device_sector_size, pd.format, pd.device_sectors_per_cluster);
        }
        else
        {
            fprintf(stderr, "please specify --destroy-all-existing-data to perform device formatting\n");
            goto error_fs;
        }
    }
    else if (pd.format_confirm)
    {
        fprintf(stderr, "--destroy-all-existing-data can only be used with --format\n");
        goto error_fs;
    }

    /* Open the device */
    status = fatx_open_device(pd.fs,
                              pd.device_path,
                              pd.mount_partition_offset,
                              pd.mount_partition_size,
                              pd.device_sector_size,
                              FATX_READ_FROM_SUPERBLOCK);
    if (status)
    {
        fprintf(stderr, "failed to initialize the filesystem\n");
        goto error_fs;
    }

    /* Force single threaded operation .*/
    fuse_opt_insert_arg(&args, 1, "-s");

    return fuse_main(args.argc, args.argv, &fatx_fuse_oper, &pd);

error_fs:
    if (pd.log_handle)
    {
        fclose(pd.log_handle);
        pd.log_handle = NULL;
    }

    free(pd.fs);
error_nofs:
    free(pd.mount_point);
    return -1;
}
