Usage Guide
===========

This guide covers building and using every component on Linux. (The
per-component READMEs are terse; this is the missing end-to-end walkthrough.)

Building
--------

```sh
sudo apt install build-essential cmake pkg-config libfuse-dev   # FUSE 2.x

# libfatx (static library, required by everything else)
cmake -S libfatx -B build/libfatx -DCMAKE_BUILD_TYPE=Release
cmake --build build/libfatx -j$(nproc)
cmake --install build/libfatx --prefix "$PWD/build/prefix"

# fatxfs (C FUSE driver)
cmake -S fatxfs -B build/fatxfs -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PWD/build/prefix"
cmake --build build/fatxfs -j$(nproc)

# Rust crates (fatx library + fatx-fuse FUSE driver)
cd rust && cargo build --release
```

fatxfs (C) — mount, format, inspect
-----------------------------------

```sh
# Create and format an 8 GB Xbox HDD image (retail partition layout):
fallocate -l 8G xbox.img
mkdir c
./build/fatxfs/fatxfs --format=retail --destroy-all-existing-data xbox.img c
fusermount -u c

# Mount the C (system) drive of an image or a real disk:
./build/fatxfs/fatxfs xbox.img c                  # default --drive=c
./build/fatxfs/fatxfs /dev/sdX e --drive=e        # data partition
./build/fatxfs/fatxfs img.bin m --offset=0 --size=0x20000000   # raw partition

df -h c        # real numbers (statfs is implemented)
fusermount -u c
```

Drive letters: `x y z c e` (plus `f` on larger, non-retail formats).
`--format=f-takes-all` formats big disks with everything beyond the retail
region in one F partition (`--sectors-per-cluster` defaults to 128 = 64 KiB).

fatx-fuse (Rust) — read/write FUSE driver
-----------------------------------------

```sh
cd rust
cargo run --bin fatx-fuse -- ../xbox.img /mnt/point --drive-letter c
# writes are enabled by default; use --read-only for a safe look around
fusermount -u /mnt/point
```

pyfatx (Python bindings)
------------------------

```sh
pip install pyfatx
python -m pyfatx -x xbox.img        # extract a whole filesystem
```

gfatx (Qt GUI)
--------------

See gfatx/README.md; early state, browse-only.
