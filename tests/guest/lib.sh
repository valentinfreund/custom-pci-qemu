# Helpers for guest tests (BusyBox sh).
PASSED=0
FAILED=0

pass() { PASSED=$((PASSED + 1)); echo "PASS: $1"; }
fail() { FAILED=$((FAILED + 1)); echo "FAIL: $1${2:+ ($2)}"; }

# check "<description>" <command...>: PASS if the command succeeds
check() {
    desc="$1"; shift
    if "$@" >/tmp/check.out 2>&1; then pass "$desc"; else fail "$desc" "$(head -c 200 /tmp/check.out)"; fi
}

# expect_eq "<description>" <expected> <actual>
expect_eq() {
    if [ "$2" = "$3" ]; then pass "$1"; else fail "$1" "expected '$2', got '$3'"; fi
}

# Sum of an interrupt line in /proc/interrupts over all CPUs
irq_count() {
    grep "$1" /proc/interrupts | awk '{ s = 0; for (i = 2; i <= NF; i++) if ($i ~ /^[0-9]+$/) s += $i; else break; t += s } END { print t + 0 }'
}

# Kernel log lines that indicate a driver bug
kernel_errors() { dmesg | grep -E 'BUG:|WARNING:|Oops|cut here|general protection' ; }
