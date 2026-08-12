#!/usr/bin/env bash
#
# Fast re-check: reuses the listing.txt from a prior full gate_real_drive.sh
# run and only re-runs step 5 (STFS display names, now content-type aware)
# and step 6 (Rust cross-check). Skips the full byte-for-byte file read.
#
# Usage: sudo tests/gate_recheck.sh /dev/sdb data

set -euo pipefail

DEV="${1:-}"
PART="${2:-data}"

if [ -z "$DEV" ]; then
    echo "usage: $0 <device> [partition]" >&2
    exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FATXFS="${FATXFS:-$HERE/../install/bin/fatxfs}"
FATXFUSE="${FATXFUSE:-$HERE/../rust/target/release/fatx-fuse}"
OUT="$(pwd)/gate-results"
MNT="$OUT/mnt"
RUSTMNT="$OUT/rustmnt"
LISTING="$OUT/listing.txt"

[ -x "$FATXFS" ] || { echo "fatxfs not found at $FATXFS" >&2; exit 1; }
[ -f "$LISTING" ] || { echo "no listing.txt from a prior gate run at $LISTING" >&2; exit 1; }

cleanup() {
    mountpoint -q "$MNT" 2>/dev/null && fusermount -u "$MNT" 2>/dev/null || true
    mountpoint -q "$RUSTMNT" 2>/dev/null && fusermount -u "$RUSTMNT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$MNT" "$RUSTMNT"
fail=0
note() { echo; echo "== $* =="; }

note "mounting $DEV partition '$PART' read-only"
"$FATXFS" --variant=x360 --partition="$PART" --read-only \
          --log="$OUT/recheck_mount.log" --loglevel=2 "$DEV" "$MNT"

note "5. STFS packages must be internally consistent (reusing $LISTING)"
if ! python3 - "$MNT" "$LISTING" <<'PY'
import os, struct, sys

mnt, listing = sys.argv[1], sys.argv[2]
NO_DISPLAY_NAME = {0x00000001, 0x00010000, 0x00040000}
ok = bad = skipped = 0
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
        content_type = struct.unpack(">I", head[0x344:0x348])[0]
        if content_type in NO_DISPLAY_NAME:
            skipped += 1
            continue
        raw = head[0x411:0x411 + 0x80]
        name = raw.decode("utf-16-be", "replace").split("\x00")[0].strip()
        if name and all(ch.isprintable() for ch in name):
            ok += 1
            if len(samples) < 6:
                samples.append((name, rel))
        else:
            bad += 1
            print(f"   UNREADABLE: {rel} (content type 0x{content_type:08x})")

for name, rel in samples:
    print(f'   "{name}"')
if skipped:
    print(f"   ({skipped} save/profile/cache package(s) skipped -- no display name field)")
if ok == 0 and bad == 0:
    print("   (no title-carrying STFS packages on this partition)")
elif bad:
    print(f"   FAILED: {bad} package(s) had unreadable display names ({ok} ok)")
    sys.exit(1)
else:
    print(f"   ok: {ok} STFS package(s) with readable display names")
PY
then
    fail=1
fi

note "6. cross-check against the independent Rust implementation"
if [ -x "$FATXFUSE" ]; then
    "$FATXFUSE" --variant=x360 --partition="$PART" "$DEV" "$RUSTMNT" 2>/dev/null &
    for _ in $(seq 1 50); do mountpoint -q "$RUSTMNT" && break; sleep 0.2; done
    ( cd "$RUSTMNT" && find . -mindepth 1 -printf '%y %s %P\n' | sort ) > "$OUT/listing-rust.txt"
    if diff -q "$LISTING" "$OUT/listing-rust.txt" > /dev/null; then
        echo "   ok: C and Rust drivers produce identical listings"
    else
        echo "   MISMATCH between the two implementations:"
        diff "$LISTING" "$OUT/listing-rust.txt" | head -20
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
exit "$fail"
