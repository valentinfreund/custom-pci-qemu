#!/usr/bin/env bash
# Fetch a pinned QEMU, hook in our devices from qemu/devices and build it.
# Safe to re-run: existing checkouts and builds are reused.
source "$(dirname "$0")/common.sh"

mkdir -p "${THIRD_PARTY}"

if [[ ! -d "${QEMU_SRC}/.git" ]]; then
    log "Cloning QEMU ${QEMU_VERSION}"
    git -c advice.detachedHead=false clone --depth 1 --branch "${QEMU_VERSION}" \
        "${QEMU_GIT_URL}" "${QEMU_SRC}"
fi

cd "${QEMU_SRC}"
for p in "${REPO_ROOT}"/qemu/patches/*.patch; do
    if git apply --reverse --check "$p" 2>/dev/null; then
        log "Patch already applied: $(basename "$p")"
    else
        log "Applying $(basename "$p")"
        git apply "$p"
    fi
done

# hw/custom -> our device sources (edit in the repo, QEMU sees it directly)
ln -sfn "${REPO_ROOT}/qemu/devices" "${QEMU_SRC}/hw/custom"

if [[ ! -f "${QEMU_BUILD}/build.ninja" ]]; then
    log "Configuring QEMU (targets: ${QEMU_TARGETS})"
    mkdir -p "${QEMU_BUILD}"
    cd "${QEMU_BUILD}"
    "${QEMU_SRC}/configure" \
        --target-list="${QEMU_TARGETS}" \
        --enable-debug \
        --enable-slirp \
        --disable-docs
fi

log "Building QEMU"
make -C "${QEMU_BUILD}" -j"$(nproc)"
log "Done: ${QEMU_BUILD}/qemu-system-x86_64"
