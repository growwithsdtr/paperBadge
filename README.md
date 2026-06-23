# PaperBadge

Firmware for a custom PaperBadgePlus e-ink badge on M5Stack PaperS3 / M5PaperS3 v1.2.

The firmware is intentionally simple and compile-checked in milestones. It does not enable Wi-Fi/Bluetooth features or make cloud calls; power diagnostics use only local M5Unified/ESP32 APIs.

## Current Overview

PaperBadge is the hardware shell for the M5PaperS3 e-ink device. PaperCoach is the generic offline learning mode that runs inside that shell.

The current embedded PaperCoach content pack is senior AI/Product Manager interview practice. Future content packs should be able to support offline Japanese N3-style grammar, vocabulary, and kanji drills, including weak-area tracking and SRS-like review, without requiring Wi-Fi, cloud calls, or live AI services.

It boots into a static conference badge and exposes a normal-orientation Home menu with Badge, Interview, Japanese, Reader, and Settings. Diagnostic tools are under Settings -> Advanced.

Current firmware version in source: `v5.9-dev3`.

## Quick Commands

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbmodem1101
python3 .claude/skills/run-paperbadge/serial_capture.py --no-reset --tail 30
UPLOAD=0 bash .claude/skills/run-paperbadge/smoke.sh
```

Default upload and monitor port: `/dev/cu.usbmodem1101` (macOS — use `cu.` prefix).

## Current SD Paths

- `/paperbadge/badge.json`: optional badge config.
- `/paperbadge/badge_en.png`, `/paperbadge/badge_ja.png`: optional full badge image overrides.
- `/paperbadge/profile_photo.png`, `/paperbadge/qr.png`: optional zoom assets.
- `/papercoach/decks/interview_cards.json`: optional card deck override.
- `/papercoach/glossary.json`: optional glossary override.
- `/papercoach/debug/render_trace.txt`: render trace output.
- `/papercoach/debug/embedded_deck_dump.md`: on-device deck text export.
- `/papercoach/progress/session_results.json`: SD-backed drill/exam session results.
- `/paperBadge/books/*.txt`, `/paperBadge/books/*.md`: Reader MVP books.
- `/paperBadge/library_index.json`: Reader SD index cache.
- `/paperBadge/reader_state.json`: last Reader file/page/offset.

## PaperCoach Modes

- Practice: choose Must cards, All cards, Continue last card, or Help/Legend; read cards with content taps for page turns and footer arrows for previous/next card.
- Drills: answer MCQ-style drill items; question and choices render together when they fit, and split choice pages repeat question context. After answering, all result pages are reachable before entering feedback.
- Exam: run a 5- or 10-question mixed exam with no immediate feedback; summary appears at the end.
- Glossary: category grid for AI/RAG, Evals, Metrics, Product, and Interview terms.
- Results: paginated e-ink summary, category bars, weakest areas, recent misses, and recommended next practice.
- Reader: SD-backed TXT/Markdown reader with simple pagination, page-turn tap zones, font-size cycle, and saved progress. EPUB is indexed as a future format stub.
- Settings: reader size, refresh mode, power profile, orientation. Advanced (under Settings) contains typography lab, render trace export, deck export, visual QA, font lab, and power diagnostics.

## Current UX Decisions

- Outlined buttons are preferred over black-filled buttons because large black fills ghost on e-ink.
- Reader M is the recommended default for QA.
- Sans Bold-like is the recommended English default text style. High Contrast remains useful when maximum density/contrast is needed.
- Balanced refresh is the recommended default: clean refresh for major transitions/feedback/badge, faster refresh for ordinary page turns.
- Badge language should use Manual toggle during QA; Auto interval should stay Off unless explicitly testing auto-rotate.
- Deep sleep touch wake is blocked because PaperS3 GT911 INT is GPIO48, which is not RTC-wake capable on ESP32-S3.
- Light sleep can use timer wake plus GT911 GPIO wake; verify behavior on hardware before relying on it for long unattended sessions.
- Static non-Badge reading and diagnostic screens can enter a logged light idle state after inactivity; touch remains active.

## Badge And Power Behavior

Badge mode is static by default and uses e-ink as intended: render once, hold the image, and avoid redraws unless the user acts. Wi-Fi and Bluetooth are shut off at boot and during power policy refreshes; speaker output is stopped; IMU polling is disabled in `M5.config()`. Sleep controls are under Settings -> Advanced -> Power Lab; Light uses short light-sleep cycles with timer wake and best-effort GT911 GPIO wake, and Deep experiment is blocked until a verified RTC wake source exists.

## Known Limitations

- Deep sleep touch wake is not enabled automatically.
- Reader TXT pagination is intentionally simple in this MVP and caps loaded text at 220 KB.
- EPUB, CBZ/manga, and PDF rendering are not implemented.
- Results are session-oriented; SD persistence writes the current session only.
- Glossary search and SRS are not implemented yet.
- Per-option drill explanations are not embedded yet, so feedback omits the weaker-options block and logs the missing detail.
- No Wi-Fi, Bluetooth, API, cloud, audio, paid API, or LLM calls are used.

## Later TODO

- UTF-8/Japanese live text rendering.
- Dynamic deck-defined categories.
- Generic stages array.
- Category cap increase.
- Glossary search.
- SRS/long-term history.

## Product Docs

- [PaperCoach PRD](docs/PAPERCOACH_PRD.md)
- [QA Guide](docs/QA_GUIDE.md)
- [Content Schema](docs/CONTENT_SCHEMA.md)
- [Power Notes](docs/POWER_NOTES.md)
- [Reader and Power Test Plan](docs/READER_AND_POWER_TEST_PLAN.md)
- [Asset Notes](docs/ASSET_NOTES.md)

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
- v3.6: compact footer/button geometry, keep the Home footer control icon-only, retain large invisible hitboxes, and left-align wrapped MCQ option text so answers are easier to compare on the e-ink panel.
- v3.7: add a visual battery bar to Settings and Debug, expand power audit logs with static-badge and loop-delay state, document the conservative no-sleep policy, and keep adaptive refresh behavior focused on reading vs image/zoom transitions.
- v3.8: add bolder hand-drawn primitive Back/Next icons, use icon + page-count Practice footer buttons, and add Debug -> Visual QA with the physical screenshot checklist and current typography/refresh/power settings.
- v5.5: stabilize live headers with ASCII separators, simplify drill/exam/feedback chrome, remove dominant missing-explanation feedback text, loosen post-summary Results pagination, add static-screen light idle, expand Power Audit, and remove stale repo bloat.
- v5.6: fix embedded drill answer-key remapping so trimmed options retain the correct answer, add runtime answer-key validation, improve feedback/results/help/settings readability, add prompt-specific drill/exam typography, preserve paragraph gaps in Practice answers, and add guarded idle CPU scaling.
- v5.9-dev1: add a Japanese Home entry with its own self-contained mode — Daily Questions (embedded N3-sample Week 1 Day 1 set, immediate feedback), Reference, RAM-only Results, and a Mock Test placeholder. Adds an isolated Japanese-safe sanitize/wrap/font path (`lgfxJapanGothic_*`) that never touches the existing English sanitize/wrap/font functions, Interview Practice/Drills/Exam/Glossary/Results behavior, Badge behavior, or deep sleep.

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
pio run --target upload --upload-port /dev/cu.usbmodem1101
```

The default upload speed is `1500000`. If upload is unstable through a cable or hub, change `upload_speed` in `platformio.ini` to `921600` and retry.

## Monitor

```bash
pio device monitor --port /dev/cu.usbmodem1101
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

- Reference-only full badge: `docs/assets/reference/completeBadge.png`
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

- `Normal`: standard touch responsiveness. Static reading/diagnostic screens can enter light idle after about 90 seconds of inactivity.
- `Battery Saver`: keeps the same screens available, uses a slower loop delay, and enters a logged idle state after about 180 seconds of inactivity.
- `Conference Badge`: intended for static badge use. After the badge is drawn and left untouched for about 30 seconds, firmware enters a logged idle state and slows the loop delay.

The PaperCoach static-screen idle state is not ESP32 deep sleep or light sleep; it slows the loop while leaving touch responsive. Badge Sleep Light remains a separate timer-based light-sleep experiment for static Badge mode only. M5Unified exposes deep/light sleep APIs and the ESP32-S3 reports wake causes, but PaperS3 touch wake behavior must be verified on the physical device before enabling sleep that could make the badge difficult to wake. Serial logs report the selected power mode, idle entry/exit, and boot wake reason.

v3.7 shows power status only in diagnostic contexts: Settings has a compact battery/USB line plus a horizontal battery bar, and Debug shows the same bar with battery voltage, approximate percent, charge/discharge state, battery current, USB/VBUS status, current power mode, and the radio/peripheral policy. Badge, Practice, and Drills do not show a battery indicator. Percent is whatever M5Unified reports when available, with a conservative voltage estimate fallback.

The power audit log confirms the app policy: battery voltage/percent/current, USB/VBUS, Wi-Fi, Bluetooth, speaker, IMU, SD status, refresh mode, idle status, sleep mode/status, last wake reason, badge language mode/current language, auto-rotate interval, whether the badge is currently static, redraw count, loop delay, and last refresh reason.

## E-Ink Refresh Policy

v2.7 adds case-by-case refresh control:

- `Fast`: uses fast text refreshes where possible, but still performs clean refreshes for badge, image, language, orientation, and zoom transitions.
- `Balanced`: default for new installs. It performs clean refreshes on mode changes and image-heavy transitions, then uses faster updates for normal PaperCoach text navigation.
- `Clean`: prioritizes ghosting reduction and uses clean refreshes more often. Zoom exit still performs an explicit white clean refresh before returning to Badge.

Balanced remains the recommended default for reading. It avoids full refreshes on every normal Practice/Drills tap, but still uses clean refreshes for mode changes requested as high-quality renders and for badge/image/zoom/language/orientation transitions. `Fast` is available for extra battery savings, and `Clean` is available when ghosting is more important than speed or energy.

Firmware tracks consecutive non-clean transitions and forces a clean refresh after 14 transitions regardless of mode. Serial logs include refresh mode, refresh reason, actual refresh type, transition count, and whether the hard-clean counter fired.

## Touch Responsiveness

v2.9 keeps the e-ink input lock, but reduces the post-refresh lockout:

- Fast/text refresh debounce: `250 ms`
- Clean refresh debounce: `600 ms`

Buttons use an invisible `10 px` hitbox expansion around their visible bounds. Serial logs include touch down/up coordinates, matched target name, ignored touch reason, last refresh end time, and active debounce. Debug shows the same diagnostics on-device. `Clean` refresh mode can still feel slower because a clean e-paper update takes longer and uses the longer debounce window; `Balanced` remains the recommended mode for normal PaperCoach use.

## Home/Menu

Current Home/Menu entries:

- Badge
- Practice
- Drills
- Exam
- Glossary
- Results
- Settings
- Japanese

Practice uses the embedded or SD deck and supports page-based study. Drills contains All Drills plus current deck-specific categories. Exam runs 5- or 10-question mixed sessions without immediate feedback. Results shows RAM/SD-backed session analytics. Diagnostic tools (render trace, deck export, visual QA, font lab, power audit) are under Settings → Advanced. Japanese is a separate, self-contained N3-sample mode (Daily Questions, Mock Test placeholder, Reference, Results) — see "Japanese Text" below. There is no spaced repetition, RTC scheduling, Wi-Fi, Bluetooth, or AI/API call behavior.

## Visual QA

Settings → Advanced → Visual QA shows the current reader size/style, refresh mode, power mode, whether `/paperbadge/fonts/reader_mid.vlw` was detected, and a short checklist for physical screenshots:

- Practice first page
- Practice long answer page
- Practice last page
- Drills MCQ screen
- Settings battery area
- Font Lab comparison
- Badge English
- Badge Japanese

Icons are hand-authored display primitives; there is no icon font runtime dependency and no external icon license to track.

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

For English PaperCoach content, `Sans Bold-like` is the recommended default style. `Sans Bold-like` and `High Contrast` both use the same `FreeSansBold` font bucket; the visible difference is density, spacing, and metadata contrast, not a different font engine. Japanese live text rendering remains deferred to a future N3 architecture pass.

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

PaperCoach modes use normal handheld orientation. Screens are page-based with large touch targets. Practice uses content taps for within-card pages and footer arrows for previous/next card. Drills and Exam try to keep question plus choices together; split choice pages repeat a compact question reminder. Drill feedback appears after option selection; Exam suppresses immediate feedback until the end summary.

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

v0.9 uses a pre-rendered Japanese badge image generated on the Mac and embedded in firmware. The PaperS3 does not render Japanese text dynamically in Badge mode. Badge behavior is unchanged in v5.9-dev1.

v5.9-dev1 adds a separate, self-contained Japanese mode reachable from Home, with its own dynamic Japanese text rendering path (Daily Questions, Mock Test placeholder, Reference, Results):

- **Daily Questions** uses one embedded N3-style sample set (Week 1, Day 1 — 11 originally written items covering もじ/kanji, ごい/vocabulary, ぶんぽう/grammar; not extracted from any copyrighted book). One question per screen with 4 outlined choices and immediate feedback (correct/wrong, correct choice, Japanese answer sentence, Japanese explanation, English meaning, grammar/vocab/kanji tags).
- **Reference** lists the grammar/vocabulary/kanji tags from the same Week 1 Day 1 set.
- **Results** is a simple RAM-only tally (answered count, correct count/percent, breakdown by kanji/vocabulary/grammar) — a separate struct (`JapaneseSessionResult`/`gJapaneseResults`) from the existing Interview Practice/Drills/Exam `SessionResult`. It never appears inside the existing Results screen and resets on reboot.
- **Mock Test** is a placeholder only; no full test flow yet.
- Dynamic Japanese text uses `lgfxJapanGothic_*` fonts via new, isolated `applyJapaneseTitleFont()`/`applyJapaneseBodyFont()` helpers, and a parallel `sanitizeJapaneseText()`/`wrapJapaneseTextToLines()` path that preserves UTF-8 verbatim and wraps by code point. Japanese text is never routed through the existing `sanitizeCoachText()` (it is ASCII-only and would replace Japanese glyphs with `?`); the existing English sanitize/wrap/font functions are unchanged.
- Full import of the 新にほんご500問 book, SRS, volunteer notes, and a multi-source concept UI are not implemented.

## Download Mode

If upload fails, connect the PaperS3 over USB-C, then long-press the power button until the back status LED flashes red. That indicates download mode. Run the upload command again.

Important: long-pressing the button only enters download mode. It does not erase firmware by itself. Firmware is overwritten only when you flash/upload a new build.

## Notes

- PlatformIO uses `esp32-s3-devkitm-1` because PlatformIO may not have a dedicated PaperS3 board ID. The config still sets PaperS3-compatible 16 MB flash and `qio_opi` memory settings for ESP32-S3R8 / 8 MB OPI PSRAM.
- M5Unified is configured to fall back to `board_M5PaperS3` so the display, SD pins, and buzzer map to PaperS3.
- Official PaperS3 docs: https://docs.m5stack.com/en/core/paperS3
