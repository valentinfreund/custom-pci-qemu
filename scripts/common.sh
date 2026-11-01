# Shared settings, sourced by the other scripts.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY="${REPO_ROOT}/third_party"

QEMU_VERSION="v9.0.0"
QEMU_GIT_URL="${QEMU_GIT_URL:-https://gitlab.com/qemu-project/qemu.git}"
QEMU_SRC="${THIRD_PARTY}/qemu"
QEMU_BUILD="${THIRD_PARTY}/qemu-build"
QEMU_TARGETS="x86_64-softmmu,aarch64-softmmu"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

BUILDROOT_VERSION="2026.02.3"
BUILDROOT_GIT_URL="${BUILDROOT_GIT_URL:-https://gitlab.com/buildroot.org/buildroot.git}"
BUILDROOT_SRC="${THIRD_PARTY}/buildroot"
BR2_EXTERNAL_DIR="${REPO_ROOT}/buildroot-external"
export BR2_DL_DIR="${BR2_DL_DIR:-${THIRD_PARTY}/dl}"

# Guest architecture: x86_64 (default) or later aarch64
ARCH="${ARCH:-x86_64}"
BR_OUT="${THIRD_PARTY}/out-${ARCH}"

# Run make inside the Buildroot output directory of the selected ARCH
br_make() { make -C "${BUILDROOT_SRC}" O="${BR_OUT}" BR2_EXTERNAL="${BR2_EXTERNAL_DIR}" "$@"; }
