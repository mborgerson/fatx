fatxfs
======
**fatxfs** is a userspace filesystem driver for the FATX filesystem (developed
by Microsoft for the original Xbox console). fatxfs uses the Filesystem in
USErspace (FUSE) API and as such is compatible with a number of its
implementations.

Currently only read access is implemented.

*Warning:* fatxfs is currently in a **very** experimental state. Please use with
caution.

How to Build
------------
Install FUSE, CMake:

    sudo apt-get install libfuse-dev cmake

Clone source:

    git clone https://github.com/mborgerson/fatx
    cd fatx

Create build directory and build makefiles:

    mkdir build
    cmake ..

Build:

    make

How to Use
----------
Get your drive or partition image.

If your image is on a qcow image, you can mount it like so:

    sudo apt-get install qemu-utils
    sudo modprobe nbd max_part=8
    sudo qemu-nbd --connect=/dev/nbd0 /mnt/MY-DISK.qcow2

Now create a mountpoint and mount the "C drive":

    mkdir mnt
    ./fatxfs /dev/nbd0 mnt --drive=c
    ls mnt

License
-------
fatxfs is licensed under the terms of the GNU General Public License (v3 or
later). See COPYING.txt for full license text.

Credit
------
This project was made possible by the research done by Andrew de Quincey, Lucien
Murray-Pitts, and Michael Steil. Thank you!