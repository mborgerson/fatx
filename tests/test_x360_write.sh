#!/usr/bin/env bash
#
# Write to a synthetic big-endian (Xbox 360) FATX image and verify the result.
#
# Round-tripping through libfatx is not enough on its own: a driver that
# byte-swaps consistently but wrongly reads back everything it wrote and looks
# perfectly healthy. So after every write this also decodes the raw image with
# verify_x360_dirents.py, which shares no code with the library.
#
# Requires fatxfs on PATH.

set -eu

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
MNT="$WORK/mnt"
IMG="$WORK/x360.img"
SIZE=$((16 * 1024 * 1024))

# Geometry of the synthetic image, needed by the raw decoder.
CLUSTER_OFFSET=8192
BYTES_PER_CLUSTER=16384

cleanup() {
    if mountpoint -q "$MNT" 2>/dev/null; then
        fusermount -u "$MNT" || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

mount_rw() {
    fatxfs --variant=x360 --offset=0 --size=$SIZE "$IMG" "$MNT"
    # fatxfs backgrounds itself; wait for the mount to become live.
    for _ in $(seq 1 50); do
        mountpoint -q "$MNT" && return 0
        sleep 0.1
    done
    fail "mount did not come up"
}

unmount() {
    fusermount -u "$MNT"
    for _ in $(seq 1 50); do
        mountpoint -q "$MNT" || return 0
        sleep 0.1
    done
    fail "unmount did not complete"
}

verify_raw() {
    python3 "$HERE/verify_x360_dirents.py" "$IMG" "$CLUSTER_OFFSET" "$BYTES_PER_CLUSTER" \
        || fail "$1: raw image does not decode as valid Xbox 360 data"
}

mkdir -p "$MNT"
python3 "$HERE/make_x360_image.py" "$IMG" > /dev/null

echo "== 1. create a file, a directory, and a nested file =="
mount_rw
echo "written by libfatx" > "$MNT/NEW.TXT"
mkdir "$MNT/NEWDIR"
echo "nested" > "$MNT/NEWDIR/INNER.TXT"
unmount
verify_raw "after create"
echo "ok"

echo
echo "== 2. a multi-cluster file, to exercise FAT chain writing =="
# 100 KiB spans seven 16 KiB clusters, so the chain has to be built and walked.
mount_rw
head -c 102400 /dev/urandom > "$WORK/big.bin"
cp "$WORK/big.bin" "$MNT/BIG.BIN"
unmount
verify_raw "after multi-cluster write"

mount_rw
cmp "$MNT/BIG.BIN" "$WORK/big.bin" || fail "multi-cluster file did not survive a remount"
unmount
echo "ok: 100 KiB across 7 clusters, byte-exact after remount"

echo
echo "== 3. pre-existing data must be untouched =="
mount_rw
grep -q "Hello from a big-endian Xbox 360 filesystem." "$MNT/HELLO.TXT" \
    || fail "HELLO.TXT was damaged by the writes"
[ "$(cat "$MNT/NEWDIR/INNER.TXT")" = "nested" ] || fail "nested file lost"
unmount
echo "ok"

echo
echo "== 4. rename and delete =="
mount_rw
mv "$MNT/NEW.TXT" "$MNT/RENAMED.TXT"
rm "$MNT/BIG.BIN"
unmount
verify_raw "after rename and delete"

mount_rw
[ -f "$MNT/RENAMED.TXT" ] || fail "rename did not persist"
[ ! -f "$MNT/NEW.TXT" ] || fail "old name still present after rename"
[ ! -f "$MNT/BIG.BIN" ] || fail "deleted file still present"
unmount
echo "ok"

echo
echo "== 5. timestamps written must round-trip through the raw decoder =="
mount_rw
touch -d "2026-08-11 23:50:30" "$MNT/RENAMED.TXT"
unmount
verify_raw "after touch"
mount_rw
STAMP="$(date -d @"$(stat -c %Y "$MNT/RENAMED.TXT")" '+%Y-%m-%d %H:%M:%S')"
[ "$STAMP" = "2026-08-11 23:50:30" ] \
    || fail "timestamp came back as $STAMP, expected 2026-08-11 23:50:30"
unmount
echo "ok: $STAMP"

echo
echo "== 6. a read-only mount must refuse to write =="
fatxfs --variant=x360 --read-only --offset=0 --size=$SIZE "$IMG" "$MNT"
if touch "$MNT/SHOULD_NOT_APPEAR" 2>/dev/null; then
    unmount
    fail "read-only mount accepted a write"
fi
unmount
echo "ok"

echo
echo "== 7. final state, decoded independently =="
python3 "$HERE/verify_x360_dirents.py" "$IMG" "$CLUSTER_OFFSET" "$BYTES_PER_CLUSTER"

echo
echo "ALL X360 WRITE TESTS PASSED"
