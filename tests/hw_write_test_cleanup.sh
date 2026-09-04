#!/usr/bin/env bash
#
# Removes the _fatx360_write_test/ folder created by hw_write_test.sh.
# Run this only after the console has confirmed it reads the drive fine.
#
# Usage: sudo tests/hw_write_test_cleanup.sh /dev/sdb

set -euo pipefail

DEV="${1:-}"
[ -z "$DEV" ] && { echo "usage: $0 <device>" >&2; exit 1; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FATXFS="${FATXFS:-$HERE/../install/bin/fatxfs}"
OUT="$(pwd)/gate-results"
MNT="$OUT/mnt"
LISTING="$OUT/listing.txt"
TESTDIR="_fatx360_write_test"

cleanup_mount() {
    mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT" 2>/dev/null || true
}
trap cleanup_mount EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$MNT"
"$FATXFS" --variant=x360 --partition=data --log="$OUT/hw_cleanup.log" --loglevel=3 "$DEV" "$MNT"
for _ in $(seq 1 50); do mountpoint -q "$MNT" && break; sleep 0.2; done

[ -d "$MNT/$TESTDIR" ] || fail "$TESTDIR not found on the drive"
rm -rf "$MNT/$TESTDIR"

fusermount -u "$MNT"
for _ in $(seq 1 50); do mountpoint -q "$MNT" || break; sleep 0.2; done

echo "== remounting read-only to confirm the drive is back to its original state =="
"$FATXFS" --variant=x360 --partition=data --read-only --log="$OUT/hw_cleanup_ro.log" --loglevel=3 "$DEV" "$MNT"
for _ in $(seq 1 50); do mountpoint -q "$MNT" && break; sleep 0.2; done

( cd "$MNT" && find . -mindepth 1 -printf '%y %s %P\n' | sort ) > "$OUT/listing_after_cleanup.txt"
fusermount -u "$MNT"

if diff -q "$LISTING" "$OUT/listing_after_cleanup.txt" > /dev/null; then
    echo "ok: drive listing matches the original gate run exactly -- clean"
else
    echo "MISMATCH after cleanup:"
    diff "$LISTING" "$OUT/listing_after_cleanup.txt" | head -20
    exit 1
fi
