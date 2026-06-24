# Hardware & Japanese Final Polish Plan

Branch: `hardware-japanese-final-polish`

## What is already implemented

- `src/hw` manager layer: BatteryManager, DisplayManager, PowerManager, SleepManager,
  WakeManager, RTCManager, TouchManager, WifiManager, SDManager, Diagnostics
- Power Lab / Power Audit with wake/reset, battery mV, charging, Wi-Fi, SD index,
  heap/PSRAM, display refresh counters, firmware size
- Reader: TxtReader, BookLibrary, ReaderState, PageCache, EPUB stub, ReaderApp
  - SD paths: `/paperBadge/books`, `/books`, root
  - State: `/paperBadge/reader_state.json`, `/paperBadge/library_index.json`
  - Home has a Reader button between Japanese and Settings
- Wi-Fi off at boot; Bluetooth stopped; radios not re-enabled by any app
- Light-sleep timer + GPIO48 wake; deep-sleep touch wake explicitly blocked
- WarmIdle CPU downscale before display refresh and interactive work
- Japanese helpers: `applyGothicFont`, `containsJapaneseCodepoint`,
  `drawMixedJapaneseLabel`, `drawJapaneseWrappedText`, `drawJapaneseButton`,
  `drawJapaneseOptionButton`, `japaneseBodyPxForReader`,
  `japaneseMetaPxForReader`, `japaneseChoicePxForReader`,
  `japanesePromptPxForReader`, `japaneseExplanationPxForReader`
- Font size helpers route through `canonicalFontSizeMode` (Medium/Large/XL)
- Font Lab JP: 3 JP pages (ladder 20-40, mixed question sample, meta sizes)
- Japanese menu: Daily Questions, Mock Test, Reference, Results, Font Lab (JP), Home
- Japanese results persistence: `/papercoach/japanese/progress/results.log`
- Japanese results isolated from Interview results
- Japanese Mock Test guard: does not sleep while awaiting answer
- `lgfxJapanGothic_40` bucket enabled in `applyGothicFont`
- Home: Badge / Interview / Japanese / Reader / Settings (current order)

## What is wrong or incomplete

- Japanese body text sizes too small: XL=32px, Large=28px, Medium=24px
  (spec requires XL=40px, Large=36px, Medium=32px)
- Japanese choice sizes too small: XL=32px, Large=32px, Medium=28px
  (spec requires XL=36px, Large=36px, Medium=32px)
- Japanese prompt sizes too small: XL=36px, Large=32px, Medium=28px
  (spec requires XL=40px, Large=36px, Medium=32px)
- `japaneseTagPxForReader()` function does not exist (spec requires it for tags >=24px)
- Home label "Japanese" should be "日本語" in Japanese Gothic font
- Japanese menu label "Daily Questions" should be "Practice"
- Home Interview button uses IconType::Practice; spec wants speaking-head/mic icon
- Font Lab JP page 3 is "meta sizes" not "explanation stress test" per spec
- Font Lab JP has no page 4 for font candidate comparison
- Font Lab JP page 1 (ladder) includes 20px which spec excludes; starts at 24px
- Font Lab JP overlap: `renderFontLabJapaneseMeta()` hardcodes y=430 in landscape
  mode (height=540), overlapping the Home button at y=458
- `renderFontLabJapaneseMixed()` annotation text at y≈460 overlaps Home at y=458
- `kFontLabPageCount = 4` (needs to be 5 for 4 JP pages + 1 English page)
- Font Lab button label "Font Lab (JP)" should be "Font Lab JP" (per spec)
- `containsJapaneseCodepoint` uses `ch >= 0xE3` byte check (over-detects, but
  works correctly for all Japanese content; leaving as-is)

## What this pass will change

1. **Japanese font size helpers** — bump body/choice/prompt/explanation sizes
   to match the spec targets (see table below). Preserve meta (unchanged).
   Add `japaneseTagPxForReader()`.
2. **Home label** — "Japanese" → "日本語" (drawButton already handles Gothic)
3. **Japanese menu label** — "Daily Questions" → "Practice"
4. **Interview icon** — add `IconType::SpeakingHead` (mic primitive) to enum
   and `drawIcon` switch; use it on Home for the Interview button
5. **Font Lab JP overlap fix** — remove 20px from ladder; add explicit footer guard
   `contentBottom = display.height() - 130` to all 3 JP pages; fix hardcoded y=430
6. **Font Lab JP page 3** — replace meta-sizes with explanation stress test per spec
7. **Font Lab JP page 4** — add font candidate comparison (built-in only; documents
   efontJA_24_b and external font status)
8. **kFontLabPageCount** → 5
9. **Font Lab button label** — "Font Lab (JP)" → "Font Lab JP"

### Font size table

| Helper | Medium (S) | Large (M) | XL (L) | Note |
|--------|-----------|-----------|--------|------|
| `japaneseBodyPxForReader` | 24 → **32** | 28 → **36** | 32 → **40** | |
| `japaneseChoicePxForReader` | 28 → **32** | 32 → **36** | 32 → **36** | conservative for fit |
| `japanesePromptPxForReader` | 28 → **32** | 32 → **36** | 36 → **40** | matches body |
| `japaneseExplanationPxForReader` | 24 → **28** | 28 → **32** | 32 → **36** | |
| `japaneseMetaPxForReader` | 24 | 24 | 28 | **unchanged** |
| `japaneseTagPxForReader` | 24 | 24 | 24 | **new** — minimum 24 always |

## What this pass will intentionally not touch

- `containsJapaneseCodepoint` byte-check approach (over-detects safely; not broken)
- External font conversion or loading
- Deep-sleep touch wake (remains blocked)
- Dynamic frequency scaling (remains compile-time gated)
- Japanese SD content loading or SQLite
- Interview render/touch paths or `applyTypographyFont`/`applySansBoldFont`
- Japanese Mock Test (remains placeholder)
- EPUB (remains clear unsupported stub)
- Japanese question content
- Badge screen
- Reader pagination or SD path changes
- PowerLab/Diagnostics (already complete; no new rows needed)
