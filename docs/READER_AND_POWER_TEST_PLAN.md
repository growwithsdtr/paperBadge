# Reader and power test plan

## SD layout

Place books here:

```text
PAPERSD/
  paperBadge/
    books/
      book.txt
      notes.md
```

The Reader also scans `/books` and the SD root for `.txt`, `.md`, and `.epub`.
TXT and MD open now. EPUB is a placeholder interface for later text extraction.

Generated firmware files:

- `/paperBadge/library_index.json`
- `/paperBadge/reader_state.json`

## Reader smoke test

1. Boot firmware with SD inserted.
2. Open Home -> Reader.
3. Confirm TXT/MD files appear in the list.
4. Open a TXT file.
5. Tap right third of the screen for next page.
6. Tap left third for previous page.
7. Tap `Font` and confirm the file reflows.
8. Tap `Library`, reopen the same file, and confirm progress is remembered.
9. Tap `Home` and confirm Badge, Interview, Japanese, and Settings still open.

## Power diagnostics test

1. Open Settings -> Advanced -> Power Lab.
2. Cycle pages and confirm rows for wake/reset, runtime manager, display mode,
   SD index/books, heap/PSRAM, and last sleep result.
3. In Settings, choose Balanced or Max Battery power profile.
4. Leave Home or Reader idle long enough for WarmIdle/LightNap eligibility.
5. Watch serial logs for `SleepManager` entry/wake rows.
6. Confirm no answer-selection screen sleeps while awaiting a tap.

## Battery test matrix

- USB connected, battery installed.
- Battery only.
- Charging from USB.

For each state, record:

- Battery mV.
- Percent and whether it is approximate.
- Charge state.
- VBUS/USB state.
- Whether the readings are stable across two Power Lab refreshes.

## Known caveats

- PaperS3 GT911 INT is GPIO48. It can be used for light-sleep GPIO wake, but it
  is not an RTC GPIO, so deep sleep touch wake remains blocked.
- Dynamic frequency scaling is compiled off unless `ENABLE_EXPERIMENTAL_DFS` is
  explicitly enabled.
- No DRM bypass, PDF rendering, or manga/CBZ support is implemented.
