#!/usr/bin/env python3
#
# FATX Filesystem Library
#
# Copyright (C) 2026  Mijael Viricochea
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""
Build a minimal Xbox 360 (big-endian) FATX partition image.

This assembles the on-disk structures byte by byte from the format spec, rather
than by running libfatx's own writer. That is the whole point: an image produced
by the library's writer would round-trip through the library's reader even if a
field were left unswapped, because the same mistake would cancel out. An image
built independently here does not, so mounting it exercises whether every
multi-byte field is actually being byte-swapped.

Everything multi-byte is emitted big-endian ('>' struct prefix).
"""

import struct
import sys

SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 32
BYTES_PER_CLUSTER = SECTOR_SIZE * SECTORS_PER_CLUSTER  # 16 KiB
PARTITION_SIZE = 16 * 1024 * 1024                      # 16 MiB
SUPERBLOCK_SIZE = 4096
FAT_OFFSET = 4096
VOLUME_ID = 0xCAFEBABE
ROOT_CLUSTER = 1

DIRENT_SIZE = 64
MAX_FILENAME_LEN = 42

ATTR_DIRECTORY = 1 << 4
END_OF_DIR_MARKER = 0xFF

FAT16_END_OF_CHAIN = 0xFFFF
FAT16_MEDIA = 0xFFF8

# Timestamp fields are packed with the original Xbox epoch (2000). Whether the
# 360 uses 1980 instead is an open question that only a real disk can settle, so
# the accompanying test asserts on names, sizes and contents and treats
# timestamps as informational.
EPOCH = 2000


def pack_date(year, month, day):
    return ((day & 0x1F) | ((month & 0xF) << 5) | (((year - EPOCH) & 0x7F) << 9))


def pack_time(hour, minute, second):
    return (((hour & 0xF) << 11) | ((minute & 0x1F) << 5) | ((second // 2) & 0x1F))


DATE = pack_date(2026, 8, 11)
TIME = pack_time(14, 30, 0)


def superblock():
    """4096 bytes. Signature is 'XTAF' -- the bytes that read big-endian give
    the same 0x58544146 that 'FATX' gives when read little-endian."""
    sb = struct.pack(
        ">IIIIH",
        0x58544146,           # signature
        VOLUME_ID,            # volume_id
        SECTORS_PER_CLUSTER,  # sectors_per_cluster
        ROOT_CLUSTER,         # root_cluster
        0,                    # unknown1
    )
    assert sb[:4] == b"XTAF", "signature bytes should spell XTAF on disk"
    return sb + b"\xFF" * (SUPERBLOCK_SIZE - len(sb))


def dirent(name, attributes, first_cluster, file_size):
    name = name.encode("ascii")
    assert len(name) < MAX_FILENAME_LEN
    entry = struct.pack(">BB", len(name), attributes)
    entry += name + b"\xFF" * (MAX_FILENAME_LEN - len(name))
    entry += struct.pack(
        ">IIHHHHHH",
        first_cluster,
        file_size,
        TIME, DATE,   # modified
        TIME, DATE,   # created
        TIME, DATE,   # accessed
    )
    assert len(entry) == DIRENT_SIZE, len(entry)
    return entry


def build():
    # Geometry, mirroring what fatx_open_device derives.
    fat_entries = PARTITION_SIZE // BYTES_PER_CLUSTER + 1
    assert fat_entries < 0xFFF0, "expected a FAT16 filesystem for this test"
    fat_size = fat_entries * 2
    if fat_size % 4096:
        fat_size += 4096 - fat_size % 4096
    cluster_offset = FAT_OFFSET + fat_size

    image = bytearray(b"\x00" * PARTITION_SIZE)

    # Superblock.
    image[0:SUPERBLOCK_SIZE] = superblock()

    # Contents. Cluster 1 is the root directory.
    hello = b"Hello from a big-endian Xbox 360 filesystem.\n"
    cover = bytes(range(256)) * 8  # 2048 bytes of non-trivial, checkable data

    files = {
        2: hello,   # /HELLO.TXT
        4: cover,   # /GAMES/COVER.JPG
    }

    # FAT16. Entry 0 is the media descriptor; every file and directory here is a
    # single cluster, so each of their entries is an end-of-chain marker.
    fat = bytearray(fat_size)
    struct.pack_into(">H", fat, 0, FAT16_MEDIA)
    for cluster in (1, 2, 3, 4):
        struct.pack_into(">H", fat, cluster * 2, FAT16_END_OF_CHAIN)
    image[FAT_OFFSET:FAT_OFFSET + fat_size] = fat

    def cluster_at(n):
        return cluster_offset + (n - 1) * BYTES_PER_CLUSTER

    # Root directory (cluster 1).
    root = dirent("HELLO.TXT", 0, 2, len(hello))
    root += dirent("GAMES", ATTR_DIRECTORY, 3, 0)
    root += bytes([END_OF_DIR_MARKER])
    image[cluster_at(1):cluster_at(1) + len(root)] = root

    # /GAMES directory (cluster 3).
    games = dirent("COVER.JPG", 0, 4, len(cover))
    games += bytes([END_OF_DIR_MARKER])
    image[cluster_at(3):cluster_at(3) + len(games)] = games

    # File data.
    for cluster, data in files.items():
        image[cluster_at(cluster):cluster_at(cluster) + len(data)] = data

    return bytes(image), {"/HELLO.TXT": hello, "/GAMES/COVER.JPG": cover}


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <output image>", file=sys.stderr)
        return 1

    image, contents = build()
    with open(sys.argv[1], "wb") as f:
        f.write(image)

    # Emit the expected listing for the test script to diff against.
    for path, data in sorted(contents.items()):
        print(f"{path} {len(data)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
