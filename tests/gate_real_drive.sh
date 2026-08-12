#!/usr/bin/env bash
#
# Verification gate: read a real Xbox 360 drive and check the result is sane.
#
# This only ever reads. It mounts with --read-only, which opens the device
# without write access at all rather than relying on FUSE's -o ro.
#
# Usage: sudo tests/gate_real_drive.sh /dev/sdX [partition]
#
# What it establishes, and what it does not:
#
#   The synthetic tests prove the driver is self-consistent. They cannot prove
#   it reads a real disk correctly, because they only ever read images built to
#   the same understanding of the format. This walks an actual console's
#   filesystem end to end and checks the contents against facts that come from
#   outside this project: file formats with their own magic numbers and
#   internal length fields, which cannot survive a misread FAT chain.
#
#   It still is not a listing captured from the console itself. If you can pull
#   one over FTP, diff it against listing.txt below -- that is the last check
#   this cannot do for itself.

set -euo pipefail

DEV="${1:-}"
PART="${2:-data}"

if [ -z "$DEV" ]; then
    echo "usage: $0 <device> [sysext|sysext2|compat|data]" >&2
    exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FATXFS="${FATXFS:-$HERE/../install/bin/fatxfs}"
FATXFUSE="${FATXFUSE:-$HERE/../rust/target/release/fatx-fuse}"
OUT="$(pwd)/gate-results"
MNT="$OUT/mnt"
RUSTMNT="$OUT/rustmnt"

[ -x "$FATXFS" ] || { echo "fatxfs not found at $FATXFS" >&2; exit 1; }

cleanup() {
    mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT" 2>/dev/null || true
    mountpoint -q "$RUSTMNT" 2>/dev/null && fusermount -u "$RUSTMNT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$MNT" "$RUSTMNT"
fail=0
note() { echo; echo "== $* =="; }

note "1. mounting $DEV partition '$PART' read-only"
"$FATXFS" --variant=x360 --partition="$PART" --read-only \
          --log="$OUT/mount.log" --loglevel=4 "$DEV" "$MNT"
grep -E 'Variant|Sectors per Cluster|FAT Type|# of Clusters' "$OUT/mount.log" | sed 's/^/   /'

note "2. full recursive listing"
# find's own errors matter: on a whole drive every directory must be
# enumerable, and anything that is not is a real failure rather than noise.
( cd "$MNT" && find . -mindepth 1 -printf '%y %s %P\n' 2> "$OUT/find-errors.txt" | sort ) \
    > "$OUT/listing.txt" || true
printf '   %s entries (%s files, %s dirs)\n' \
    "$(wc -l < "$OUT/listing.txt")" \
    "$(grep -c '^f' "$OUT/listing.txt" || true)" \
    "$(grep -c '^d' "$OUT/listing.txt" || true)"
if [ -s "$OUT/find-errors.txt" ]; then
    echo "   $(wc -l < "$OUT/find-errors.txt") directory error(s):"
    head -5 "$OUT/find-errors.txt" | sed 's/^/     /'
    echo "     (on a whole drive this should be empty; on a truncated dump it"
    echo "      just means those clusters were past the end of the file)"
    fail=1
fi

note "3. every file must be readable in full"
# A misread FAT chain typically shows up as a short read or an I/O error long
# before it shows up as wrong content.
unreadable=0
while IFS= read -r rel; do
    if ! dd if="$MNT/$rel" of=/dev/null bs=1M status=none 2>/dev/null; then
        echo "   UNREADABLE: $rel"
        unreadable=$((unreadable + 1))
    fi
done < <(grep '^f ' "$OUT/listing.txt" | cut -d' ' -f3-)
if [ "$unreadable" -gt 0 ]; then
    echo "   $unreadable file(s) failed to read"
    fail=1
else
    echo "   ok: all files read to completion"
fi

note "4. file contents must match their own format's magic"
# These magic numbers are defined by Microsoft and by the image formats, not by
# this project, so they are ground truth from outside the codebase. A FAT chain
# that is followed incorrectly cannot land on the right magic by luck.
python3 - "$MNT" "$OUT/listing.txt" <<'PY'
import os, struct, sys

mnt, listing = sys.argv[1], sys.argv[2]

# (description, extensions, expected leading bytes)
MAGIC = [
    ("STFS package", (),            (b"CON ", b"LIVE", b"PIRS")),
    ("XEX executable", (".xex",),   (b"XEX2", b"XEX1")),
    ("JPEG",         (".jpg", ".jpeg"), (b"\xff\xd8\xff",)),
    ("PNG",          (".png",),     (b"\x89PNG\r\n\x1a\n",)),
]

checked = matched = mismatched = 0
by_kind = {}

with open(listing) as f:
    for line in f:
        kind, size, rel = line.rstrip("\n").split(" ", 2)
        if kind != "f" or int(size) < 4:
            continue
        path = os.path.join(mnt, rel)
        try:
            with open(path, "rb") as fh:
                head = fh.read(8)
        except OSError:
            continue

        ext = os.path.splitext(rel)[1].lower()
        for desc, exts, magics in MAGIC:
            applies = (ext in exts) if exts else any(head.startswith(m) for m in magics)
            if not applies:
                continue
            checked += 1
            if any(head.startswith(m) for m in magics):
                matched += 1
                by_kind[desc] = by_kind.get(desc, 0) + 1
            else:
                mismatched += 1
                print(f"   MISMATCH: {rel} claims {desc} but starts {head[:4]!r}")
            break

for desc, n in sorted(by_kind.items()):
    print(f"   {n:5d}  {desc}")
if checked == 0:
    print("   (no files with recognisable magic found)")
elif mismatched:
    print(f"   FAILED: {mismatched} of {checked} did not match")
    sys.exit(1)
else:
    print(f"   ok: {matched}/{checked} files match their format's magic")
PY
[ $? -eq 0 ] || fail=1

note "5. STFS packages must be internally consistent"
# An STFS header stores the display name as UTF-16BE at a fixed offset. Getting
# readable text out of it means the cluster chain landed exactly right, not
# merely on a plausible-looking first sector.
python3 - "$MNT" "$OUT/listing.txt" <<'PY'
import os, sys

mnt, listing = sys.argv[1], sys.argv[2]
ok = bad = 0
samples = []

with open(listing) as f:
    for line in f:
        kind, size, rel = line.rstrip("\n").split(" ", 2)
        if kind != "f" or int(size) < 0x1000:
            continue
        try:
            with open(os.path.join(mnt, rel), "rb") as fh:
                head = fh.read(0x500)
        except OSError:
            continue
        if not head[:4] in (b"CON ", b"LIVE", b"PIRS"):
            continue
        # Display name: UTF-16BE at 0x411 in the STFS metadata.
        raw = head[0x411:0x411 + 0x80]
        name = raw.decode("utf-16-be", "replace").split("\x00")[0].strip()
        if name and all(ch.isprintable() for ch in name):
            ok += 1
            if len(samples) < 6:
                samples.append((name, rel))
        else:
            bad += 1

for name, rel in samples:
    print(f'   "{name}"')
if ok == 0 and bad == 0:
    print("   (no STFS packages on this partition)")
elif bad:
    print(f"   FAILED: {bad} package(s) had unreadable display names ({ok} ok)")
    sys.exit(1)
else:
    print(f"   ok: {ok} STFS package(s) with readable display names")
PY
[ $? -eq 0 ] || fail=1

note "6. cross-check against the independent Rust implementation"
if [ -x "$FATXFUSE" ]; then
    "$FATXFUSE" --variant=x360 --partition="$PART" "$DEV" "$RUSTMNT" 2>/dev/null &
    for _ in $(seq 1 50); do mountpoint -q "$RUSTMNT" && break; sleep 0.2; done
    ( cd "$RUSTMNT" && find . -mindepth 1 -printf '%y %s %P\n' | sort ) > "$OUT/listing-rust.txt"
    if diff -q "$OUT/listing.txt" "$OUT/listing-rust.txt" > /dev/null; then
        echo "   ok: C and Rust drivers produce identical listings"
    else
        echo "   MISMATCH between the two implementations:"
        diff "$OUT/listing.txt" "$OUT/listing-rust.txt" | head -20
        fail=1
    fi
    fusermount -u "$RUSTMNT" 2>/dev/null || true
else
    echo "   skipped: fatx-fuse not built at $FATXFUSE"
fi

note "RESULT"
if [ "$fail" -eq 0 ]; then
    echo "   GATE PASSED"
else
    echo "   GATE FAILED -- do not write to a real drive"
fi
echo
echo "   Listing written to $OUT/listing.txt"
echo "   Remaining check this cannot do for itself: pull a listing from the"
echo "   console over FTP and diff it against that file."
exit "$fail"
