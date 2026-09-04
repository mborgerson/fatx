#!/usr/bin/env bash
#
# Mount a synthetic big-endian (Xbox 360) FATX image and verify its contents.
#
# The image is assembled independently of libfatx by make_x360_image.py, so a
# multi-byte field that is never byte-swapped shows up here as a wrong name,
# size, or content -- which an image written by libfatx's own writer would hide.
#
# Requires fatxfs on PATH.

set -eu

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
MNT="$WORK/mnt"
IMG="$WORK/x360.img"

cleanup() {
    if mountpoint -q "$MNT" 2>/dev/null; then
        fusermount -u "$MNT" || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

mkdir -p "$MNT"

echo "== building synthetic big-endian image =="
python3 "$HERE/make_x360_image.py" "$IMG" > "$WORK/expected.txt"
cat "$WORK/expected.txt"

fail() { echo "FAIL: $*" >&2; exit 1; }

echo
echo "== 1. auto-detection should identify it as x360 =="
fatxfs --offset=0 --size=$((16 * 1024 * 1024)) \
       --log="$WORK/mount.log" --loglevel=4 "$IMG" "$MNT"
grep -q "detected x360 filesystem" "$WORK/mount.log" \
    || fail "auto-detection did not report an x360 filesystem:
$(cat "$WORK/mount.log")"
echo "ok: $(grep -h 'detected' "$WORK/mount.log")"

echo
echo "== 2. directory tree should match =="
ACTUAL="$(cd "$MNT" && find . -type f -printf '/%P %s\n' | sort)"
EXPECTED="$(sort "$WORK/expected.txt")"
if [ "$ACTUAL" != "$EXPECTED" ]; then
    fail "listing mismatch
expected:
$EXPECTED
actual:
$ACTUAL"
fi
echo "ok:"
echo "$ACTUAL"

echo
echo "== 3. file contents should match =="
grep -q "Hello from a big-endian Xbox 360 filesystem." "$MNT/HELLO.TXT" \
    || fail "HELLO.TXT content is wrong: $(cat "$MNT/HELLO.TXT" | head -c 80)"

python3 - "$MNT/GAMES/COVER.JPG" <<'EOF' || fail "COVER.JPG content is wrong"
import sys
expected = bytes(range(256)) * 8
with open(sys.argv[1], "rb") as f:
    actual = f.read()
if actual != expected:
    print(f"length {len(actual)} vs {len(expected)}", file=sys.stderr)
    sys.exit(1)
EOF
echo "ok: both files read back byte-exact"

echo
echo "== 3b. timestamps should decode with the 360's own rules =="
# 2026-08-11 23:50:30. The hour and minute are chosen so that the original
# Xbox's narrower fields would mangle them into 07:18 rather than 23:50.
STAMP="$(date -d @"$(stat -c %Y "$MNT/HELLO.TXT")" '+%Y-%m-%d %H:%M:%S')"
[ "$STAMP" = "2026-08-11 23:50:30" ] \
    || fail "timestamp is $STAMP, expected 2026-08-11 23:50:30
(2046-* means the epoch reverted to 2000; *07:18* means the time field
reverted to the original Xbox's 4-bit hour and 5-bit minute)"
echo "ok: $STAMP"

fusermount -u "$MNT"

echo
echo "== 4. explicit --variant=x360 should also work =="
fatxfs --variant=x360 --offset=0 --size=$((16 * 1024 * 1024)) "$IMG" "$MNT"
test -f "$MNT/GAMES/COVER.JPG" || fail "explicit variant mount is missing files"
echo "ok"
fusermount -u "$MNT"

echo
echo "== 5. forcing the wrong variant should be rejected, not silently wrong =="
if fatxfs --variant=xbox --offset=0 --size=$((16 * 1024 * 1024)) \
          --log="$WORK/wrong.log" --loglevel=4 "$IMG" "$MNT" > /dev/null 2>&1; then
    fusermount -u "$MNT" || true
    fail "mounting a 360 image as xbox succeeded; it should have been rejected"
fi
grep -qi 'invalid signature' "$WORK/wrong.log" \
    || fail "mount failed, but not because of the signature:
$(cat "$WORK/wrong.log")"
echo "ok: $(grep -i 'invalid signature' "$WORK/wrong.log" | head -1)"

echo
echo "ALL X360 READ TESTS PASSED"
