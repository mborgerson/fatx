#!/usr/bin/env bash
#
# Diagnostic: for every STFS package whose display name failed to decode in
# gate_real_drive.sh step 5, dump the header bytes around the display-name
# field and the content-type/metadata-version fields, so we can tell whether
# this is a misread FAT chain or just a header-layout variant the verifier
# doesn't know about.
#
# Usage: sudo tests/diag_stfs_names.sh /dev/sdb data

set -euo pipefail

DEV="${1:-}"
PART="${2:-data}"

if [ -z "$DEV" ]; then
    echo "usage: $0 <device> [partition]" >&2
    exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FATXFS="${FATXFS:-$HERE/../install/bin/fatxfs}"
OUT="$(pwd)/gate-results"
MNT="$OUT/mnt"
LISTING="$OUT/listing.txt"

[ -x "$FATXFS" ] || { echo "fatxfs not found at $FATXFS" >&2; exit 1; }
[ -f "$LISTING" ] || { echo "no listing.txt from a prior gate run at $LISTING" >&2; exit 1; }

cleanup() {
    mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$MNT"
"$FATXFS" --variant=x360 --partition="$PART" --read-only \
          --log="$OUT/diag_mount.log" --loglevel=2 "$DEV" "$MNT"

python3 - "$MNT" "$LISTING" <<'PY'
import os, sys

mnt, listing = sys.argv[1], sys.argv[2]
bad = []

with open(listing) as f:
    for line in f:
        kind, size, rel = line.rstrip("\n").split(" ", 2)
        if kind != "f" or int(size) < 0x1000:
            continue
        path = os.path.join(mnt, rel)
        try:
            with open(path, "rb") as fh:
                head = fh.read(0x1000)
        except OSError as e:
            print(f"OPEN FAILED: {rel}: {e}")
            continue
        if head[:4] not in (b"CON ", b"LIVE", b"PIRS"):
            continue
        raw = head[0x411:0x411 + 0x80]
        name = raw.decode("utf-16-be", "replace").split("\x00")[0].strip()
        if not (name and all(ch.isprintable() for ch in name)):
            bad.append((rel, size, head))

print(f"{len(bad)} package(s) with unreadable display name at 0x411\n")

for rel, size, head in bad:
    print("=" * 70)
    print(f"{rel}  (size={size})")
    print(f"  magic:            {head[:4]!r}")
    # candidate metadata fields per public STFS docs (offsets from file start)
    print(f"  content type  @0x344: {head[0x344:0x348].hex()}")
    print(f"  metadata ver  @0x348: {head[0x348:0x34c].hex()}")
    print(f"  bytes 0x400-0x440 (display name region + neighbors):")
    chunk = head[0x400:0x440]
    hexline = " ".join(f"{b:02x}" for b in chunk)
    print(f"    {hexline}")
    # try a spread of plausible alternate offsets for the UTF-16BE name
    for off in (0x411, 0x171, 0x1691, 0x3391, 0x395, 0x1191):
        raw = head[off:off + 0x80]
        name = raw.decode("utf-16-be", "replace").split("\x00")[0].strip()
        printable = name and all(ch.isprintable() for ch in name)
        flag = "OK" if printable else "--"
        print(f"    [{flag}] @0x{off:04x}: {name!r}")
    print()
PY
