#!/usr/bin/env bash
#
# Dump a slice of an Xbox 360 hard drive partition for offline testing.
#
# This only ever reads from the device. It never writes to it.
#
# Usage: dump_x360_partition.sh <device> [partition] [size in MiB]
#   e.g. dump_x360_partition.sh /dev/sdb data 256
#
# The 360 has no partition table: partitions live at fixed byte offsets, and
# those offsets are not MiB-aligned, so the skip is done in bytes rather than
# with a round block count.

set -euo pipefail

DEV="${1:-}"
PART="${2:-data}"
SIZE_MIB="${3:-256}"

if [ -z "$DEV" ]; then
    echo "usage: $0 <device> [sysext|sysext2|compat|data] [size in MiB]" >&2
    exit 1
fi

case "$PART" in
    sysext)  OFFSET=$((0x10C080000)) ;;
    sysext2) OFFSET=$((0x118EB0000)) ;;
    compat)  OFFSET=$((0x120EB0000)) ;;
    data)    OFFSET=$((0x130EB0000)) ;;
    *) echo "unknown partition '$PART' (expected sysext, sysext2, compat or data)" >&2; exit 1 ;;
esac

OUT="x360-${PART}.img"

if [ ! -r "$DEV" ]; then
    echo "cannot read $DEV -- run with sudo, or check the device path" >&2
    exit 1
fi

echo "device    : $DEV"
echo "partition : $PART"
echo "offset    : $OFFSET bytes (0x$(printf '%X' $OFFSET))"
echo "size      : ${SIZE_MIB} MiB"
echo "output    : $OUT"
echo

# Check the signature before committing to a large read. The 360 writes the
# bytes 'XTAF'; the original Xbox writes 'FATX'. Anything else means either the
# wrong device or the wrong offset, and there is no point dumping it.
SIG="$(dd if="$DEV" bs=1 skip="$OFFSET" count=4 status=none | tr -d '\0')"
case "$SIG" in
    XTAF)
        echo "signature : XTAF -- Xbox 360 filesystem, as expected"
        ;;
    FATX)
        echo "signature : FATX -- this is an ORIGINAL XBOX filesystem, not a 360 one." >&2
        echo "            Wrong drive? Continuing anyway, but --variant=x360 will not apply." >&2
        ;;
    *)
        echo "signature : '$SIG' -- not a FATX partition at this offset." >&2
        echo "            Wrong device, or a drive whose layout differs from the standard map." >&2
        echo "            Refusing to dump. Check the device with:" >&2
        echo "              sudo dd if=$DEV bs=1 skip=$OFFSET count=64 | xxd | head" >&2
        exit 1
        ;;
esac

echo
echo "dumping..."
dd if="$DEV" of="$OUT" bs=1M count="$SIZE_MIB" \
   skip="$OFFSET" iflag=skip_bytes status=progress

echo
echo "sha256: $(sha256sum "$OUT" | cut -d' ' -f1)"
echo
echo "Mount it read-only with:"
echo "  mkdir -p /tmp/x360 && fatxfs --variant=x360 --offset=0 --size=$((SIZE_MIB * 1024 * 1024)) -o ro $OUT /tmp/x360"
echo
echo "Note: a partial dump truncates the partition, so any file whose data lives"
echo "beyond ${SIZE_MIB} MiB will list correctly but fail to read in full."
