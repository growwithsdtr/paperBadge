---
name: run-paperbadge
description: Build, flash, and verify PaperBadge firmware. Run this skill to build the firmware, upload to the M5PaperS3 device, capture the boot log, and run automated checks. Use for "build", "run", "upload", "flash", "verify", "smoke test", "serial monitor", or "check boot log".
---

# run-paperbadge

PaperBadge is ESP32-S3 Arduino firmware for the M5Stack PaperS3 e-ink device. There is no GUI
to drive headlessly — the surface is the **serial boot log**. The driver (`serial_capture.py`)
resets the device via esptool and captures the log. `smoke.sh` wraps build + flash + capture
into a single pass/fail verification.

All commands run from repo root. Paths in this skill are relative to repo root.

---

## Prerequisites

```bash
pip3 install pyserial              # required by serial_capture.py
pio --version                      # PlatformIO must be installed
```

PlatformIO install: `brew install platformio` or `pip3 install platformio`.

esptool is bundled by PlatformIO at:
`~/.platformio/packages/tool-esptoolpy/esptool.py`

---

## Build

```bash
pio run
```

Incremental: ~5s. Clean: `pio run --target clean && pio run` (~13s).

Check firmware version in source:

```bash
grep 'kFirmwareVersion' src/main.cpp | grep -o '"v[^"]*"'
```

---

## Run (agent path) — smoke test

The primary verification path. Builds, optionally flashes, then captures and validates the
boot log:

```bash
UPLOAD=0 bash .claude/skills/run-paperbadge/smoke.sh    # build + boot-log check, no re-flash
bash .claude/skills/run-paperbadge/smoke.sh             # build + flash + boot-log check
```

Checks the boot log for:
- Correct firmware version string (`PaperBadge+ vX.Y-devN boot`)
- `Sleep mode: reset to Off on boot (not sticky)`
- `Wake reason: not sleep`
- Power profile logged
- Badge rendered (`Badge mode: language=`)
- Input unlocked
- `deep=blocked` wake source note

Exits 0 on PASS, nonzero on FAIL.

When no device is connected, build-only pass is reported automatically.

---

## Serial capture (individual)

Capture boot log after a hard reset:

```bash
python3 .claude/skills/run-paperbadge/serial_capture.py
```

Tail live serial without resetting (for capturing touch-event logs while driving the device
physically):

```bash
python3 .claude/skills/run-paperbadge/serial_capture.py --no-reset --tail 60
```

Filter for specific events:

```bash
python3 .claude/skills/run-paperbadge/serial_capture.py --grep "screen=" --no-reset --tail 30
python3 .claude/skills/run-paperbadge/serial_capture.py --grep "touch" --no-reset --tail 30
python3 .claude/skills/run-paperbadge/serial_capture.py --grep "power\|sleep\|nap" --no-reset --tail 60
```

---

## Upload (flash firmware)

```bash
pio run -t upload --upload-port /dev/cu.usbmodem1101
```

After upload, the device reboots and starts logging. Run `serial_capture.py` immediately after.

To flash and capture in one step:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem1101 2>&1 | tail -5 && \
  python3 .claude/skills/run-paperbadge/serial_capture.py
```

---

## Run (human path)

1. Connect M5PaperS3 via USB.
2. `pio run -t upload` — flashes and reboots the device.
3. Device shows the Badge screen. Tap **Home** area to navigate.
4. `pio device monitor --port /dev/cu.usbmodem1101 --baud 115200` — live serial.

---

## Physical QA

Touch/screen behavior cannot be automated. For physical QA, run serial in tail mode while
you drive the device, and read the navigation events:

```bash
python3 .claude/skills/run-paperbadge/serial_capture.py --no-reset --tail 120 --grep "screen=\|touch\|hit\|nap\|sleep"
```

Each tap emits:
- `touch down coordinates: x=N y=N screen=Badge`
- `touch up coordinates: x=N y=N screen=Badge`
- `hit target: <name> ...` or `touch ignored: reason=...`

Screen transitions emit e.g.:
- `Settings screen shown: font=Reader L refresh=Fast profile=Responsive orientation=handheld`
- `Advanced screen shown.`
- `Power Lab shown: page=1/3 ...`
- `Badge mode: language=English ...`

---

## Gotchas

**`/dev/tty.usbmodem1101` opens but emits nothing.**
Use `/dev/cu.usbmodem1101` (cu. prefix). On macOS the tty. variant behaves differently for USB-CDC
devices. `serial_capture.py` auto-corrects to `cu.` if given a `tty.` path.

**Device disconnects after upload.**
The M5PaperS3 drops USB after the firmware boots (it restarts the USB stack). Wait ~1s, then
the port reappears as `/dev/cu.usbmodem1101`. `serial_capture.py` handles this with the
esptool reset flow.

**Device emits nothing without a reset.**
When the app is already running, the UART is silent. `serial_capture.py` defaults to sending
an esptool reset pulse. Use `--no-reset` only when you know the device just booted or you're
tailing interaction events.

**SD card errors are expected.**
`sdCommand(): Card Failed! cmd: 0x00` on every boot — there's no SD card inserted during
development. All content falls back to embedded assets. Not a failure.

**`pio device monitor` requires manual Ctrl-C.**
It blocks forever. Use `serial_capture.py` for programmatic capture with a timeout.

**Port number can vary.**
If multiple USB devices are connected, the port may be `/dev/cu.usbmodem1103` etc. Pass
`--port /dev/cu.usbmodem<N>` or set `PORT=/dev/cu.usbmodem<N>` for `smoke.sh`.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `No /dev/cu.usbmodem* device found` | Device not connected or in deep sleep; press the side button |
| `esptool: Cannot open /dev/cu.usbmodem*` | Port in use by another process (another `pio monitor`, Xcode, etc.) |
| `serial.SerialException: [Errno 16] Resource busy` | Kill other serial readers: `lsof /dev/cu.usbmodem1101` then `kill <pid>` |
| Build fails: `No such file or directory: 'pio'` | `pip3 install platformio` or `brew install platformio` |
| `pip3 install pyserial` → `externally-managed` error | `pip3 install --break-system-packages pyserial` or `python3 -m pip install pyserial --user` |
| `FAIL: Firmware version` check fails | Source was edited but not yet built+flashed; run `smoke.sh` with `UPLOAD=1` |
