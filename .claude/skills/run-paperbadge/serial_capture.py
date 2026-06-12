#!/usr/bin/env python3
"""
PaperBadge serial capture driver.

Resets the M5PaperS3 via esptool and captures the boot log, or
tails live serial output for a specified duration.

Usage:
  python3 serial_capture.py              # reset + 12s capture (boot log)
  python3 serial_capture.py --tail 30   # tail without reset for 30s
  python3 serial_capture.py --port /dev/cu.usbmodem1101  # explicit port
  python3 serial_capture.py --grep "screen=" --tail 60   # filter lines

Output goes to stdout. Exit 0 if any lines were captured, 1 if none.

Serial port note (macOS):
  Device appears as /dev/cu.usbmodem<N>, NOT /dev/tty.usbmodem<N>.
  The tty. variant opens but emits nothing. Always use cu. prefix.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

ESPTOOL = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
DEFAULT_PORT = "/dev/cu.usbmodem1101"
DEFAULT_BAUD = 115200


def find_port(hint: str) -> str:
    import glob
    if Path(hint).exists():
        return hint
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    if candidates:
        return candidates[0]
    sys.exit("No /dev/cu.usbmodem* device found. Is the M5PaperS3 connected?")


def reset_device(port: str) -> None:
    """Hard-reset via esptool so the app boots fresh and emits its log."""
    if not ESPTOOL.exists():
        print("[warn] esptool not found at expected pio path; skipping reset", file=sys.stderr)
        return
    result = subprocess.run(
        ["python3", str(ESPTOOL), "--port", port, "--no-stub", "run"],
        capture_output=True, timeout=10
    )
    if result.returncode != 0:
        print(f"[warn] esptool reset exited {result.returncode}", file=sys.stderr)


def capture(port: str, baud: int, duration: float, grep: str | None, reset: bool) -> int:
    import serial

    if reset:
        reset_device(port)
        time.sleep(0.3)  # brief pause for USB re-enumeration

    try:
        s = serial.Serial(port, baud, timeout=0.5)
    except serial.SerialException as e:
        sys.exit(f"Cannot open {port}: {e}")

    start = time.time()
    count = 0
    try:
        while time.time() - start < duration:
            raw = s.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if grep and grep not in line:
                continue
            print(line, flush=True)
            count += 1
    finally:
        s.close()

    print(f"--- {count} lines captured in {duration:.0f}s ---", file=sys.stderr)
    return 0 if count > 0 else 1


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default=DEFAULT_PORT, help="Serial port (default: %(default)s)")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("--tail", type=float, default=12, metavar="SECONDS",
                   help="How many seconds to capture (default: 12)")
    p.add_argument("--no-reset", action="store_true",
                   help="Skip the esptool hard reset (use when device is already running)")
    p.add_argument("--grep", default=None, metavar="PATTERN",
                   help="Only print lines containing this string")
    args = p.parse_args()

    port = find_port(args.port)
    sys.exit(capture(port, args.baud, args.tail, args.grep, not args.no_reset))


if __name__ == "__main__":
    main()
