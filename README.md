fatxfs
======
**fatxfs** is a userspace filesystem driver for the FATX filesystem, a varient
of FAT16/32 developed by Microsoft for the original Xbox console.

Currently `fatxfs` provides only read access. Write functionality is in the
works.

Platform Support
----------------
Currently targeting:
* Linux-based systems, specifically Ubuntu 14.04, and
* OS X 10.10.

How to Build
------------
### Prerequisites

#### Ubuntu
Assuming you already have typical build tools installed, install FUSE and CMake:

    $ sudo apt-get install libfuse-dev cmake

#### OS X
Download Xcode (available from the App Store) to get command line tools.

Assuming you have homebrew installed, install `pkgconfig` and `cmake`:

    $ brew install install pkgconfig cmake

Install FUSE for OS X:
* Download the latest FUSE DMG image file at the [FUSE for OS X homepage](https://osxfuse.github.io/).
* Mount the DMG and run the installer.
* Restart your system.

### Download Source
Clone the repository:

    $ git clone https://github.com/mborgerson/fatx && cd fatx

### Build
Create a build directory and run `cmake` to construct the Makefiles:

    $ mkdir build && cd build
    $ cmake ..

Finally, start the build:

    $ make

How to Use
----------
Firstly, you will need an image or block device to mount. Then, you can simply
create a mountpoint and mount the "C drive" (default behavior). For example:

    $ mkdir c_drive
    $ ./fatxfs /dev/nbd0 c_drive
    $ ls c_drive

You can specify the drive letter of the partition to mount:

    $ ./fatxfs /dev/nbd0 e_drive --drive=e

Or, you can specify the offset and size of the partition manually:

    $ ./fatxfs /dev/nbd0 c_drive --offset=0x8ca80000 --size=0x01f400000

Tips
----
### Mounting a QCOW2 Image
If you have a qcow image, you can mount the qcow image as a network block device
before mounting a partition on the device:

    $ sudo apt-get install qemu-utils
    $ sudo modprobe nbd max_part=8
    $ sudo qemu-nbd --connect=/dev/nbd0 /path/to/your/image.qcow2
    $ sudo chmod a+rwx /dev/nbd0

Unfortunately, on OS X, there is not a way to mount a qcow image like this.
I recommend converting the qcow image to a raw disk image.

    $ qemu-img convert /path/to/image.qcow /path/to/output.raw

### Logging
For debug purposes, you can have `fatxfs` log operations to a file given by the
`--log` option. You can control the amount of output using the `--loglevel`
option.

License
-------

    Copyright (C) 2015  Matt Borgerson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

Credit
------
This project was made possible in part by the research done by Andrew de
Quincey, Lucien Murray-Pitts, and Michael Steil. Thank you!