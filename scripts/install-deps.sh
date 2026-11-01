#!/usr/bin/env bash
# Install host packages needed to build QEMU and Buildroot (Debian/Ubuntu).
set -euo pipefail
sudo apt-get update
sudo apt-get install -y \
    build-essential git python3 python3-venv ninja-build pkg-config \
    libglib2.0-dev libpixman-1-dev libslirp-dev flex bison \
    bc cpio rsync unzip wget file perl libelf-dev libssl-dev
