# Hardware refactor summary

Branch: `hardware-layer-reader-refactor`

## What changed

- Added `src/hw` managers for battery, power mode tracking, sleep/wake, RTC
  state, display refresh/palette, touch wake capability, Wi-Fi shutdown, SD book
  indexing, and heap/sketch diagnostics.
- Routed existing boot, SD mount, battery polling, Wi-Fi-off policy, display
  refresh selection, and light-nap entry through the new managers.
- Extended Power Lab and Power Audit with manager-backed diagnostics:
  wake/reset classification, RTC boot/app/reader state, battery source,
  Wi-Fi mode, SD index status, display refresh state, heap/PSRAM, firmware size,
  and last `SleepManager` result.
- Added a top-level Reader button between Japanese and Settings.
- Added `src/apps/ReaderApp` plus `src/reader` modules for:
  - SD book library from `/paperBadge/books`, `/books`, and root.
  - `/paperBadge/library_index.json` cache.
  - TXT and Markdown-as-plain-text reading.
  - Page navigation by left/right tap zones.
  - Reader state in `/paperBadge/reader_state.json`.
  - RTC-backed last reader path/offset.
  - EPUB architecture stub with a clear unsupported message.

## Concepts ported

- From M5Unified: PaperS3 battery and wake constraints, especially ADC-backed
  battery voltage and GPIO48 touch INT light-sleep wake behavior.
- From M5PaperS3-UserDemo: conservative 16-level grayscale palette use and
  quality refresh for full-clean transitions.
- From ESP-IDF examples/docs: timer/GPIO light sleep wake registration,
  wake/reset reporting, tiny RTC state, and compile-time-gated DFS.
- From CrossPoint Reader / PaperS3 fork: SD library scanning, recent-reader
  state, cached page/index mindset, and TXT page-offset persistence.
- From LilyGo repos: conceptual e-paper power-down and grayscale strategies only.

## Intentionally not copied

- LilyGo pin mappings, PMIC assumptions, and panel power sequences.
- CrossPoint's full EPUB renderer, font cache, KOReader sync, networking,
  WebDAV, OPDS, and bitmap/manga paths.
- DRM bypass, PDF rendering, external fonts, and new Japanese question content.

## Current caveats

- Reader TXT pagination is intentionally simple and capped at 220 KB per file for
  this MVP. Larger files open truncated instead of streaming.
- EPUB files may appear in the SD index, but opening them shows the EPUB stub.
- PaperS3 deep sleep touch wake remains blocked because GPIO48 is not RTC-capable.
- Light sleep now attempts timer + GT911 GPIO wake, but this still needs physical
  testing for wake-loop and missed-tap behavior.
- Battery percentage is approximate on PaperS3 when derived from ADC voltage.

## Hardware QA

- Boot on USB and confirm Power Lab page 4 reports wake/reset and touch caveat.
- Put `book.txt` and `notes.md` under `/paperBadge/books`; open Reader and verify
  the list, page turns, Font cycle, Library, and Home controls.
- Reboot and reopen the same file; verify it resumes near the saved page.
- Leave Reader idle in Balanced or Max Battery and watch serial for light-nap
  entry/wake. Confirm it does not record bogus taps after wake.
- Test on battery only, USB connected, and charging; compare voltage, percent,
  charge state, and VBUS/USB rows in Power Lab.
