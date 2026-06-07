# PaperBadge

First working firmware milestone for M5Stack PaperS3 / M5PaperS3 v1.2.

The firmware is intentionally small and built in compile-checked milestones. It does not use Wi-Fi, Bluetooth, cloud calls, custom Japanese fonts, or image decoding yet.

## Version Status

- v0.1: initialize the e-paper display, draw fallback badge text, mount microSD, check `/paperbadge/badge.json`, show `SD OK` or `SD FAIL`, and beep once on boot if the buzzer is available.
- v0.2: parse English badge text from `/paperbadge/badge.json` using ArduinoJson. If SD, JSON, or fields are unavailable, the firmware renders hardcoded English fallback text.

## Hardware

- M5Stack PaperS3 / M5PaperS3 v1.2
- ESP32-S3R8
- 4.7 inch e-paper display
- GT911 touch
- microSD
- Passive buzzer
- USB-C flashing

## Build

```bash
pio run
```

## Upload

```bash
pio run --target upload --upload-port /dev/tty.usbmodem1101
```

The default upload speed is `1500000`. If upload is unstable through a cable or hub, change `upload_speed` in `platformio.ini` to `921600` and retry.

## Monitor

```bash
pio device monitor --port /dev/tty.usbmodem1101
```

The serial monitor prints board/display/flash/PSRAM details, whether the SD card mounted, whether `/paperbadge/badge.json` was found, JSON parse status, and whether rendered text came from JSON or fallback.

## SD Card

Expected SD structure:

```text
PAPERSD/
  paperbadge/
    badge.json
    profile_photo.png
    qr.png
```

For v0.2 only `badge.json` is read. Photos and QR codes are intentionally not loaded yet.

Prepare the sample SD folder locally:

```bash
python3 tools/prepare_assets.py
```

Or copy directly to a mounted SD card:

```bash
python3 tools/prepare_assets.py --output /Volumes/PAPERSD
```

## Download Mode

If upload fails, connect the PaperS3 over USB-C, then long-press the power button until the back status LED flashes red. That indicates download mode. Run the upload command again.

Important: long-pressing the button only enters download mode. It does not erase firmware by itself. Firmware is overwritten only when you flash/upload a new build.

## Notes

- PlatformIO uses `esp32-s3-devkitm-1` because PlatformIO may not have a dedicated PaperS3 board ID. The config still sets PaperS3-compatible 16 MB flash and `qio_opi` memory settings for ESP32-S3R8 / 8 MB OPI PSRAM.
- M5Unified is configured to fall back to `board_M5PaperS3` so the display, SD pins, and buzzer map to PaperS3.
- Official PaperS3 docs: https://docs.m5stack.com/en/core/paperS3
