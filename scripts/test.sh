#!/usr/bin/env bash
# Boot the guest with two custom-pci-device instances and run the driver tests.
# Exit code 0 if all tests passed. The full console log is written to
# third_party/test-<ARCH>.log.
source "$(dirname "$0")/common.sh"

LOG="${THIRD_PARTY}/test-${ARCH}.log"
exec python3 "${REPO_ROOT}/scripts/lib/guest_test.py" "${LOG}" \
    "${REPO_ROOT}/scripts/run.sh" -- -device custom-pci-device
