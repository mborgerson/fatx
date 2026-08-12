#!/usr/bin/env bash
#
# First real-hardware write test against the 360 HDD's `data` partition.
#
# Scope, deliberately narrow: creates exactly one new top-level folder
# (_fatx360_write_test/) and touches nothing that already exists. Backs up
# the boot sector + FAT before writing anything. Verifies the result three
# ways: read-back through the driver, independent raw decode of the on-disk
# bytes (shares no code with libfatx), and a structural re-listing diffed
# against the last full gate run to prove nothing pre-existing moved.
#
# This does NOT delete the test folder -- leave it in place so the console
# can be booted against it and checked over FTP/XeXMenu. Run
# hw_write_test_cleanup.sh afterward once that's confirmed.
#
# Usage: sudo tests/hw_write_test.sh /dev/sdb

set -euo pipefail

DEV="${1:-}"
[ -z "$DEV" ] && { echo "usage: $0 <device>" >&2; exit 1; }

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FATXFS="${FATXFS:-$HERE/../install/bin/fatxfs}"
OUT="$(pwd)/gate-results"
MNT="$OUT/mnt"
BACKUP="$OUT/data_partition_boot_and_fat.img"
LISTING="$OUT/listing.txt"

DATA_OFFSET=$((0x130EB0000))
BACKUP_SIZE=242946048        # 4096-byte superblock + FAT, computed from fatx.c's own layout math
CLUSTER_OFFSET=5358620672    # fs->cluster_offset for this drive's data partition (root dir cluster 1)
BYTES_PER_CLUSTER=16384

[ -x "$FATXFS" ] || { echo "fatxfs not found at $FATXFS" >&2; exit 1; }
[ -f "$LISTING" ] || { echo "no prior listing.txt at $LISTING -- run gate_real_drive.sh first" >&2; exit 1; }

cleanup_mount() {
    mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT" 2>/dev/null || true
}
trap cleanup_mount EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$MNT"

echo "== 0. backing up boot sector + FAT ($BACKUP_SIZE bytes) =="
if [ -f "$BACKUP" ] && [ "$(stat -c %s "$BACKUP")" -eq "$BACKUP_SIZE" ]; then
    echo "   ok: pre-write backup already exists at $BACKUP, not overwriting it"
else
    dd if="$DEV" of="$BACKUP" bs=512 skip=$((DATA_OFFSET / 512)) count=$((BACKUP_SIZE / 512)) status=none
    [ "$(stat -c %s "$BACKUP")" -eq "$BACKUP_SIZE" ] || fail "backup came back the wrong size"
    echo "   ok: saved to $BACKUP"
fi

mount_rw() {
    "$FATXFS" --variant=x360 --partition=data --log="$OUT/hw_write.log" --loglevel=3 "$DEV" "$MNT"
    for _ in $(seq 1 50); do mountpoint -q "$MNT" && return 0; sleep 0.2; done
    fail "read-write mount did not come up"
}
mount_ro() {
    "$FATXFS" --variant=x360 --partition=data --read-only --log="$OUT/hw_write_ro.log" --loglevel=3 "$DEV" "$MNT"
    for _ in $(seq 1 50); do mountpoint -q "$MNT" && return 0; sleep 0.2; done
    fail "read-only mount did not come up"
}
unmount() {
    fusermount -u "$MNT"
    for _ in $(seq 1 50); do mountpoint -q "$MNT" || return 0; sleep 0.2; done
    fail "unmount did not complete"
}

TESTDIR="_fatx360_write_test"

echo
echo "== 1. mounting read-write, creating $TESTDIR/ =="
mount_rw
if [ -d "$MNT/$TESTDIR" ]; then
    echo "   ok: $TESTDIR already exists on the drive from a prior run, not re-writing it"
else
    mkdir "$MNT/$TESTDIR"
    echo "fatx360 hardware write test $(date -Iseconds)" > "$MNT/$TESTDIR/HELLO.TXT"
    [ -f "$OUT/big_local.bin" ] || head -c 102400 /dev/urandom > "$OUT/big_local.bin"
    cp "$OUT/big_local.bin" "$MNT/$TESTDIR/BIG.BIN"
    mkdir "$MNT/$TESTDIR/NESTED"
    echo "nested file" > "$MNT/$TESTDIR/NESTED/INNER.TXT"
fi
unmount
echo "   ok: written and unmounted cleanly"

echo
echo "== 2. independent raw decode of the root directory =="
# The decoder scans the WHOLE root directory, not just what we wrote -- other
# pre-existing entries (e.g. from unrelated console-side tools) may already
# carry issues that have nothing to do with this test. Only what we ourselves
# wrote is grounds to fail here.
python3 "$HERE/verify_x360_dirents.py" "$DEV" "$CLUSTER_OFFSET" "$BYTES_PER_CLUSTER" \
    > "$OUT/hw_write_raw_decode.txt" || true
cat "$OUT/hw_write_raw_decode.txt"
OUR_LINE="$(grep "^${TESTDIR}[[:space:]]" "$OUT/hw_write_raw_decode.txt" || true)"
[ -n "$OUR_LINE" ] || fail "$TESTDIR does not appear in the independently-decoded root directory"
case "$OUR_LINE" in
    *IMPLAUSIBLE*) fail "$TESTDIR has an implausible timestamp in the raw decode: $OUR_LINE" ;;
esac
PADDING_LINE="$(grep '^FAIL: filename padding carries junk in:' "$OUT/hw_write_raw_decode.txt" || true)"
if [ -n "$PADDING_LINE" ]; then
    case "$PADDING_LINE" in
        *"$TESTDIR"*) fail "our own write left garbage in filename padding: $PADDING_LINE" ;;
        *) echo "   note: pre-existing unrelated entries flagged for padding junk (not from this test): $PADDING_LINE" ;;
    esac
fi
echo "   ok: $TESTDIR present and well-formed in the raw on-disk root directory"

echo
echo "== 3. remounting read-only, verifying new files byte-exact =="
mount_ro
cmp "$MNT/$TESTDIR/BIG.BIN" "$OUT/big_local.bin" || fail "multi-cluster file did not survive"
[ "$(cat "$MNT/$TESTDIR/NESTED/INNER.TXT")" = "nested file" ] || fail "nested file lost"
[ -f "$MNT/$TESTDIR/HELLO.TXT" ] || fail "top-level file lost"
echo "   ok: all new files read back byte-exact"

echo
echo "== 4. confirming nothing pre-existing moved =="
( cd "$MNT" && find . -mindepth 1 -not -path "./$TESTDIR*" -printf '%y %s %P\n' | sort ) \
    > "$OUT/listing_after_write.txt"
if diff -q "$LISTING" "$OUT/listing_after_write.txt" > /dev/null; then
    echo "   ok: every pre-existing entry (path, size, kind) is unchanged"
else
    echo "   MISMATCH -- something pre-existing changed:"
    diff "$LISTING" "$OUT/listing_after_write.txt" | head -20
    unmount
    fail "pre-existing data was disturbed"
fi
unmount

echo
echo "== RESULT: HARDWARE WRITE TEST PASSED =="
echo "   $TESTDIR/ is on the drive now. Move it to the console, boot, and check"
echo "   over FTP/XeXMenu that the dash reads it fine and the folder is there."
echo "   Once confirmed, run: sudo tests/hw_write_test_cleanup.sh $DEV"
