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
- v0.9: generate exact-size bilingual full badge images, embed both English and Japanese fallbacks, boot Badge mode in 180 degree strap orientation, alternate EN/JA every 15 seconds, and use long press to enter a normal-orientation Home/Debug mode.
- v1.0: production Badge mode with persistent Settings stored in ESP32 Preferences/NVS. Badge boots first, uses strap or handheld orientation according to Settings, supports auto/English/Japanese language mode, and exposes normal-orientation Home, Settings, Debug, and a PaperCoach placeholder.
- v1.1: add the PaperCoach launcher shell with normal-orientation placeholder screens for Interview Practice, Blitz Quiz, Weak Answer Detector, Glossary, and Mock Interview. Badge remains the default boot mode.
- v1.2: add a read-only PaperCoach sample deck engine. It loads `/papercoach/decks/sample_interview.json` from SD when present, otherwise uses an embedded senior AI/Product Manager interview deck with QA, MCQ, weak-answer, glossary, hostile-followup, and metric-precision items.
- v1.3: regenerate English and Japanese full-screen badge fallbacks from one shared layout template. Both languages use the same profile, text, QR, margin, and divider positions; Japanese text is high contrast, the profile photo uses a soft circular treatment, and the public badge still shows no debug labels.
- v1.4: reduce e-ink ghosting on QR/photo zoom transitions by using M5GFX `epd_quality` full refreshes for zoom entry, mode/orientation changes, and a conservative white refresh before returning from zoom to Badge mode.
- v1.5: improve touch navigation with serial touch down/up logs, center long-press Home entry, bottom-left triple-tap emergency Home entry, a full PaperCoach Home/Menu, Debug-only touch diagnostics, and persistent PaperCoach font size controls.

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
- `/paperbadge/badge_en.png`
- `/paperbadge/badge_ja.png`
- `/paperbadge/profile_photo.png`
- `/paperbadge/qr.png`

It also accepts simple fallback variants for SD dynamic mode:

- English badge: `/paperbadge/completeBadge.png`, `/paperbadge/complete_badge.png`, `/paperbadge/badge.png`, `/paperbadge/badge_full.png`, `/paperbadge/full_badge.png`.
- Japanese badge: `/paperbadge/badge_jp.png`, `/paperbadge/badge_japanese.png`, `/paperbadge/completeBadge_ja.png`, `/paperbadge/complete_badge_ja.png`.
- Profile photo: `/paperbadge/profilePhoto.png`, `/paperbadge/photo.png`, `/paperbadge/portrait.png`, `/paperbadge/profile.png`, plus `.jpg` / `.jpeg` variants for the profile names.
- QR: `/paperbadge/qr.JPG`, `/paperbadge/qr.jpg`, `/paperbadge/qr.jpeg`, `/paperbadge/linkedin_qr.png`, `/paperbadge/linkedinQR.png`.

If SD badge images are missing or fail to decode, the public screen falls back to the embedded English/Japanese badge PNGs generated into firmware.

## Embedded Fallback Assets

Build embedded fallback assets from the repo sample data:

```bash
/Users/danieljimenez/AIDevelopment/.venv/bin/python tools/build_embedded_assets.py
```

The script inspects `sample-data/paperbadge`, chooses the most likely full badge for reporting, detects the profile photo, QR code, and `badge.json`, normalizes generated copies under `generated-assets/embedded`, and writes `src/embedded_assets.h`. It never deletes or overwrites the original sample files. In v1.3 both `badge_en.png` and `badge_ja.png` are rendered from one shared Pillow template so English and Japanese keep identical element positions and contrast hierarchy.

Current selected repo assets:

- Full badge: `sample-data/paperbadge/completeBadge.png`
- Generated English badge: `generated-assets/embedded/badge_en.png` at `540x960`
- Generated Japanese badge: `generated-assets/embedded/badge_ja.png` at `540x960`
- Profile photo: `sample-data/paperbadge/profilePhoto.png`
- QR: `sample-data/paperbadge/qr.png`
- JSON: `sample-data/paperbadge/badge.json`

## Orientation

Badge mode defaults to 180 degree strap orientation so the badge appears upright when the PaperS3 is hanging from the neck strap holder. Home/Menu, Debug, Settings, and PaperCoach modes use normal handheld orientation. Physical buttons are not used for app controls.

Long-press the center of the Badge screen to enter Home/Menu. If long press is hard to trigger, triple-tap the bottom-left area of the Badge screen as an emergency Home/Menu fallback. Settings lets you persist:

- Badge orientation: `strap` or `handheld`
- Badge language: `auto`, `English`, or `Japanese`
- PaperCoach font size: `Medium`, `Large`, `XL`, or `Huge`

Settings are stored in ESP32 Preferences/NVS and survive SD card removal.

## Home/Menu

v1.5 Home/Menu entries:

- Badge
- Interview Practice
- Blitz Quiz
- Weak Answer Detector
- Metric Precision
- Hostile Follow-up
- Glossary
- Mock Interview
- Settings
- Debug

PaperCoach screens are read-only in v1.5. There is no progress writing, spaced repetition, RTC scheduling, Wi-Fi, Bluetooth, or AI/API call behavior.

## PaperCoach Decks

Expected SD deck path:

```text
PAPERSD/
  papercoach/
    decks/
      sample_interview.json
```

The repo includes a matching sample file at `sample-data/papercoach/decks/sample_interview.json`.

Supported item types:

- `qa`
- `mcq`
- `weak_answer`
- `glossary`
- `hostile_followup`
- `metric_precision`

PaperCoach modes use normal handheld orientation. Screens are page-based with large touch targets: bottom-left Home returns to Home/Menu, bottom-right Next advances to the next item, and tapping the page reveals the next read-only answer/rubric stage. MCQ screens reveal the explanation after an option tap.

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

v0.9 uses a pre-rendered Japanese badge image generated on the Mac and embedded in firmware. The PaperS3 does not render Japanese text dynamically in Badge mode.

## Download Mode

If upload fails, connect the PaperS3 over USB-C, then long-press the power button until the back status LED flashes red. That indicates download mode. Run the upload command again.

Important: long-pressing the button only enters download mode. It does not erase firmware by itself. Firmware is overwritten only when you flash/upload a new build.

## Notes

- PlatformIO uses `esp32-s3-devkitm-1` because PlatformIO may not have a dedicated PaperS3 board ID. The config still sets PaperS3-compatible 16 MB flash and `qio_opi` memory settings for ESP32-S3R8 / 8 MB OPI PSRAM.
- M5Unified is configured to fall back to `board_M5PaperS3` so the display, SD pins, and buzzer map to PaperS3.
- Official PaperS3 docs: https://docs.m5stack.com/en/core/paperS3
