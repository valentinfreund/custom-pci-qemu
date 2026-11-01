#!/usr/bin/env bash
# Boot the Buildroot guest in our QEMU with the custom devices attached.
#
#   ./scripts/run.sh            serial console in this terminal (exit: Ctrl-a x)
#   ./scripts/run.sh --share    also share ./share with the guest at /mnt/host
#   ./scripts/run.sh --persist  keep changes to the guest disk (default: discard)
#   ./scripts/run.sh -- <args>  pass extra arguments to QEMU
source "$(dirname "$0")/common.sh"

IMAGES="${BR_OUT}/images"
[[ -f "${IMAGES}/rootfs.ext2" ]] || die "No guest image. Run ./scripts/setup-buildroot.sh first."

SNAPSHOT=(-snapshot)
SHARE=()
EXTRA=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --share)
            mkdir -p "${REPO_ROOT}/share"
            SHARE=(-virtfs "local,path=${REPO_ROOT}/share,mount_tag=hostshare,security_model=none,id=hostshare")
            ;;
        --persist) SNAPSHOT=() ;;
        --) shift; EXTRA=("$@"); break ;;
        *) die "Unknown option: $1" ;;
    esac
    shift
done

ACCEL=(-accel tcg)
[[ -w /dev/kvm ]] && ACCEL=(-accel kvm -cpu host)

case "${ARCH}" in
    x86_64)
        exec "${QEMU_BUILD}/qemu-system-x86_64" \
            -M pc "${ACCEL[@]}" -m 1G -smp 2 \
            -kernel "${IMAGES}/bzImage" \
            -drive "file=${IMAGES}/rootfs.ext2,if=virtio,format=raw" \
            -append "rootwait root=/dev/vda console=ttyS0" \
            -nic user,model=virtio-net-pci \
            -device custom-pci-device \
            -nographic \
            "${SNAPSHOT[@]}" "${SHARE[@]}" "${EXTRA[@]}"
        ;;
    *) die "ARCH=${ARCH} is not supported yet" ;;
esac
