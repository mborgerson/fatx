fatxfs
======
**fatxfs** is a userspace filesystem driver for the FATX filesystem, a varient
of FAT16/32 developed by Microsoft for the original Xbox console.

Currently only read access is implemented.

**Warning:** fatxfs is currently in a **very** experimental state. Please use
with caution.

Platform Support
----------------
Currently targeting Linux-based systems, specifically Ubuntu 14.04 and OS X
(10.10).

How to Build
------------

### Prerequisites

#### Ubuntu
Assuming you already have typical build tools installed, install FUSE and CMake:

    sudo apt-get install libfuse-dev cmake

#### OS X
Assuming you already have typical build tools installed, install pkgconfig,
FUSE, and CMake.

Install FUSE for OS X:
* Download the latest FUSE DMG image file at the [FUSE for OS X](https://osxfuse.github.io/) homepage.
* Mount the DMG and run the installer.
* Restart.

Install pkgconfig and cmake (via homebrew):

    brew install install pkgconfig cmake

### Download Source
Clone the repository:

    git clone https://github.com/mborgerson/fatx && cd fatx

### Build
Create a build directory and run CMake to construct the Makefiles:

    mkdir build && cd build
    cmake ..

Finally, start the build:

    make

How to Use
----------
Firstly, you will need an image or block device to mount. Then, you can simply
create a mountpoint and mount the "C drive" (default behavior):

    mkdir c_drive
    ./fatxfs /dev/nbd0 c_drive
    ls c_drive

You can specify the drive letter of the partition to mount:

    ./fatxfs /dev/nbd0 e_drive --drive=e

Or, you can specify the offset and size of the partition manually:

    ./fatxfs /dev/nbd0 c_drive --offset=0x8ca80000 --size=0x01f400000

Tips
----
### Mounting a QCOW2 Image
If you have a qcow image, you can mount the qcow image as a network block device
before mounting a partition on the device:

    sudo apt-get install qemu-utils
    sudo modprobe nbd max_part=8
    sudo qemu-nbd --connect=/dev/nbd0 /path/to/your/image.qcow2

Unfortunately, on OS X, there is not a way to mount a qcow image like this.
I recommend converting the qcow image to a raw disk image.

    qemu-img convert /path/to/image.qcow /path/to/output.raw

License
-------
fatxfs is licensed under the terms of the GNU General Public License (v2, or
later). See LICENSE.txt for full license text.

Credit
------
This project was made possible by the research done by Andrew de Quincey, Lucien
Murray-Pitts, and Michael Steil. Thank you!