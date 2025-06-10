fatx
====

Original Xbox FATX Filesystem Library, Python bindings, FUSE driver, and GUI explorer.

* [**libfatx**](#libfatx) is a C library for working with the FATX filesystem, a variant of FAT16/32 developed by Microsoft for the original Xbox console.
* [**fatxfs**](#fatxfs) is a [FUSE driver](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) built using libfatx.
* [**pyfatx**](#pyfatx) is a Python module providing bindings to libfatx.
* [**gfatx**](#gfatx) is a graphical utility for working with FATX disk images, built around libfatx.

libfatx
-------
**libfatx** provides both read and write access to FATX filesystems, and formatting disks. Large disks are supported via F partition.

License
-------
See LICENSE.txt

Credit
------
This project was made possible in part by the research done by Andrew de Quincey, Lucien Murray-Pitts, and Michael Steil. Thank you!
