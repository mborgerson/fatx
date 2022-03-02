#!/bin/bash
set -ex

function checksum {
	sha256sum $1 | cut -f1 -d' '
}

tar xvf xbox_hdd.img.tgz
mkdir -p c
fatxfs --log=log.txt --loglevel=100 xbox_hdd.img c
[[ $(checksum c/xboxdash.xbe) = "338e6e203d9f1db5f2c1d976b8969af42049c32e1a65ac0347fbc6efcf5bd7c6" ]]

# Copy a file in
SRC_PATH=$(which fatxfs)
DST_PATH="c/a/b/c/d/randfile.bin"

mkdir -p $(dirname $DST_PATH)
cp $SRC_PATH $DST_PATH
sleep 1
fusermount -u c

# Mount and verify contents
fatxfs xbox_hdd.img c
[[ $(checksum $DST_PATH) = $(checksum $SRC_PATH) ]]
sleep 1
fusermount -u c
