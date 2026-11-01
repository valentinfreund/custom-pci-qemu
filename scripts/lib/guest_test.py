#!/usr/bin/env python3
"""Boot the guest through scripts/run.sh, log in on the serial console,
run the guest test runner and report the result.

Usage: guest_test.py <log file> <run.sh command line...>
Exit code: 0 if all tests passed, 1 otherwise.
"""
import os
import re
import select
import subprocess
import sys
import time

BOOT_TIMEOUT = 60
TEST_TIMEOUT = 120
PROMPT = b"# "


class Console:
    def __init__(self, cmd, log):
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
        self.log = log
        self.buf = b""

    def expect(self, pattern, timeout):
        """Read until the regex pattern appears. Returns the match or None."""
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.buf)
            if m:
                self.buf = self.buf[m.end():]
                return m
            ready, _, _ = select.select([self.proc.stdout], [], [], 0.5)
            if not ready:
                continue
            data = os.read(self.proc.stdout.fileno(), 4096)
            if not data:          # QEMU exited
                return None
            self.log.write(data)
            self.log.flush()
            self.buf += data
        return None

    def send(self, line):
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()

    def close(self):
        self.proc.kill()
        self.proc.wait()


def main():
    log_path, cmd = sys.argv[1], sys.argv[2:]
    with open(log_path, "wb") as log:
        con = Console(cmd, log)
        try:
            if not con.expect(rb"login: ", BOOT_TIMEOUT):
                print("ERROR: guest did not boot (see %s)" % log_path)
                return 1
            con.send("root")
            con.expect(re.escape(PROMPT), 10)
            con.send("dmesg -n 1")          # keep kernel messages off the console
            con.expect(re.escape(PROMPT), 10)
            con.send("driverlab-test")
            m = con.expect(rb"DRIVERLAB-RESULT: (\d+) passed, (\d+) failed", TEST_TIMEOUT)
            con.send("dmesg > /dev/null; dmesg | tail -n 50")   # kernel log for the record
            con.expect(re.escape(PROMPT), 10)
        finally:
            con.close()

    with open(log_path, "rb") as log:
        for line in log.read().decode(errors="replace").splitlines():
            if line.startswith(("PASS:", "FAIL:", "===")):
                print(line)
    if not m:
        print("ERROR: tests did not finish (guest crashed or hung, see %s)" % log_path)
        return 1
    passed, failed = int(m.group(1)), int(m.group(2))
    print("\n%d passed, %d failed  (full log: %s)" % (passed, failed, log_path))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
