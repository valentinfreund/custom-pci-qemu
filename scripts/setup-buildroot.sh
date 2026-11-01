#!/usr/bin/env bash
# Fetch a pinned Buildroot and build the guest system for $ARCH (default x86_64).
source "$(dirname "$0")/common.sh"

mkdir -p "${THIRD_PARTY}"
if [[ ! -d "${BUILDROOT_SRC}/.git" ]]; then
    log "Cloning Buildroot ${BUILDROOT_VERSION}"
    git -c advice.detachedHead=false clone --depth 1 --branch "${BUILDROOT_VERSION}" \
        "${BUILDROOT_GIT_URL}" "${BUILDROOT_SRC}"
fi

if [[ ! -f "${BR_OUT}/.config" ]]; then
    log "Configuring Buildroot for ${ARCH}"
    br_make "driverlab_${ARCH}_defconfig"
fi

log "Building guest system for ${ARCH} (first run takes a while)"
br_make
log "Done: images in ${BR_OUT}/images"
