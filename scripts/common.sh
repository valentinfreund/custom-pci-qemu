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
