# PaperBadge

Firmware for a custom PaperBadgePlus e-ink badge on M5Stack PaperS3 / M5PaperS3 v1.2.

The firmware is intentionally simple and compile-checked in milestones. It does not use Wi-Fi, Bluetooth, cloud calls, or ESP-IDF.

## Version Status

- v0.1: initialize the e-paper display, draw fallback badge text, mount microSD, check `/paperbadge/badge.json`, show `SD OK` or `SD FAIL`, and beep once on boot if the buzzer is available.
- v0.2: parse English badge text from `/paperbadge/badge.json` using ArduinoJson. If SD, JSON, or fields are unavailable, the firmware renders hardcoded English fallback text.
- v0.3: prepare image assets on the Mac and let firmware check whether `/paperbadge/profile_photo.png` and `/paperbadge/qr.png` exist. Firmware draws placeholder boxes only; it does not decode images yet.
- v0.4: draw `/paperbadge/profile_photo.png` and `/paperbadge/qr.png` from SD using M5GFX PNG decoding. If an image is missing or fails to decode, the firmware logs it and draws a placeholder.
- v0.5: switch English/Japanese every `interval_seconds` seconds and switch immediately on a center tap. Japanese uses M5GFX `efontJA_16` when JSON Japanese text is present; otherwise it uses romanized fallback text.
- v0.6: add portrait and landscape badge layouts. JSON `orientation` can start in portrait or landscape, `strap_orientation: 2` applies upside-down rotation, center tap switches language, and top-right tap toggles layout. IMU auto-rotate is intentionally not used.
- v0.7: tap the QR or photo area for a full-screen zoom view. Tap a zoom view to return to the badge. QR zoom keeps a white margin for scanner readability.
- v0.8: generate embedded fallback assets from `sample-data/paperbadge`, render a polished public badge without debug labels, prefer SD dynamic assets when JSON/photo/QR are available, and fall back to an embedded full-badge PNG when SD or component assets are missing.

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

The serial monitor prints board/display/flash/PSRAM details, whether the SD card mounted, whether `/paperbadge/badge.json`, profile, and QR assets were found, JSON parse status, embedded fallback sizes, and whether the public screen used SD dynamic mode or embedded fallback mode.

## SD Card

Expected SD structure:

```text
PAPERSD/
  paperbadge/
    badge.json
    profile_photo.png
    qr.png
```

For v0.8 the firmware first tries these exact paths:

- `/paperbadge/badge.json`
- `/paperbadge/profile_photo.png`
- `/paperbadge/qr.png`

It also accepts simple fallback variants for SD dynamic mode:

- Profile photo: `/paperbadge/profilePhoto.png`, `/paperbadge/photo.png`, `/paperbadge/portrait.png`, `/paperbadge/profile.png`, plus `.jpg` / `.jpeg` variants for the profile names.
- QR: `/paperbadge/qr.JPG`, `/paperbadge/qr.jpg`, `/paperbadge/qr.jpeg`, `/paperbadge/linkedin_qr.png`, `/paperbadge/linkedinQR.png`.
- Full badge on SD, for inspection/logging: `/paperbadge/badge_full.png`, `/paperbadge/badge.png`, `/paperbadge/full_badge.png`, `/paperbadge/badge_en.png`, `/paperbadge/complete_badge.png`, `/paperbadge/completeBadge.png`.

If SD dynamic mode cannot load `badge.json`, a profile image, and a QR image, the public screen falls back to the embedded full-badge PNG generated into firmware.

## Embedded Fallback Assets

Build embedded fallback assets from the repo sample data:

```bash
python3 tools/build_embedded_assets.py
```

The script inspects `sample-data/paperbadge`, chooses the most likely full badge, profile photo, QR code, and `badge.json`, normalizes generated copies under `generated-assets/embedded`, and writes `src/embedded_assets.h`. It never deletes or overwrites the original sample files. On macOS it uses the built-in `sips` command, so Pillow is not required for this script.

Current selected repo assets:

- Full badge: `sample-data/paperbadge/completeBadge.png`
- Profile photo: `sample-data/paperbadge/profilePhoto.png`
- QR: `sample-data/paperbadge/qr.png`
- JSON: `sample-data/paperbadge/badge.json`

## SD Asset Preparation

Install the image conversion dependency once:

```bash
python3 -m pip install Pillow
```

Prepare assets in a mounted SD card `paperbadge` folder:

```bash
python3 tools/prepare_assets.py /Volumes/PAPERSD/paperbadge
```

The script converts `profilePhoto.png`, `profilePhoto.jpg`, or `profilePhoto.jpeg` to `profile_photo.png` as a 220x220 grayscale PNG. It converts `qr.JPG`, `qr.jpg`, `qr.jpeg`, or `qr.png` to `qr.png` as a 320x320 high-contrast black/white PNG. Original files are not deleted.

## Japanese Text

v0.8 keeps the public badge in English only. Earlier Japanese text experiments compile with built-in M5GFX fonts, but the visual quality is not ready for the public badge screen. The next step is either an embedded Japanese full-badge image or a dedicated Japanese font asset.

## Download Mode

If upload fails, connect the PaperS3 over USB-C, then long-press the power button until the back status LED flashes red. That indicates download mode. Run the upload command again.

Important: long-pressing the button only enters download mode. It does not erase firmware by itself. Firmware is overwritten only when you flash/upload a new build.

## Notes

- PlatformIO uses `esp32-s3-devkitm-1` because PlatformIO may not have a dedicated PaperS3 board ID. The config still sets PaperS3-compatible 16 MB flash and `qio_opi` memory settings for ESP32-S3R8 / 8 MB OPI PSRAM.
- M5Unified is configured to fall back to `board_M5PaperS3` so the display, SD pins, and buzzer map to PaperS3.
- Official PaperS3 docs: https://docs.m5stack.com/en/core/paperS3
