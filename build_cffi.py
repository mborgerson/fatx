#!/usr/bin/env python3
import os.path
import struct
import subprocess
import platform
from distutils.command.build_ext import build_ext


ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
SRC_DIR = os.path.join(ROOT_DIR, 'src')
BUILD_DIR = os.path.join(ROOT_DIR, 'build')


class FfiPreBuildExtension(build_ext):
    def pre_run(self, ext, ffi):
        try:
            out = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError('Please install CMake to build')

        cmake_config_args = [
            '-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON',
            '-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE',
            ]
        cmake_build_args = []
        if platform.system() == 'Windows':
            is_64b = (struct.calcsize("P")*8 == 64)
            cmake_config_args += ['-A', 'x64' if is_64b else 'Win32']
            cmake_build_args += ['--config', 'Release']

        # Build sleigh and csleigh library
        subprocess.check_call(['cmake', '-B', 'build'] + cmake_config_args, cwd=ROOT_DIR)
        subprocess.check_call(['cmake', '--build', 'build', '--parallel', '--verbose', '--target', 'fatx'] + cmake_build_args, cwd=ROOT_DIR)


def ffibuilder():
    from cffi import FFI
    ffi = FFI()
    ffi.set_source("pyfatx.libfatx",
        """
        #include <stdlib.h>
        #include <fatx.h>

        struct fatx_fs *pyfatx_open_helper(void)
        {
            struct fatx_fs *fs = malloc(sizeof(struct fatx_fs));
            if (!fs) return NULL;
            fatx_log_init(fs, stdout, 1);
            return fs;
        }

        """,
        libraries=['fatx'],
        include_dirs=[SRC_DIR],
        library_dirs=[BUILD_DIR])
    ffi.cdef("""
        struct fatxfs;

        struct fatx_dir {
            size_t cluster;
            size_t entry;
        };

        struct fatx_ts {
            uint16_t year;
            uint8_t  month;
            uint8_t  day;
            uint8_t  hour;
            uint8_t  minute;
            uint8_t  second;
        };

        struct fatx_dirent {
            char filename[42+1];
        };

        struct fatx_attr {
            char           filename[42+1];
            uint8_t        attributes;
            size_t         first_cluster;
            size_t         file_size;
            struct fatx_ts modified;
            struct fatx_ts created;
            struct fatx_ts accessed;
        };

        typedef long int off_t;

        struct fatx_fs *pyfatx_open_helper(void);

        int fatx_open_device(struct fatx_fs *fs, char const *path, size_t offset, size_t size, size_t sector_size, size_t sectors_per_cluster);
        int fatx_close_device(struct fatx_fs *fs);
        int fatx_open_dir(struct fatx_fs *fs, char const *path, struct fatx_dir *dir);
        int fatx_read_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr, struct fatx_dirent **result);
        int fatx_write_dir(struct fatx_fs *fs, struct fatx_dir *dir, struct fatx_dirent *entry, struct fatx_attr *attr);
        int fatx_next_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir);
        int fatx_alloc_dir_entry(struct fatx_fs *fs, struct fatx_dir *dir);
        int fatx_close_dir(struct fatx_fs *fs, struct fatx_dir *dir);
        int fatx_get_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
        int fatx_set_attr(struct fatx_fs *fs, char const *path, struct fatx_attr *attr);
        int fatx_utime(struct fatx_fs *fs, char const *path, struct fatx_ts ts[2]);
        int fatx_read(struct fatx_fs *fs, char const *path, off_t offset, size_t size, void *buf);
        int fatx_write(struct fatx_fs *fs, char const *path, off_t offset, size_t size, const void *buf);
        int fatx_create_dirent(struct fatx_fs *fs, char const *path, struct fatx_dir *dir, uint8_t attributes);
        int fatx_unlink(struct fatx_fs *fs, char const *path);
        int fatx_mkdir(struct fatx_fs *fs, char const *path);
        int fatx_rmdir(struct fatx_fs *fs, char const *path);
        int fatx_mknod(struct fatx_fs *fs, char const *path);
        int fatx_truncate(struct fatx_fs *fs, char const *path, off_t offset);
        int fatx_rename(struct fatx_fs *fs, char const *from, char const *to);
        """)
    return ffi

if __name__ == "__main__":
    ffibuilder().compile(verbose=True)
