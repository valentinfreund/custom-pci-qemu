#!/usr/bin/env bash
# Incremental rebuilds.
#
#   ./scripts/build.sh                 rebuild everything that changed in the guest
#   ./scripts/build.sh drv-pci [...]   force a rebuild of the given Buildroot packages
#   ./scripts/build.sh qemu            rebuild QEMU (after changing qemu/devices)
#   ./scripts/build.sh menuconfig      any other Buildroot target, e.g. linux-menuconfig
source "$(dirname "$0")/common.sh"

[[ -f "${BR_OUT}/.config" ]] || die "Guest not configured. Run ./scripts/setup-buildroot.sh first."
br_sync_config

if [[ $# -eq 0 ]]; then
    br_make
    exit 0
fi

for target in "$@"; do
    if [[ "${target}" == "qemu" ]]; then
        make -C "${QEMU_BUILD}" -j"$(nproc)"
    elif [[ -d "${BR2_EXTERNAL_DIR}/package/${target}" ]]; then
        # Always build our packages from scratch: they are small, and a plain
        # "-rebuild" misses changes in the kernel configuration.
        br_make "${target}-dirclean"
        br_make          # build the package and repack the root filesystem image
    else
        br_make "${target}"
    fi
done
