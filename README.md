# PaperBadge

Firmware for a custom PaperBadgePlus e-ink badge on M5Stack PaperS3 / M5PaperS3 v1.2.

The firmware is intentionally simple and compile-checked in milestones. It does not enable Wi-Fi/Bluetooth features or make cloud calls; power diagnostics use only local M5Unified/ESP32 APIs.

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
- v1.6: convert the real Markdown interview prep sheet at `sample-data/papercoach/interview_prep_sheet3.md` into generated PaperCoach JSON. The converter extracts 71 cards, including 17 must-master cards, writes generated deck/drill/glossary JSON, and prepares an SD-ready deck copy under `dist/sdcard/papercoach/decks/`.
- v1.7: embed the full 71-card PaperCoach interview deck in firmware as a flash-resident C++ fallback. Runtime first tries SD override `/papercoach/decks/interview_cards.json`; if SD is missing or parsing fails, PaperCoach still has the full embedded deck.
- v1.8: make Interview Practice useful with real deck cards, question/spoken/anchor/watch pages, long-answer pagination, All vs Must-master filter, left/right page zones, and PaperCoach font-size support.
- v1.9: add objective PaperCoach drills generated offline from the real deck: Blitz Quiz MCQs, Weak Answer Detector options, Metric Precision phrasing drills, and Hostile Follow-up prompts. The embedded fallback includes 149 drills.
- v2.0: add a PaperCoach typography/layout foundation for physical readability using M5GFX Gothic fonts, default new installs to XL PaperCoach text, add larger touch targets, persist Normal/Clean refresh mode, and lock touch input during e-ink refresh/debounce windows.
- v2.1: add reusable text wrapping and layout diagnostics, wrap MCQ option buttons inside their bounds, grow option buttons for multi-line labels, split drill result/explanation onto a separate page, shorten Metric Precision option labels, and log vertical budget/overflow warnings over Serial.
- v2.2: replace the overlapping PaperCoach top-level modes with outcome-based navigation: Badge, Practice, Drills, Exam, Glossary, Results, Settings, and Debug. Practice opens the existing answer-prep flow, Drills contains category choices, and Exam/Results are readable placeholders.
- v2.3: add Debug → Font Lab, persistent typography presets, and contrast presets. The default app typography is now `Large Reader` + `XL`, using real M5GFX `FreeSansBold` fonts for heavier English UI text and Japanese Gothic for Japanese samples/runtime fallback.
- v2.4: add simple monochrome line icons to top-level Home menu items and add a primitive house icon to Home buttons. Icons are drawn with display primitives rather than icon fonts, so text labels remain the fallback.
- v2.5: make Badge mode static by default. Badge language defaults to Manual toggle, center tap switches English/Japanese, current language is persisted, and auto-rotate only runs when explicitly selected with a 15s/30s/60s interval.
- v2.6: add persistent Power Mode settings, turn off unused Wi-Fi/Bluetooth/speaker behavior at boot, silence the boot buzzer, and add conservative idle logging for Battery Saver and Conference Badge modes. Deep/light sleep is documented but deferred until PaperS3 touch wake reliability is physically verified.
- v2.7: add Fast/Balanced/Clean refresh modes with an adaptive e-paper policy. Badge/image/zoom transitions still get clean refreshes, normal text navigation can use faster updates, and a hard-clean counter forces a clean refresh after 14 non-clean transitions.
- v2.8: make High Contrast + XXL the recommended typography default for new installs, add an XXL font size between XL and Huge, tighten PaperCoach line spacing, add Debug -> Reset typography, and expand Font Lab with direct candidate comparisons.
- v2.9: improve touch responsiveness by reducing input debounce windows, using padded invisible hitboxes around buttons, logging matched hit targets and ignored-touch reasons, and preventing power idle while touch is active or immediately after interaction.
- v3.0: polish button alignment and icon affordances. Buttons now center text or icon+label groups vertically and horizontally, compact PaperCoach Home controls are icon-only, and top-level menu icons use stronger primitive-drawn monochrome shapes instead of thin line icons.
- v3.1: regenerate embedded English/Japanese badge images from one high-contrast 3-line template, remove rendered location/footer text, move the QR upward, and preprocess the portrait offline for sharper 16-level grayscale e-ink output.
- v3.2: add battery and USB/VBUS status to Settings and Debug, add serial power audit logs, keep battery indicators off Badge/PaperCoach screens unless future critical-low handling is added, and lengthen Conference Badge idle entry to about 30 seconds for better touch reliability.
- v3.3a: diagnose the PaperCoach font path, replace the five nominal size choices with three visibly distinct reader sizes, mark legacy XXL/Huge as colliding buckets, add Font Lab measurement rows with actual height/width/line-height metrics, add Tight/Normal/Loose line spacing, and sanitize live PaperCoach text to avoid square-box glyphs.
- v3.4: replace raw-character Practice pagination with sanitized, measured wrapped-line pagination, compact the reading header/footer, move page information into Back/Next buttons, remove deck/filter labels from reading screens, and auto-fit long Reader L Practice content down to Reader M.
- v3.5: add an SD-only intermediate reader font experiment. Font Lab probes `/paperbadge/fonts/reader_mid.vlw` when present, logs actual height/width, and always falls back to built-in reader fonts when the file is missing or fails to load.

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

v3.1 keeps that shared template but renders only three text lines on the public badge: name, role, and `0→1 AI, SaaS & FinTech`. Location and footer text remain in `badge.json` for source compatibility but are no longer rendered into the embedded fallback badge. The QR is moved upward, and the profile image is preprocessed offline with autocontrast, mild contrast/sharpening, and 16-level grayscale quantization preview. No runtime photo processing is done on the ESP32.

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
- Badge language: `Manual toggle`, `English`, `Japanese`, or `Auto rotate`
- Badge auto-rotate interval: `Off`, `15s`, `30s`, or `60s`
- PaperCoach reader size: `Reader S`, `Reader M`, or `Reader L`
- Refresh mode: `Fast`, `Balanced`, or `Clean`
- Power mode: `Normal`, `Battery Saver`, or `Conference Badge`

Settings are stored in ESP32 Preferences/NVS and survive SD card removal.

## Power / Battery Behavior

v2.6 keeps the power policy conservative so the device does not become hard to wake during a conference. At boot, firmware explicitly turns Wi-Fi off, stops Bluetooth, stops the speaker, disables the boot beep, and keeps IMU polling disabled in `M5.config()`.

Power modes:

- `Normal`: standard touch responsiveness. No idle entry while actively using PaperCoach.
- `Battery Saver`: keeps the same screens available, uses a slower loop delay, and enters a logged idle state after about 180 seconds of inactivity.
- `Conference Badge`: intended for static badge use. After the badge is drawn and left untouched for about 30 seconds, firmware enters a logged idle state and slows the loop delay.

The idle state is not ESP32 deep sleep or light sleep. M5Unified exposes deep/light sleep APIs and the ESP32-S3 reports wake causes, but PaperS3 touch wake behavior must be verified on the physical device before enabling sleep that could make the badge difficult to wake. Serial logs report the selected power mode, idle entry/exit, and boot wake reason.

v3.2 shows power status only in diagnostic contexts: Settings has a compact battery/USB line, and Debug shows battery voltage, approximate percent, charge/discharge state, battery current, USB/VBUS status, current power mode, and the radio/peripheral policy. Badge, Practice, and Drills do not show a battery indicator. Percent is whatever M5Unified reports when available, with a conservative voltage estimate fallback.

The power audit log confirms the app policy: Wi-Fi off, Bluetooth stopped, IMU disabled in `M5.config()`, speaker stopped, badge language mode/current language, redraw count, VBUS, battery voltage, charge state, and sleep deferred.

## E-Ink Refresh Policy

v2.7 adds case-by-case refresh control:

- `Fast`: uses fast text refreshes where possible, but still performs clean refreshes for badge, image, language, orientation, and zoom transitions.
- `Balanced`: default for new installs. It performs clean refreshes on mode changes and image-heavy transitions, then uses faster updates for normal PaperCoach text navigation.
- `Clean`: prioritizes ghosting reduction and uses clean refreshes more often. Zoom exit still performs an explicit white clean refresh before returning to Badge.

Firmware tracks consecutive non-clean transitions and forces a clean refresh after 14 transitions regardless of mode. Serial logs include refresh mode, refresh reason, actual refresh type, transition count, and whether the hard-clean counter fired.

## Touch Responsiveness

v2.9 keeps the e-ink input lock, but reduces the post-refresh lockout:

- Fast/text refresh debounce: `250 ms`
- Clean refresh debounce: `600 ms`

Buttons use an invisible `10 px` hitbox expansion around their visible bounds. Serial logs include touch down/up coordinates, matched target name, ignored touch reason, last refresh end time, and active debounce. Debug shows the same diagnostics on-device. `Clean` refresh mode can still feel slower because a clean e-paper update takes longer and uses the longer debounce window; `Balanced` remains the recommended mode for normal PaperCoach use.

## Home/Menu

v2.2 Home/Menu entries:

- Badge
- Practice
- Drills
- Exam
- Glossary
- Results
- Settings
- Debug

Practice uses the real embedded or SD interview deck and supports page-based study. Drills contains All Drills, Weak Answer, Metric Precision, Follow-up Defense, Framework Choice, and Maturity Claim categories. Exam is a placeholder for a future 10-question readiness test. Results is a placeholder and shows no session results yet. There is no progress writing, spaced repetition, RTC scheduling, Wi-Fi, Bluetooth, or AI/API call behavior.

## PaperCoach Typography

v2.0 started with real M5GFX `lgfxJapanGothic_*` fonts rather than offset-drawing fake bold text. Main content is black; metadata is dark gray.

v3.3a defaults PaperCoach typography to `High Contrast` + `Reader L` + `Tight` line spacing. Existing NVS values are preserved but legacy `XXL` and `Huge` are canonicalized to `Reader L`. Serial logs include the current screen, reader size, underlying font bucket, measured text height/width, line height, refresh mode, and input lock/unlock state.

v2.3 adds typography presets:

- `Sans Thin/current`: Japanese Gothic, the original v2.0/v2.2 look.
- `Large Reader`: real `FreeSansBold` for heavier English UI text; default.
- `Sans Bold-like`: real `FreeSansBold` with less size expansion than Large Reader.
- `High Contrast`: real `FreeSansBold`, darker metadata, larger spacing.
- `Debug Mono`: real `FreeMonoBold`, intended for diagnostics only.

Font Lab is available from Debug and cycles font style, font size, and contrast directly on-device. Japanese sample text uses `lgfxJapanGothic_*` because the FreeSans/FreeMono GFX fonts are ASCII/Latin-oriented.

v2.8 changes the recommended new-device typography default to `High Contrast` + `XXL` + `Max` contrast. Existing Preferences/NVS values are preserved; use `Debug -> Reset typography` to force the new default. PaperCoach line spacing is tighter than v2.7, especially for Practice and Drills. Font Lab now shows direct comparison rows for High Contrast XL, High Contrast XXL, High Contrast Huge, Sans Bold-like XXL, and Large Reader XXL. Serial typography logs include font style, size, body/button pixels, line height, and button height.

v3.3a changes the PaperCoach typography model from nominal pixel steps to physical reader buckets:

- `Reader S`: High Contrast body maps to `FreeSansBold12pt7b`
- `Reader M`: High Contrast body maps to `FreeSansBold18pt7b`
- `Reader L`: High Contrast body maps to `FreeSansBold24pt7b`

The previous `XXL` and `Huge` values remain supported internally for Preferences/NVS compatibility, but they are canonicalized to `Reader L` because they collide with the same rendered font bucket. Font Lab now reports the active style, logical/effective size, underlying M5GFX font name/type, actual `fontHeight()`, measured sample width, line height, and collisions over Serial and on screen.

Current runtime PaperCoach fonts:

- Headings: M5GFX `FreeSansBold18pt7b` or `FreeSansBold24pt7b` for High Contrast/Large Reader/Sans Bold-like; `lgfxJapanGothic_*` for Sans Thin/current; `FreeMonoBold*` for Debug Mono.
- Body text: M5GFX `FreeSansBold12pt7b`, `FreeSansBold18pt7b`, or `FreeSansBold24pt7b` for the three reader sizes.
- Buttons: M5GFX `FreeSansBold12pt7b` or `FreeSansBold18pt7b` depending on reader size.
- Metadata/footer/debug: smaller M5GFX FreeSansBold/FreeMono/JapanGothic buckets.

These are embedded/discrete font faces, not a continuous scalable TTF renderer. M5GFX supports `loadFont()` for VLW runtime fonts from flash arrays or filesystems; v3.3a adds an experimental Font Lab-only probe for `/paperbadge/fonts/reader.vlw` on SD, and v3.5 adds the intermediate reader probe `/paperbadge/fonts/reader_mid.vlw`. No external font file is bundled in firmware, so there is no new font license/source to track and no flash cost from a reader font asset.

The local project does not include a verified Atkinson/Inter/Plex/Source Sans reader asset. A scan of common local Mac font folders found system Noto fonts but no repo-owned open-license intermediate reader file ready to convert. To test a real Latin reader font later, generate a legally licensed VLW file on the Mac and place it at:

```text
PAPERSD/
  paperbadge/
    fonts/
      reader_mid.vlw
      reader.vlw
```

`reader_mid.vlw` is the preferred v3.5 experiment path for a 20-22pt-equivalent font. Font Lab reports whether it was found, loaded, its type, height, width, and whether fallback was used.

Live PaperCoach rendering now sanitizes UTF-8 punctuation at draw time for both SD and embedded decks. It replaces curly quotes, en/em dashes, bullets, ellipses, non-breaking spaces, arrows, and other non-ASCII glyphs with ASCII fallbacks before wrapping/drawing. Badge assets are image-rendered and are not sanitized.

v2.1 adds serial layout diagnostics for physical debugging. Each PaperCoach render logs important bounding boxes, computed text height, available height, page count, and overflow warnings. The Debug screen has a `Layout log` action. Screenshot-to-SD is not enabled in this checkpoint because the public M5GFX/PaperS3 display API does not expose a simple, safe BMP/PNG save path for the active e-paper framebuffer in this firmware shape.

## PaperCoach Decks

Expected SD deck path:

```text
PAPERSD/
  papercoach/
    decks/
      sample_interview.json
```

The repo includes a matching sample file at `sample-data/papercoach/decks/sample_interview.json`.

Convert the real Markdown prep sheet into generated deck files:

```bash
/Users/danieljimenez/AIDevelopment/.venv/bin/python tools/convert_prep_sheet.py
```

Generated outputs:

- `generated/papercoach/interview_cards.json`
- `generated/papercoach/drills.json`
- `generated/papercoach/glossary.json`
- `dist/sdcard/papercoach/decks/interview_cards.json`

Build the firmware embedded PaperCoach fallback header:

```bash
/Users/danieljimenez/AIDevelopment/.venv/bin/python tools/build_embedded_papercoach.py
```

The generated header is `src/embedded_papercoach_deck.h`. It embeds the runtime card fields in flash rather than loading the full JSON document into RAM at boot.

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
