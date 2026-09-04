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
Decode the directory entries of an Xbox 360 FATX image, independently.

This exists to check the *write* path. A filesystem driver that byte-swaps
consistently but wrongly will read back everything it wrote and look perfectly
healthy, so round-tripping through libfatx proves nothing about whether the
bytes on disk are what an Xbox 360 would actually write.

This parser shares no code with libfatx. It decodes straight from the format:
big-endian fields, timestamps with a 1980 epoch, standard FAT hour and minute
widths, and the date stored before the time in each pair.

Usage: verify_x360_dirents.py <image> [cluster-offset] [bytes-per-cluster]
"""

import calendar
import struct
import sys

DIRENT_SIZE = 64
END_MARKERS = (0x00, 0xFF)
DELETED_MARKER = 0xE5
ATTR_DIRECTORY = 0x10
EPOCH = 1980


def decode_date(raw):
    return (((raw >> 9) & 0x7F) + EPOCH, (raw >> 5) & 0xF, raw & 0x1F)


def decode_time(raw):
    return ((raw >> 11) & 0x1F, (raw >> 5) & 0x3F, (raw & 0x1F) * 2)


def plausible(y, mo, d, h, mi, s):
    if not (EPOCH <= y <= EPOCH + 127 and 1 <= mo <= 12 and 1 <= d <= 31):
        return False
    if d > calendar.monthrange(y, mo)[1]:
        return False
    return h <= 23 and mi <= 59 and s <= 59


def parse_dirents(data):
    """Yield (name, is_dir, first_cluster, size, timestamp, ok) per entry."""
    for i in range(len(data) // DIRENT_SIZE):
        entry = data[i * DIRENT_SIZE:(i + 1) * DIRENT_SIZE]
        name_len = entry[0]
        if name_len in END_MARKERS:
            return
        if name_len == DELETED_MARKER:
            continue

        attributes = entry[1]
        name = entry[2:2 + name_len].decode('ascii', 'replace')
        first_cluster, size = struct.unpack('>II', entry[44:52])
        # Date first, then time -- the opposite of the original Xbox.
        date_raw, time_raw = struct.unpack('>HH', entry[52:56])
        y, mo, d = decode_date(date_raw)
        h, mi, s = decode_time(time_raw)
        stamp = f'{y:04d}-{mo:02d}-{d:02d} {h:02d}:{mi:02d}:{s:02d}'
        yield (name, bool(attributes & ATTR_DIRECTORY), first_cluster, size,
               stamp, plausible(y, mo, d, h, mi, s))


def check_padding(data):
    """The filename tail must not carry leftover stack bytes."""
    bad = []
    for i in range(len(data) // DIRENT_SIZE):
        entry = data[i * DIRENT_SIZE:(i + 1) * DIRENT_SIZE]
        name_len = entry[0]
        if name_len in END_MARKERS:
            break
        if name_len == DELETED_MARKER:
            continue
        tail = set(entry[2 + name_len:44])
        if tail and not tail.issubset({0x00, 0xFF}):
            bad.append(entry[2:2 + name_len].decode('ascii', 'replace'))
    return bad


def main():
    if len(sys.argv) < 2:
        print(f'usage: {sys.argv[0]} <image> [cluster-offset] [bytes-per-cluster]',
              file=sys.stderr)
        return 2

    path = sys.argv[1]
    cluster_offset = int(sys.argv[2], 0) if len(sys.argv) > 2 else 8192
    bytes_per_cluster = int(sys.argv[3], 0) if len(sys.argv) > 3 else 16384

    with open(path, 'rb') as f:
        f.seek(cluster_offset)
        root = f.read(bytes_per_cluster)

    failures = 0
    print(f'{"name":<24} {"kind":<5} {"cluster":>8} {"size":>10}  timestamp')
    print('-' * 74)
    for name, is_dir, cluster, size, stamp, ok in parse_dirents(root):
        flag = '' if ok else '   <-- IMPLAUSIBLE TIMESTAMP'
        if not ok:
            failures += 1
        kind = 'dir' if is_dir else 'file'
        print(f'{name:<24} {kind:<5} {cluster:>8} {size:>10}  {stamp}{flag}')

    bad_padding = check_padding(root)
    if bad_padding:
        failures += 1
        print(f'\nFAIL: filename padding carries junk in: {", ".join(bad_padding)}')

    print()
    if failures:
        print(f'FAILED: {failures} problem(s)')
        return 1

    print('OK: every entry decodes as valid Xbox 360 on-disk data')
    return 0


if __name__ == '__main__':
    sys.exit(main())
