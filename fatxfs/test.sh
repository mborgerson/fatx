#!/bin/bash
set -ex

function checksum {
	sha256sum $1 | cut -f1 -d' '
}

mkdir -p c

# Format the disk
if [[ "$(uname)" == "Darwin" ]]; then
	truncate -s 8g xbox_hdd.img
else
	fallocate -l 8G xbox_hdd.img
fi
fatxfs --format=retail --destroy-all-existing-data xbox_hdd.img c
sleep 1

# Copy a file in
fatxfs --log=log.txt --loglevel=100 xbox_hdd.img c
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
