#!/bin/bash
# smoke.sh — PaperBadge build + boot-log verification
# Run from repo root. Exit 0 = pass, nonzero = fail.
#
# What it does:
#   1. Checks firmware version in source matches expected
#   2. Builds firmware (pio run)
#   3. If device is connected: flashes + captures boot log
#   4. Validates key log lines in captured output
#
# Usage:
#   bash .claude/skills/run-paperbadge/smoke.sh
#   UPLOAD=0 bash .claude/skills/run-paperbadge/smoke.sh   # build only
#   PORT=/dev/cu.usbmodem1103 bash ... .../smoke.sh         # explicit port

set -euo pipefail

UPLOAD="${UPLOAD:-1}"
PORT="${PORT:-/dev/cu.usbmodem1101}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$REPO_ROOT"

echo "=== PaperBadge smoke test ==="

# ── 1. Version check ──────────────────────────────────────────────────────────
EXPECTED_VERSION=$(grep 'kFirmwareVersion' src/main.cpp | grep -o '"v[^"]*"' | tr -d '"')
echo "Source firmware version: $EXPECTED_VERSION"
if [[ -z "$EXPECTED_VERSION" ]]; then
  echo "FAIL: could not read kFirmwareVersion from src/main.cpp"
  exit 1
fi

# ── 2. Build ──────────────────────────────────────────────────────────────────
echo "Building..."
if ! pio run 2>&1 | tail -3; then
  echo "FAIL: pio run failed"
  exit 1
fi
echo "Build: OK"

# ── 3. Device check ───────────────────────────────────────────────────────────
DEVICE_PORT=""
for candidate in /dev/cu.usbmodem*; do
  [[ -e "$candidate" ]] && DEVICE_PORT="$candidate" && break
done

if [[ -z "$DEVICE_PORT" ]]; then
  echo "No device found — skipping upload and serial capture (build-only pass)"
  echo "PASS (build only)"
  exit 0
fi
echo "Device: $DEVICE_PORT"

# ── 4. Upload ─────────────────────────────────────────────────────────────────
if [[ "$UPLOAD" == "1" ]]; then
  echo "Uploading firmware..."
  if ! pio run -t upload --upload-port "$DEVICE_PORT" 2>&1 | tail -5; then
    echo "FAIL: upload failed"
    exit 1
  fi
  echo "Upload: OK"
fi

# ── 5. Capture boot log ───────────────────────────────────────────────────────
echo "Capturing boot log (12s)..."
BOOT_LOG=$(python3 "$SCRIPT_DIR/serial_capture.py" --port "$DEVICE_PORT" --tail 12 2>/dev/null)
echo "$BOOT_LOG"

# ── 6. Validate boot log ─────────────────────────────────────────────────────
FAIL=0

check() {
  local desc="$1"
  local pattern="$2"
  if echo "$BOOT_LOG" | grep -q "$pattern"; then
    echo "  ✓ $desc"
  else
    echo "  ✗ $desc (pattern: $pattern)"
    FAIL=1
  fi
}

echo ""
echo "=== Boot log checks ==="
check "Firmware version"          "PaperBadge+ $EXPECTED_VERSION boot"
check "Sleep resets Off on boot"  "Sleep mode: reset to Off on boot"
check "Wake reason: not sleep"    "Wake reason: not sleep"
check "Power profile logged"      "profile="
check "Badge rendered"            "Badge mode: language="
check "Input unlocked"            "UI input unlocked"
check "Deep sleep blocked note"   "deep=blocked"

echo ""
if [[ "$FAIL" == "0" ]]; then
  echo "PASS — all boot log checks passed"
  exit 0
else
  echo "FAIL — one or more boot log checks failed"
  exit 1
fi
