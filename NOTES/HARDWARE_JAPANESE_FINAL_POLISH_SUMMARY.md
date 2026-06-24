# Hardware & Japanese Final Polish Summary

Branch: `hardware-japanese-final-polish`
Build result: **SUCCESS**
Firmware: 3.80 MB / 6.55 MB Flash (58.0%) — well under 12 MB guardrail
RAM: 231364 / 327680 bytes (70.6%)

## Commits

- `a39ebea` — review: add final hardware japanese polish plan + all code changes

(Plan note, Japanese font size updates, Font Lab JP layout fix,
Home label/icon polish, Japanese menu label polish committed together.)

## Hardware review findings

### Battery
- `BatteryManager` polls at 45s active, 120s Balanced idle, 180s Max Battery idle.
- mV, percent (approximate ADC-derived), charging state, VBUS all reported in Power Lab.
- Battery polling is cached; not in tight loops.

### Wi-Fi / Bluetooth
- Wi-Fi off at boot via `WifiManager::disableAll()`.
- Bluetooth stopped via `btStop()` at boot.
- No app (Reader, Japanese, Interview) re-enables radios.
- Wi-Fi state shown in Power Lab diagnostics.

### Sleep / wake
- GPIO48 = GT911 touch INT. Used for light-sleep GPIO wake.
- GPIO48 is not RTC-capable → deep-sleep touch wake remains blocked.
- Timer wake fallback registered with light sleep.
- Wake cause and reset reason logged in Power Lab page 4.
- Answer-selection screens protected by `isAnswerSelectionActive()`.
- Japanese Mock Test answer state guarded by `gJapaneseMockTestAwaitingAnswer`.

### CPU / responsiveness
- WarmIdle CPU downscale (240→80 MHz) after 2s idle on static screens.
- `prepareFullRefresh()` restores active CPU before any display refresh.
- Touch debounce: 250ms fast, 600ms clean. Input locked during refresh.
- No repeated redraw loop found in `loop()`.

### Display / e-paper
- `DisplayManager` tracks refresh mode (Fast/Balanced/Clean), counters,
  and reason string.
- Clean refresh on major screen transitions; fast/balanced for page turns.
- Hard clean limit: 16 non-clean transitions; Balanced: 10.

### Reader
- Home has Reader button between 日本語 and Settings.
- TxtReader opens `.txt` and `.md` from `/paperBadge/books`, `/books`, SD root.
- Library index: `/paperBadge/library_index.json`
- Reader state: `/paperBadge/reader_state.json`
- EPUB shows a clear unsupported stub message.
- No constant SD rescanning; index caches book list.

### Diagnostics (Power Lab)
- Wake cause, reset reason, battery mV/percent/source/charging/VBUS
- Wi-Fi mode, CPU MHz, heap/PSRAM, sketch size
- SD mounted/index/book count
- Display refresh mode/counters
- Last SleepManager result
- Touch wake caveat note

## Hardware changes made

None — hardware layer was already complete from the `hardware-layer-reader-refactor`
branch. This pass verified and documented the existing implementation.

## Japanese rendering findings

### Font size helpers (before → after)

| Helper | Medium (S) | Large (M) | XL (L) |
|--------|-----------|-----------|--------|
| `japaneseBodyPxForReader` | 24 → **32** | 28 → **36** | 32 → **40** |
| `japaneseChoicePxForReader` | 28 → **32** | 32 → **36** | 32 → **36** |
| `japanesePromptPxForReader` | 28 → **32** | 32 → **36** | 36 → **40** |
| `japaneseExplanationPxForReader` | 24 → **28** | 28 → **32** | 32 → **36** |
| `japaneseMetaPxForReader` | 24 | 24 | 28 | (unchanged) |
| `japaneseTagPxForReader` | — | — | — | **new** (24px always) |

### Font Lab JP (before → after)

- Page 1: Ladder was {20,24,28,32,36,40} → **{24,28,32,36,40}** (20px removed per spec)
- Page 1: Footer guard was `y > height-120` → **`y + px + 24 + 10 > height-130`**
  (earlier guard prevents overlap with Home button in landscape mode)
- Page 2 (Mixed question): Annotation text now only drawn if `y+28 <= contentBottom`
  (was at hardcoded y=460, overlapping Home button at y=458 in landscape)
- Page 3: Was "Meta sizes" → **"Explanation stress test"** per spec
  (renders 「郵便局」は... explanation at current reader size with diagnostic output)
- Page 4: **New** — "Font candidate comparison"
  (shows lgfxJapanGothic_28/36/40 samples + efontJA status + external font note)
- `kFontLabPageCount`: 4 → **5** (1 English + 4 JP pages)
- Button label: "Font Lab (JP)" → **"Font Lab JP"**

### Home menu

- "Japanese" button label → **"日本語"** (Japanese Gothic via containsJapaneseCodepoint)
- Interview icon: `IconType::Practice` → **`IconType::SpeakingHead`** (microphone primitive)

### Japanese menu

- "Daily Questions" button → **"Practice"**
- Source Select screen title "Daily Questions" → **"Practice"**
- Week Select screen title "Daily Questions" → **"Practice"**
- Day Select screen title "Daily Questions" → **"Practice"**
- "Font Lab (JP)" button → **"Font Lab JP"**

### Grammar tag

- Previously hardcoded `applyGothicFont(24)` → now uses **`japaneseTagPxForReader()`**
  (returns 24 always, but routed through the central helper)

## External JP font status

- **Not embedded.** Built-in `lgfxJapanGothic_*` is the sole Japanese font.
- See `NOTES/JP_FONT_ASSET_NOTES.md` for full candidate list, licenses,
  efontJA_24_b status, and recommended future embedding route.

## What was intentionally not implemented

- `containsJapaneseCodepoint` rewrite (current `ch >= 0xE3` check works correctly
  for all Japanese content; false positives are benign on this device)
- External font embedding, conversion tooling, or SD font loading
- Deep-sleep touch wake (GPIO48 not RTC-capable; remains blocked)
- Dynamic frequency scaling (remains compile-time gated behind `ENABLE_EXPERIMENTAL_DFS`)
- Japanese SD content loading, SQLite, or full source registry
- Full Mock Test logic (remains placeholder)
- EPUB rendering (remains clear unsupported stub)
- New Japanese question content
- Interview render/touch paths (untouched)
- PowerLab / Diagnostics new rows (already complete)

## Physical QA checklist

### Home
- [ ] Badge / Interview / 日本語 / Reader / Settings — correct order and labels
- [ ] Interview shows microphone icon (SpeakingHead)
- [ ] Settings shows gear icon
- [ ] 日本語 renders in Japanese Gothic font (not boxes)
- [ ] Tapping 日本語 goes to Japanese menu

### Japanese menu
- [ ] Practice / Mock Test / Reference / Results / Font Lab JP / Home
- [ ] "Daily Questions" label does NOT appear anywhere
- [ ] Tapping Practice goes to source select

### Font Lab JP page 1 (ladder)
- [ ] Page indicator shows "Page 2/5" (since page 0 = English)
- [ ] Sizes shown: 24 / 28 / 32 / 36 / 40 — NO 20px
- [ ] In portrait mode: all 5 sizes visible
- [ ] In landscape mode: as many as fit without overlapping Home button
- [ ] Japanese sample text "あいうえお アイウエオ 郵便局 ぶんぽう" renders in each size

### Font Lab JP page 2 (question sample)
- [ ] Header "N3 · W1D1 · 500問 · もじ" renders
- [ ] Prompt "「郵便局」の読み方として..." renders (up to 3 lines)
- [ ] Choice button "A. ゆうびんきょく" renders
- [ ] No text behind Home button

### Font Lab JP page 3 (explanation stress test)
- [ ] Shows full explanation sentence at current reader size
- [ ] Diagnostic line shows explPx / lineH / lines / overflow
- [ ] No footer overlap

### Font Lab JP page 4 (font comparison)
- [ ] Shows lgfxJapanGothic_28, _36, _40 samples
- [ ] efontJA status note visible
- [ ] External font note visible
- [ ] No footer overlap

### Japanese Practice (Reader M default)
- [ ] Body/prompt text uses 36px Gothic (was 28-32px)
- [ ] Choice buttons use 36px Gothic (was 32px)
- [ ] Header/meta uses 24px Gothic (unchanged)
- [ ] Grammar tag uses 24px Gothic (unchanged)
- [ ] 4 option buttons visible without overlapping footer

### Japanese Feedback
- [ ] Explanation text uses 32px Gothic (was 28px at Reader M)
- [ ] Grammar tag >=24px
- [ ] No ??? or blank glyphs

### Power Lab
- [ ] Wake/reset/battery/Wi-Fi/SD/heap/display rows visible

### Reader
- [ ] TXT/MD opens from SD
- [ ] Page turns work
- [ ] Home returns to Home screen

### Interview regression
- [ ] Practice/Drills/Exam questions render correctly
- [ ] Interview Results do NOT show Japanese results
