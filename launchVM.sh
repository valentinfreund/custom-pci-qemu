#!/bin/bash

QEMU_BUILD="./build/qemu-system-x86_64"
IMAGE="./images/linux-pci-dev.qcow2"
ISO="./isos/ubuntu-24.04.3-desktop-amd64.iso"

$QEMU_BUILD \
    -cpu host \
    -enable-kvm \
    -m 4G \
    -smp 4 \
    -drive file=$IMAGE,format=qcow2,if=virtio \
    -device my-pci-device \
    -display gtk \
    -virtfs local,path=./filex,mount_tag=hostshare,security_model=mapped,id=hostshare
