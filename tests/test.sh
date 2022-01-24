#!/bin/bash
set -ex
tar xvf xbox_hdd.img.tgz
mkdir -p c
fatxfs xbox_hdd.img c
sha256sum -c expected_sha256.txt
