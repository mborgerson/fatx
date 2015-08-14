fatxfs
======
**fatxfs** is a userspace filesystem driver for the FATX filesystem, a varient
of FAT16/32 developed by Microsoft for the original Xbox console.

Currently only read access is implemented.

**Warning:** fatxfs is currently in a **very** experimental state. Please use
with caution.

Platform Support
----------------
Currently targeting Linux-based systems, specifically Ubuntu 14.04. Because
fatxfs employs the FUSE API, it should be relatively straightforward to port to
other platforms.

How to Build
------------
Assuming you already have typical build tools installed, install FUSE and CMake:

    sudo apt-get install libfuse-dev cmake

Clone the repository:

    git clone https://github.com/mborgerson/fatx && cd fatx

Create a build directory and run CMake to construct the Makefiles:

    mkdir build && cd build
    cmake ..

Build:

    make

How to Use
----------
Firstly, you will need an image or block device to mount.

If you have a qcow image, you can mount the qcow image as a network block device
before mounting a partition on the device:

    sudo apt-get install qemu-utils
    sudo modprobe nbd max_part=8
    sudo qemu-nbd --connect=/dev/nbd0 /path/to/your/image.qcow2

Finally, create a mountpoint and mount the "C drive" (default behavior):

    mkdir c_drive
    ./fatxfs /dev/nbd0 c_drive
    ls c_drive

You can specify the drive letter of the partition to mount:

    ./fatxfs /dev/nbd0 e_drive --drive=e

Or, you can specify the offset and size of the partition manually:

    ./fatxfs /dev/nbd0 c_drive --offset=0x8ca80000 --size=0x01f400000

License
-------
fatxfs is licensed under the terms of the GNU General Public License (v2, or
later). See LICENSE.txt for full license text.

Credit
------
This project was made possible by the research done by Andrew de Quincey, Lucien
Murray-Pitts, and Michael Steil. Thank you!