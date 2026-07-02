# QA Font/Manga/Interview Fixes

Branch: `qa-font-manga-interview-fixes`
Base: `e5ea710` (`font-manga-reader-sprint`)

## Commits

- `4a5a56b` — fix font lab readability
- `f7ac6f7` — clarify font runtime model
- `c16d3b9` — fix manga slice nav and sort
- `0dbdf2d` — add manga reading overlay
- `7ea6aa9` — fix manga touch zones
- `1941711` — stabilize manga landscape navigation
- `4f55c13` — add png manga slice view
- `19d7628` — probe imu manga autorotate
- `f7c299b` — fix interview practice pagination
- `efc0380` — wire interview size setting
- `4c5d783` — improve epub cap messages
- `e7fa611` — improve manga error diagnostics

## Summary

- Font Lab now uses spacious summary, production-font, index, and one-candidate pages. Full-cell sample boxes prevent the 24 px and 48 px previews from clipping or overlapping, and Latin-only candidates no longer render fake Japanese boxes.
- Runtime font docs and UI copy now make the model explicit: production rendering is BIZ UDGothic or IPAex Gothic only; Font Lab assets are fixed bitmap QA preview subsets, not runtime variable/outline fonts.
- Manga landscape/backward navigation now lands on the previous page's last slice, CBZ files use natural numeric sort, and hidden dotfiles / `__MACOSX` entries are ignored.
- Manga reading has subtle chrome, first-use hints, page/slice/fit/orientation status, and a center-tap overlay for Fit, Orientation, Refresh, Clean now, Library, and Close.
- Manga touch zones no longer use the old fixed bottom controls: top strip returns to library, center opens the overlay, portrait left/right thirds page, and landscape left/right regions step slices/pages.
- Manga decode failures now show page/slice/fit/orientation diagnostics instead of silently bouncing, and archive-open failures distinguish ZIP64, CBR/RAR, WebP-only/no JPEG/PNG, corrupt/unreadable ZIP, and allocation failures.
- PNG-in-CBZ now uses the same fit/slice view path as JPEG pages, subject to PNGdec row-buffer limits.
- IMU work is probe-only in this branch: firmware logs I2C reads at BMI270 candidate addresses `0x68` and `0x69`; manga auto-rotation remains manual until hardware response is confirmed on-device.
- Interview Practice answers now paginate across all answer sections using available screen height, and Interview Size now affects practice/drill/glossary layout spacing.
- EPUB text extraction now uses UTF-8-safe truncation and distinguishes oversized spine-entry skips from total extracted-text cap truncation.

Build checkpoints: all stage `pio run` checks passed. Latest checked firmware flash 1,550,241 bytes / 11,534,336 bytes, RAM 27,632 bytes / 327,680 bytes.
Flash: NOT RUN by explicit request. Manual command: `pio run -t upload --upload-port /dev/cu.usbmodem1101`.

# Font Manga Reader Sprint

Branch: `font-manga-reader-sprint`
Base: `18bf704` (`heavy-paperbadge-sprint`)

## Commits

- `ed4e23f` — embed font lab candidates
- `6dba52c` — finish font size settings
- `ca400e2` — implement manga orientation rotation
- `4b16d28` — implement manga fit slice rendering
- `f37a864` — improve manga archive support
- `f7db450` — support larger epub reader
- `9aae6e2` — wire manga refresh controls
- `880a98c` — upgrade manga preprocessor

## Summary

- Font Lab now embeds generated sparse bitmap subsets for BIZ UDPGothic, Noto Sans JP, M PLUS 1p, M PLUS Rounded 1c, Inter, and Source Serif 4, with a host download/subset/embed pipeline.
- Font and size settings persist through `settings.kv`; Reader and Japanese sizing now alter actual layout density.
- Manga reading can enter true epdiy landscape rotation with remapped touch and restores app portrait orientation when leaving Manga.
- JPEG manga rendering supports fit-page, fit-width, fit-height, and vertical slice paging through the decoder viewport path.
- CBZ size guard now checks ZIP64 markers instead of rejecting files over 50 MB; non-ZIP64 large CBZs can attempt open.
- PNG-in-CBZ is supported for fit-page rendering; WebP, CBR/RAR, and ZIP64 remain unsupported in firmware.
- EPUB container limit raised for normal unencrypted XHTML EPUBs around 10 MB with safe chapter/text caps and partial extraction notice.
- Manga refresh profile now applies as Fast/Balanced/Clean, with visible clean cadence in Settings.
- Manga preprocessor defaults to 150 MB non-ZIP64 chunks and outputs landscape 960x540 slice assets plus richer OCR metadata.

Build checkpoints: all stage `pio run` checks passed. Latest checked firmware flash 1,540,761 bytes / 11,534,336 bytes, RAM 27,496 bytes / 327,680 bytes.
Flash: NOT RUN by explicit request. Manual command: `pio run -t upload --upload-port /dev/cu.usbmodem1101`.

# Heavy PaperBadge Sprint

Branch: `heavy-paperbadge-sprint`
Base: `d76e86c` (`codex/aggressive-papers3-idf-overhaul`)

## Commits

- `7fa5773` — fix transactional reader epub
- `4383712` — fix badge interactions final frame
- `b543c56` — add header navigation stack
- `c97ca45` — isolate screen orientation
- `beee96a` — restore rich interview flows
- `30ec7a1` — expand japanese study scaffold
- `a203f9a` — improve manga reader layout formats
- `3e06af4` — improve reader typography settings
- `0cb18fa` — restore refresh profiles
- `0300efe` — add font lab font settings
- `3ae477f` — expand settings pages
- `6eedaec` — add manga preprocessor
- `e4844ff` — add manga ocr sidecar schema
- `a2dd916` — add japanese data schema index scaffold
- `93f72c0` — document heavy sprint

## Summary

- Reader EPUB open is transactional and OPF/spine-based, with bounds-safe HTML entity stripping and explicit ReaderError Back/Home routing.
- Badge is footer-free, supports manual EN/JA toggle, QR zoom, and English-only 180-degree final sleep/power frame without leaking orientation.
- Header navigation fallback is top-left Back, top-right Home, with a small stack and deterministic Reader/Manga error targets.
- Interview/PaperCoach has richer practice menu/list, paged answers from embedded deck fields, distinct no-feedback Exam, and glossary categories.
- Japanese has Source -> Week/Unit -> Day/Lesson -> Practice scaffold, no-feedback Mock Test, Reference, Results, and SD registry detection.
- Manga is fullscreen while reading, keeps CBZ/JPEG stable, has explicit CBR/RAR/PNG/WebP/ZIP64 guidance, and host preprocessing for large archives.
- Font Lab was expanded and a host font candidate/subset helper was added.
- Refresh profiles Fast/Balanced/Clean are persisted and wired into page turns/reveal transitions.
- Settings now has Fonts, Reader, Manga, Power, and Refresh pages.
- Schema/docs added for manga OCR sidecars and Japanese cross-reference/progress data.

Build: SUCCESS — latest checked firmware flash 1,402,561 bytes / 11,534,336 bytes, RAM 24,344 bytes / 327,680 bytes.
Flash: NOT RUN for this sprint by explicit request. Use `pio run -t upload --upload-port /dev/cu.usbmodem1101` when ready.

# Aggressive Paper S3 ESP-IDF / Manga Shell

Branch: `codex/aggressive-papers3-idf-overhaul`

## Commits

- `339d811` — fix embedded badge rendering (P0 QA stabilization pass)

  **Badge root cause and fix**
  - Root cause: `PNG_MAX_BUFFERED_PIXELS` in PNGdec.h was `(320*4+1)*2 = 2562`. The guard
    in `png.inl` rejects any image where `iPitch >= PNG_MAX_BUFFERED_PIXELS/2 = 1281`.
    The 540-wide badge PNG has `iPitch = 540*3 = 1620 > 1281` → `PNG_TOO_BIG` returned.
  - Fix: `components/PNGdec/src/PNGdec.h` limit raised to `4400`. Supports up to 540-wide
    RGBA (iPitch=2160 < 2200=4400/2); decode buffer covers two rows + alignment (needs ~4352B,
    have 4400B). Both the guard check and the internal two-row buffer now fit.
  - Improved badge fallback: on decode failure, shows styled name/title card instead of
    bare "Embedded badge image unavailable." text.
  - Added ESP_LOGI diagnostics at each decode path so future failures are visible in serial.

  **Navigation fixes (random jump mitigation)**
  - `handle_manga_error`: add `ps3::touch::drain()` after `render_manga_library()` to drop
    phantom taps buffered during the slow GC16Full panel refresh.
  - `handle_reader_error`: same drain after `render_reader_library()`.
  - `handle_manga_reading` (header tap → back): drain after GC16Full library render.
  - Prevents queued taps from the previous screen triggering spurious library row opens
    on the newly rendered screen.

  **render_current completeness**
  - Added missing `Screen::JapaneseFont` case; wake-from-sleep while on font picker now
    restores the font screen instead of falling through to `default: render_home`.

  Build: SUCCESS — Flash 11.5% (1,329,993 bytes / 11,534,336 bytes), RAM 7.3%
  Flash: PENDING — device in deep sleep at commit time; wake device and run:
    `pio run -t upload --upload-port /dev/cu.usbmodem1101`

- `6d5ca18` — restore badge, interview, manga guard, reader guard (staged recovery pass)

  **Stage 1 — Embedded Badge (no SD required)**
  - `src/embedded_badge.h`: ESP-IDF-compatible flash arrays for badge_en.png,
    badge_ja.png, profile.png, qr.png (~100 KB in .rodata)
  - `render_badge()` decodes embedded PNG via `display_png()`;
    SD asset file (`/paperBadge/assets/badge_en.png`) is optional fallback only
  - `draw_english_badge_final_frame()`: draws badge as final e-ink frame before
    light sleep, deep sleep, and power-off so screen persists as badge
  - No SD card required for Badge

  **Stage 2 — Embedded Interview (no SD required)**
  - `src/embedded_interview_deck.h`: ESP-IDF port of `embedded_papercoach_deck.h`
    (removes `#include <Arduino.h>` / `PROGMEM`); 71 cards, 149 drills, 44 glossary terms
  - Interview now opens a real submenu: Practice, Drills, Exam, Glossary, Results
  - Practice: flip-card view (title → Reveal → spoken answer), tracks session card count
  - Drills: scrolls through all MCQ drills (optionCount > 0), option selection + feedback screen
  - Exam: 10-question shuffled subset (LCG shuffle), score tracked per session
  - Glossary: scrollable term/definition/example browser (44 terms)
  - Results: session drill correct/total + last exam score + Reset
  - No SD card required

  **Stage 3 — Manga archive guard**
  - CBR/RAR tapped: immediate clear error screen with conversion advice
  - File size guard: archives >50 MB shown ZIP64 diagnostic before attempting open
    (miniz cannot parse ZIP64 central directories)
  - Diagnostic includes: file name, size (MB), ZIP64 risk rating, remediation steps
  - open_manga_book() failure: structured error with likely causes + serial log hint
  - Missing SD (library dir unreadable): graceful "No SD" screen
  - New `Screen::MangaError` dismisses on any tap → library
  - Small CBZ with JPEG pages continues to open normally

  **Stage 4 — Reader/EPUB guard**
  - File size guard: files >4 MB blocked before text extraction attempt
  - EPUB entry guard: individual HTML entries >2 MB uncompressed skipped to avoid heap OOM
  - EPUB open failure: structured error (corrupted, encrypted, no HTML, OOM, suggest TXT)
  - Non-EPUB open failure: clear message
  - Missing SD: detects `/sdcard` unavailability, shows message
  - New `Screen::ReaderError` dismisses on any tap → library
  - Good TXT/MD/small EPUB continue to open normally

  **Stage 5 — Japanese / font baseline preserved**
  - BIZ UDGothic remains default; IPAex Gothic remains selectable via Font page
  - Japanese practice (3 embedded questions), feedback, multi-page flow: unchanged
  - No Japanese routes through ASCII sanitizer

  **Stage 6 — Sleep / power preserved**
  - Badge final frame before every sleep/power-off path
  - Auto sleep timeout still functional (configurable in Settings)
  - light sleep → badge frame → block on touch → wake → render_current()
  - deep sleep → badge frame → epd_poweroff → esp_deep_sleep_start()

  Build: SUCCESS — Flash 11.5% (1,329,273 bytes / 11,534,336 bytes), RAM 7.3%
  Flash: SKIPPED — device in deep sleep, physical button press required to wake
  Port detected: `/dev/cu.usbmodemHA0E246012845` (present, not responding to auto-reset)

- `2a3b2af` — overhaul: migrate PaperS3 firmware to ESP-IDF manga shell
  - Switched PlatformIO from Arduino/M5Unified to ESP-IDF 5.5 via `espressif32@6.12.0`
  - Added 16 MB Paper S3 partition layout with 0xB00000 app slot (<12 MB cap)
  - Vendored build-relevant epdiy, JPEGDEC, PNGdec, and miniz components
  - Added epdiy Paper S3 display HAL, direct GT911 touch, SDSPI SD mount, ADC battery/charge logging, NVS deep-sleep metadata
  - Added ESP-IDF app shell with Home entries: Badge, Interview, 日本語, Manga, Reader, Settings
  - Manga opens CBZ/ZIP JPEG archives from `/paperBadge/content/manga` with upstream page loader/progress backend
  - Reader opens TXT/MD plus minimal EPUB XHTML/HTML extraction from `/paperBadge/content/books`
  - Japanese practice runs embedded sample questions and fixes single-page feedback: `Next` advances instead of looping on explanation
  - Embedded BIZ UDGothic Regular and IPAex Gothic sparse firmware fonts with bundled OFL/IPA licenses; JP font lab renders both samples
  - Power UX includes manual light sleep, deep sleep power-off, touch wake drain, battery log path `/paperBadge/logs/battery.log`

Build: SUCCESS — Flash 10.1% (1,162,457 bytes / 11,534,336 bytes), RAM 7.2%
Flash: SUCCESS — 1,162,976 bytes written to `/dev/cu.usbmodem1101`, verified
Serial smoke: monitor connected, but no boot transcript was captured after upload/reset

Known first-shell gaps:
- Interview content is preserved in the repo but not fully ported into the ESP-IDF renderer yet; the current Interview entry is a read-only placeholder.
- Manga v1 path is JPEG-in-CBZ/ZIP first; PNG-in-CBZ and richer bookshelf thumbnails can follow once hardware smoke is confirmed.

# Study UI Power Reader Overhaul

Branch: `study-ui-power-reader-overhaul`

## Commits

- `6fc7ef0` — study-ui-power-reader-overhaul: power policy, JP typography, feedback flow, mixed EN/JP, embedded reader samples

### Power policy (responsive-first)
- Responsive: `WarmIdle@5min` (was 60s), LightSleep disabled
- Balanced: `WarmIdle@2min` (was 15s), `LightSleep@5min` (was 10min)
- Max Battery: `WarmIdle@1min` (was 5s), `LightSleep@4min` (was 5min)
- No deep-sleep; no experimental DFS; CPU restored before touch processing

### Japanese typography unified
- Reader L (XL): body/prompt/choice/explanation all 40px, English EN explanation 32px
- Reader M (Large): all 36px, English 28px
- Reader S (Medium): all 32px, English 24px
- Added `japaneseStudyBodyPx/ChoicePx/ExplanationPx/MetaPx/EnglishPx/FooterPx` named aliases

### Feedback pagination rebuilt
- Pre-measures all content blocks before drawing
- Single-page when all content fits: footer shows Next→next question (no wasted page)
- Multi-page: page 0 shows result + answer + answer sentence + partial JP explanation; page 1 shows EN + grammar tag
- No more "Tap More for explanation." waste page

### Mixed EN/JP renderer
- `splitIntoScriptRuns()` splits UTF-8 text into same-script runs
- `drawMixedScriptLine()` renders a line with per-run font switching
- `drawMixedScriptWrappedText()` wraps and renders mixed paragraphs
- English runs → FreeSansBold; Japanese runs → lgfxJapanGothic
- No tofu boxes; no Gothic for English prose

### Reader embedded samples
- `TxtReader::openFromString()` opens content from RAM (no SD required)
- English sample: Metro Study Tips (~400 words, multiple pages at all sizes)
- Japanese sample: N3 vocabulary short paragraphs with hiragana/kanji
- Library screen shows embedded samples with "Embedded sample" label when SD has no books
- Tap any row to open sample with full Reader pagination and font size support

### Font Lab + tooling
- Font Lab page 4: actionable status message pointing to `tools/prepare_jp_fonts.sh`
- `tools/prepare_jp_fonts.sh`: downloads BIZ UDPGothic Bold, calls subset script
- `tools/subset_jp_font.py`: pyftsubset wrapper for N3 codepoints (OFL 1.1 license)

Build: SUCCESS — Flash 69.5% (4.55 MB / 6.55 MB), RAM 71.1%
Flash: SUCCESS — 4555088 bytes written, verified

## Physical QA checklist

### Power policy
- [ ] Responsive mode: use device for 2 minutes — all taps first-try, no CPU downscale
- [ ] Responsive mode: idle 5 min on Home screen, tap once — wakes and acts immediately (or reliably)
- [ ] Balanced mode: idle 2 min on Home — serial shows WarmIdle entered after ~2min
- [ ] Max Battery mode: idle 1 min on Badge — serial shows WarmIdle after ~1min

### Japanese typography
- [ ] Reader L: question prompt and 4 answer choices all at 40px consistent size
- [ ] Reader M: question prompt and choices all at 36px
- [ ] Feedback (Reader L): first feedback screen shows result + answer + answer sentence + JP explanation
- [ ] Feedback (Reader L): no screen whose only content is "Tap More"
- [ ] Q003 EN explanation: no square boxes for 違っていました
- [ ] Q004 EN explanation: no square boxes for 引っ越した
- [ ] English words in EN explanation use FreeSansBold; Japanese examples use Gothic font

### Reader embedded samples
- [ ] Reader without SD: Library shows "Embedded sample documents" header
- [ ] Two rows visible: "Sample EN — Metro Study" and "Sample JP — N3 Vocab"
- [ ] Open English sample: text in FreeSansBold, multiple pages at Reader L
- [ ] Open Japanese sample: kana/kanji readable, wrapping correct, no missing glyphs
- [ ] Page turns work (tap left/right thirds and footer buttons)
- [ ] "Library" and "Home" footer buttons work from embedded sample
- [ ] Font size button cycles S/M/L correctly on embedded sample

### Font Lab
- [ ] Font Lab page 4 shows: "External JP font: not embedded" + "Run: tools/prepare_jp_fonts.sh"
- [ ] lgfxJapanGothic samples at 28/36/40px visible with 郵便局 sample
- [ ] efontJA_24_b sample visible

### Interview regression check
- [ ] Interview Practice: question renders, options selectable
- [ ] Drills: renders and taps work
- [ ] Exam: renders, question answer selectable

---

# Touch / Reader / Feedback Bugfix Pass

Branch: `touch-reader-feedback-bugfix-pass`

## Commits

- `1346954` — touch-reader-feedback-bugfix: fix debounce, CPU guard, reader fonts, feedback pagination
  - `kInputDebounceMs` 250 → 50 ms, `kInputCleanRefreshDebounceMs` 600 → 150 ms
    (eliminates 1-2s post-navigation dead zone — primary touch miss cause)
  - `maybeScaleIdleCpu()`: Responsive profile (`PowerProfile::Balanced`) returns early
    — CPU never scales to 80 MHz on interactive screens in Responsive mode
  - `renderReader()`: clean refresh on entry (not page-turns) clears ghosting that
    made Reader text appear pale/washed out
  - `ReaderApp.cpp`: full rewrite — all `setTextFont(2)` GLCD 6px bitmap font replaced
    with `FreeSansBold9/12/24pt7b` across Library, Reading, Message, and footer buttons
  - `gJapaneseFeedbackPage` (0/1): feedback split into two pages
    — Page 0: Correct/Wrong + correct answer + "Tap More" hint
    — Page 1: answer sentence + JP explanation + EN explanation + grammar tag
  - EN explanation always rendered with `applyJapaneseEnglishLabelFont` (FreeSansBold),
    not Gothic — even when explanation contains embedded Japanese examples
  - `NOTES/TOUCH_READER_FEEDBACK_BUGFIX_PLAN.md`: root cause analysis doc

Build: SUCCESS — Flash 69.4% (4.55 MB / 6.55 MB), RAM 70.6%
Flash: SUCCESS — 4549248 bytes written, verified

## Physical QA checklist

### Touch responsiveness
- [ ] Home → navigate to Japanese: first tap after screen settles registers immediately
- [ ] After 60s+ idle on Home (Responsive mode): tap registers, serial shows CPU NOT scaled
- [ ] Home → Reader → page turn → Back: all taps register within ~200ms of screen settling

### Reader
- [ ] Library screen: "Reader" title large, book titles readable (~24px), metadata legible
- [ ] Empty SD / no books: readable message text, not tiny gray GLCD dots
- [ ] Open TXT at Reader S: body text ~24px FreeSansBold, high contrast black
- [ ] Open TXT at Reader M: body text ~34px FreeSansBold
- [ ] Open TXT at Reader L: body text ~44px FreeSansBold
- [ ] Footer buttons (Library / Font S/M/L / Home): readable labels, not tiny
- [ ] Entering Reader from Home: clean refresh (no pale ghosting), text is crisp black

### Japanese feedback pagination
- [ ] Answer any question → feedback page 0: shows Correct/Wrong + answer + "Tap More" hint
- [ ] Tap "More": page 1 shows answer sentence + JP explanation + EN explanation
- [ ] EN explanation on page 1: rendered FreeSansBold (not Gothic), English readable
- [ ] At Reader L: page 1 content does NOT overflow into footer area
- [ ] Tap "Next" from page 1: advances to next question, resets to page 0

---

# Responsiveness / Reader / JP Fonts Pass

Branch: `responsiveness-reader-jp-fonts-pass`

## Commits

- `df0a9a9` — responsiveness-reader-jp-fonts: fix tap miss, Reader font, JP explanation, kinsoku, efontJA
  - `loopDelayMs()`: Responsive idle 300ms → 50ms (eliminates missed taps after 30s idle)
  - `profileIdleScaleThresholdMs()`: Responsive WarmIdle threshold 30s → 60s
  - `ReaderApp::applyBodyFont()`: FreeSansBold12/18/24pt7b replaces setTextFont(2)+scale
  - `lineHeight()` / `charsPerLine()` updated to proportional font metrics (34/44/54px, 11/16/22px widths)
  - `japaneseModeEnglishPxForReader()`: 24px (S) / 31px (M/L) replaces hardcoded 20px
  - Mixed-JP English explanation lines use `japaneseExplanationPxForReader()` when JP detected
  - `isKinsokuForbiddenStart()` + `wrapJapaneseTextToLines()` kinsoku rule for 、。」』）】！？ーっッ
  - Font Lab JP page 4: renders `fonts::efontJA_24_b` sample (no build flag needed)
  - Plan note: `NOTES/RESPONSIVENESS_READER_JP_FONTS_PLAN.md`

Build: SUCCESS — Flash 69.5% (4.55 MB / 6.55 MB), RAM 70.6%
efontJA_24_b font data added ~750 KB — well under 12 MB guardrail.

## Physical QA checklist

### Touch responsiveness
- [ ] After 60s+ idle on Home: first tap registers immediately (was needing 2-3 taps)
- [ ] After 30s idle: first tap still registers (WarmIdle now at 60s, so no scale yet)
- [ ] Japanese menu: taps responsive even after sitting idle

### Reader
- [ ] Open a TXT file at Reader S: text is ~24px FreeSansBold, clearly readable
- [ ] Open at Reader M: text is ~34px FreeSansBold
- [ ] Open at Reader L: text is ~44px FreeSansBold
- [ ] Lines wrap correctly; no text cut off at right edge
- [ ] Font cycle button cycles 1→2→3→1 and page count changes to match
- [ ] Page turn and Library navigation still work

### Japanese feedback English explanation
- [ ] Settings → Reader L → Japanese practice → answer a question
- [ ] English explanation "EN: ..." renders at FreeSansBold18pt7b (not tiny 20px)
- [ ] If English explanation contains Japanese examples, those render at explanation size

### Kinsoku
- [ ] In Japanese practice, long prompts do not end a line with 、or 。at the start of the next line
- [ ] Grammar tag lines do not start with 。or ）

### Font Lab JP page 4
- [ ] efontJA_24_b renders the 郵便局 sample (not boxes or ? glyphs)
- [ ] Three lgfxJapanGothic samples (28/36/40px) still visible above it
- [ ] No footer overlap

---

# Hardware & Japanese Final Polish

Branch: `hardware-japanese-final-polish`
Base: `9d70d95` (`hardware-layer-reader-refactor` tip)

## Commits

- `a39ebea` — review: add final hardware japanese polish plan + all changes
  - Japanese body/choice/prompt/explanation font sizes bumped to spec targets
  - `japaneseTagPxForReader()` helper added
  - Home "Japanese" → "日本語" (Gothic-rendered)
  - Home Interview icon: Practice → SpeakingHead (microphone primitive)
  - Japanese menu "Daily Questions" → "Practice"
  - Japanese menu "Font Lab (JP)" → "Font Lab JP"
  - Source/Week/Day Select screen titles: "Daily Questions" → "Practice"
  - Font Lab JP page 1 ladder: removed 20px, start at 24px; footer guard tightened
  - Font Lab JP page 2 (Mixed): annotation guarded against footer overlap
  - Font Lab JP page 3: Meta sizes replaced with Explanation stress test
  - Font Lab JP page 4: New font candidate comparison page
  - `kFontLabPageCount`: 4 → 5
  - Grammar tag uses `japaneseTagPxForReader()` instead of hardcoded 24
  - Docs: HARDWARE_JAPANESE_FINAL_POLISH_PLAN.md

Build: SUCCESS — Flash 58.0% (3.80 MB / 6.55 MB), RAM 70.6%

---

# Overnight implementation log

Base: `f2315aa` (`v5.9-dev3`)

## Commits

- `42af620` - Add reversible power trace instrumentation.
- `23a1024` - Enable Japanese Gothic 40 font bucket.
- `2f9c6f1` - Add Japanese codepoint detector.
- `fa981aa` - Add mixed Japanese text rendering helpers.
- `8dbe06b` - Route Japanese UI labels through safe text path.
- `d9f1e96` - Warn when Japanese reaches ASCII sanitizer.
- `1c01c92` - Guard Japanese mock test answer sleep.
- `5e2c2c9` - Add built-in Japanese Font Lab pages.
- `3af3018` - Persist Japanese results to isolated NDJSON log.

Each commit was followed by `pio run`; all builds passed before moving on.

## Block A power audit findings

- Max Battery WarmIdle threshold is `5000ms` via `profileIdleScaleThresholdMs()`.
- WarmIdle entry path is `loop()` -> `maybeEnterPowerIdle()` -> `enterIdleMode()` -> `maybeScaleIdleCpu()`.
- WarmIdle is gated by: `gInputLocked == false`, `gTouchActive == false`, `gLastUserActivityMs != 0`, post-touch guard >= `2000ms`, `isStaticIdleScreen(gScreen)`, `kEnableIdleCpuScaling`, and not already idle-scaled.
- CPU scales from active `240MHz` to requested idle `80MHz` through `setCpuFrequencyMhz(kIdleCpuMhz)`.
- `recordUserActivity()` callers are `touch press`, `touch release`, and `post-wake touch held`. Spurious touch-controller press/release/click noise can reset the idle timer if it reaches `M5.Touch.getDetail()` as those events; locked input is ignored before this reset.
- WarmIdle eligible: Home, Interview menu, Japanese menu/source/week/day, Japanese Reference, Japanese Results, Settings, Advanced, Power Lab, Font Lab, Practice menu, Glossary menu, Drills menu, Interview Practice, Glossary, Results.
- WarmIdle ineligible: active Drills question screen, active Exam question screen, Japanese Daily question screen, Badge.
- LightNap eligible: Badge, Home, Interview menu, Japanese menu/source/week/day, Japanese Daily, Japanese Reference, Japanese Results, Practice/Glossary/Drills menus, Interview Practice, Glossary, Results, Drills, Exam.
- LightNap ineligible by screen: Settings, Advanced, Debug, Power Lab, Font Lab, Visual QA, Help, Japanese Mock Test placeholder.
- Pre-answer Drills/Exam/JapaneseDaily are blocked by `isAnswerSelectionActive()`. A future Japanese Mock Test pre-answer state now has `gJapaneseMockTestAwaitingAnswer` as an answer-selection guard, but Mock Test was not added to LightNap eligibility.
- Battery polling cadence is 45s active, 120s idle Balanced, 180s idle Max Battery. Added `PWR` instrumentation logs voltage/current every 5s while `PWR_TRACE` is enabled.
- Display refresh locks input for 250ms on fast refresh and 600ms on clean refresh; `prepareFullRefresh()` restores active CPU before drawing.
- Repeated redraw warning exists in `prepareFullRefresh()` after 3 repeated same-screen/same-reason renders; no render loop was added or found in `loop()`.

## Skipped or deferred

- Did not fetch, subset, embed, or convert external fonts.
- Did not author or import new Japanese question content.
- Did not implement real Mock Test logic, deeper Reference behavior, SRS, Review, or cross-source learning.
- Did not change Interview content/rendering intentionally.
- `wrapJapaneseTextToLines()` already breaks by measured `display.textWidth()` per UTF-8 codepoint, so no wrapper conversion was needed.
- Font Lab reports whether the source string contains `?`; physical e-ink glyph fallback still needs morning visual QA.
- Japanese results persistence is backend-only. Existing Japanese Results UI remains the simple current-session summary.

## Final build

- Final `firmware.bin`: `3757040` bytes.
- Final PlatformIO report: RAM `70.4%`, flash `57.3%`.
- Stop line `firmware.bin > 12MB` was not reached.

## Morning QA checklist

- Boot serial: confirm firmware starts, SD mounts, and Japanese session counter/log messages do not block boot.
- Settings -> Max Battery: leave Home idle for at least 10s; confirm `PWR idle enter` and CPU scale to 80MHz.
- Check Home, Interview menu, Japanese menu, Japanese source/week/day, Reference, Results, Settings, Advanced, Power Lab for WarmIdle expectations.
- Check Drills pre-answer, Exam pre-answer, and Japanese Daily pre-answer do not enter LightNap while awaiting an answer.
- Japanese -> Font Lab (JP): cycle all pages; verify `_20/_24/_28/_32/_36/_40`, mixed header/prompt/choice, and meta sizes render without `????`.
- Japanese source/week/day: verify `500問` and `ぶんぽう` render as Japanese, not boxes or `?`.
- Japanese Daily: answer at least one correct and one incorrect item; verify feedback grammar tag is readable at >=24px and no sanitizer WARN appears for normal Japanese UI.
- Reboot with SD inserted; confirm `/papercoach/japanese/progress/session_counter.txt` increments and `/papercoach/japanese/progress/results.log` is append-only NDJSON.
- Corrupt-tail test if desired: append a partial line to Japanese results log and confirm boot skips it without failing.
- Confirm Interview Practice, Drills, Exam, Glossary, and Results still render and behave as before.

## 2026-06-18 Japanese sanitizer shipping pass

### Commit

- `62e106a` - Add standing workflow rules and fix the remaining Japanese sanitizer routes.

### Call-site audit

- Fixed Japanese Daily feedback English explanations: mixed English/Japanese examples previously entered `wrapTextToLines()` and now use `drawMixedJapaneseLabel()`; Japanese-bearing lines render through Gothic at 24px while English-only lines retain the 20px English font.
- Fixed the `500問 N3` source button: it now uses the explicit `drawJapaneseButton()` path instead of generic `drawButton()`.
- Already safe: Japanese week/day subtitles use `drawMixedJapaneseLabel()`.
- Already safe: Japanese Daily header, prompt, choices, answer sentence, Japanese explanation, and grammar tag use Japanese-safe draw/wrap helpers.
- Already safe: Reference Kanji, Grammar, and Vocabulary content uses `drawJapaneseWrappedText()`.
- Results rows are currently English-only; no Japanese-bearing Results row enters `sanitizeCoachText()` or `wrapTextToLines()`.
- Japanese menu labels are English-only; Japanese source metadata uses the mixed-safe path.
- `Font Lab (JP)` is rendered in the Japanese submenu and its touch handler opens Font Lab page 1.

### Verification

- `pio run` passed after the commit: RAM `70.4%`, flash `57.3%`.
- `firmware.bin`: `3757504` bytes, below the 12MB limit.
- Static diff review confirmed no Interview render/touch path changes and no changes to `applyTypographyFont()` or `applySansBoldFont()`.
- Japanese result storage and Interview result storage were untouched.

### Morning QA

- Japanese -> Daily Questions -> `500問 N3`: confirm `問` renders without `?`.
- Answer the items whose English explanations contain `違っていました` and `引っ越した`; confirm the embedded Japanese renders without `?` and wraps within the content area.
- Check Japanese week/day headings, feedback header/body/grammar tag, Reference rows, and Results for layout regressions.
- Open Japanese -> Font Lab (JP), confirm it reaches page 1, and cycle all pages.
- Smoke-test Interview Practice, Drills, and Exam rendering and touch behavior.

## 2026-06-23 hardware layer and Reader refactor

### Commits

- `953cf0a` - reference-notes.
- `b2965ae` - hardware-layer.
- `f3d37cc` - power-diagnostics.
- `96eb04e` - reader-app.
- `docs` - documents this pass, test plan, and shipping notes.

### Verification so far

- `pio run` passed after `hardware-layer`: RAM `70.5%`, flash `57.5%`.
- `pio run` passed after `power-diagnostics`: RAM `70.5%`, flash `57.6%`.
- `pio run` passed after `reader-app`: RAM `70.6%`, flash `58.0%`.
- Latest `firmware.bin` before docs: `3802016` bytes, below the 12 MB guardrail.

### Hardware QA

- Verify Reader list/open/page-turn/font/home with TXT/MD files in `/paperBadge/books`.
- Verify Power Lab wake/reset/RTC/display/SD/heap rows on USB and battery.
- Verify GT911 light-sleep GPIO wake does not wake-loop on the physical PaperS3.
- Smoke-test Badge, Interview, Japanese, and Settings after flashing.
