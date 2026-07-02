# Font/Manga Reader Sprint — Read-Only Red-Team Review

Branch reviewed: `font-manga-reader-sprint` (HEAD `e5ea710`)
Review type: read-only code audit + physical-QA prep. No code was changed, committed, pushed, or flashed during this review.

Implementation status: follow-up branch `qa-font-manga-interview-fixes` addressed the P0/P1 items for Font Lab readability/runtime clarity, manga backward slice navigation, CBZ natural sort/hidden filtering, manga overlay/touch zones, PNG fit/slice rendering, IMU probe logging, Interview Practice pagination/size wiring, EPUB cap messaging, and archive diagnostics. Line references below describe the reviewed base branch and may drift after those fixes.

---

## 1. Executive Summary

The sprint's manga rendering math (fit-page/width/height, slice cropping, JPEG scale selection) is **correct** — no P0 bugs found there. The user's "only half a page visible, unclear what to tap" complaint is **not a rendering bug**; it's a **discoverability gap**: `render_manga_page()` draws zero UI chrome (no header, footer, slice counter, or hints) over a full-bleed page, and landscape auto-switches to FitWidth, which — given typical manga aspect ratios — genuinely only shows ~⅓–½ of the page per slice by design. There's no on-screen cue this is happening or where to tap next. That's the #1 fix for next sprint.

A real, confirmed bug exists in backward slice/page navigation (lands on slice 0 instead of the last slice of the previous page). Orientation isolation across Badge/Interview/Japanese is **solid** — every render function forces portrait on entry; no leak paths found.

The IMU question has a real answer: the active ESP-IDF `touch.cpp` itself contains the evidence — `GPIO_NUM_48` is commented `"shared with BMI270 INT"`. The chip is confirmed present in this codebase's own hardware notes; only the driver is missing. This is a real next-sprint feature, not a hardware dead end.

EPUB has a bigger documentation problem than a code problem: `src/reader/EpubReader.*` is dead legacy code (Arduino-only, `#include <Arduino.h>`, not in `CMakeLists.txt`). The **real, active** EPUB parser is inline in `paperbadge_main.cpp` (`read_epub_text`), is reasonably solid, and the most likely cause of "~10MB EPUBs usually fail" is a single-XHTML-file EPUB tripping the 2MB-per-entry cap, or many chapters tripping the 2MB total-text cap — both plausible with real-world EPUBs.

Archive handling has one real correctness bug worth P0 attention: **page sort is lexical, not natural** (`page10.jpg` sorts before `page2.jpg`), which silently scrambles any manga with 10+ pages using non-zero-padded filenames.

Font Lab candidates are real embedded glyph bitmaps (not label-only), correctly scoped as QA-only, and **not** wired to production rendering — confirmed by design, not a bug. Two settings are decorative: Interview Size (P1, misleads users) and Western Font Profile (P2).

---

## 2. Branch/Build Status

- Branch: `font-manga-reader-sprint`, HEAD `e5ea710`, working tree **clean**.
- `heavy-paperbadge-sprint` **is** an ancestor of the current branch (confirmed via `git merge-base --is-ancestor`) — no divergence/rebase risk.
- `pio run`: **SUCCESS**. RAM 27,496 / 327,680 bytes (8.4%). Flash 1,540,761 / 11,534,336 bytes (13.4%, ~1.47MB — well under the 12MB firmware.bin cap). No warnings surfaced in the terse output.

## 3. Flash Status

**No flash was performed during this review.** The build ran `pio run` only (no `-t upload`). The user's earlier manual `pio run -t upload --upload-port /dev/cu.usbmodem1101` stands as the last flash event; this session did not touch the device.

## 4. Manga Landscape/Touch Red-Team Findings

**Confirmed tap zones (verified directly, `paperbadge_main.cpp:3221-3269`), identical logical layout in portrait and landscape** — these are computed off `ps3::display::width()/height()` (post-rotation logical dimensions), not physical panel dimensions:

| Zone | Condition | Action |
|---|---|---|
| Top strip | `y < 120` | Close book → return to Manga Library |
| Bottom strip, left half | `y > height-86 && x < width/2` | Cycle fit mode (Page→Width→Height→Page) |
| Bottom strip, right half | `y > height-86 && x >= width/2` | Toggle orientation (portrait↔landscape) |
| Middle, left/right (swapped by `right_binding` setting) | else | Advance/retreat slice, then page |

Because these use logical (not physical) pixels, the transform is directionally consistent — `hal/touch.cpp` applies the same rotation constant as `hal/display.cpp` — **no coordinate-transform bug found**.

**The real problem is proportion, not mapping.** The 120px top zone is 12.5% of height in portrait (960px) but **22% of height in landscape** (540px), and the 86px bottom zone goes from 9%→16%. The usable "turn page" middle zone shrinks from ~78% to ~62% of the screen in landscape. Not broken, but tighter and — critically — **completely unmarked**.

**Confirmed defect — no UI chrome during manga reading** (`render_manga_page()`, lines 2005-2033): no `draw_header()`, no `draw_footer()`, no slice indicator, no hint text. The full-bleed page is the only thing drawn. Every other screen in the app (Reader, Interview, Settings) calls `draw_header`/`draw_footer`. Manga reading uniquely doesn't. This is the direct, verified root cause of "unclear where to tap."

**Confirmed defect — landscape zoom is real, by design, and unexplained.** `toggle_manga_orientation()` (line 1097-1107) auto-switches `FitPage → FitWidth` on entering landscape. FitWidth scales to the full 960px logical width; for a typical manga page this produces a scaled height well beyond the 540px screen height, requiring 2-3 slices per page. **The user seeing "only half a page" is the correct current behavior of FitWidth+slicing** — just with zero on-screen indication that slicing is happening or how many slices remain. This is a discoverability bug, not a math bug (the rendering-agent traced the crop/slice math and found it correct — ceiling-division slice boundaries, no gaps/overlaps).

**Confirmed bug (P0) — backward slice/page navigation lands wrong.** `handle_manga_reading()` line 3255-3258: going back from slice 0 of a page sets `g_manga_slice = 999` (sentinel meaning "last slice") and `g_manga_slice_count = 1` (stale). `render_manga_page()` (line 2018-2020) recomputes the *real* slice count from the decoder *after* this, but clamps `g_manga_slice` **before** that recompute takes effect in the render call that follows — net effect: user tapping "previous" from slice 0 of page N lands on slice 0 (not the last slice) of page N-1, when they'd expect the last slice. Concrete repro: 3-slice landscape page, tap back at slice index 0 → previous page opens on its first slice instead of its third.

**Orientation restore — verified exhaustive, not just "on Manga exit."** Two independent agents plus direct source reading confirm: *every* render function for every screen (Badge, Interview×9, Japanese×10, Home, Settings, Reader, Manga Library/Error) calls `restore_app_orientation()` or an equivalent explicit rotation-set at the *start* of its own render — not only when leaving Manga. So even if a hypothetical crash path skipped `close_manga_book_if_open()`'s (line 1316-1321, which itself does *not* restore orientation) cleanup, the next screen's own render call would still force portrait. The theoretical gap (an early-return between `before_leave_screen()` and the next `render_screen()` call) is P2/theoretical — no confirmed live path triggers it.

**Answers to the numbered questions:**
1. Portrait tap zones: as table above (same code path).
2. Landscape tap zones: same logical zones, different proportions (top/bottom strips consume more of the shorter 540px height).
3. "Top/header" is defined in **logical, post-rotation** coordinates (`ps3::display::height()`), consistently in both `hal/touch.cpp` and `hal/display.cpp`.
4/9. Touch remap matches display rotation — verified, no swap/pivot bug.
5. No overlapping zones — clean partition (top / bottom-left / bottom-right / middle-left / middle-right).
6. Slice state resets correctly on orientation change (line 1099-1100) and fit-mode change (`invalidate()`), correctly on forward page-turn — **incorrectly on backward page-turn** (Defect above).
7. Leaving Manga always restores portrait via the destination screen's own render call — verified.
8. No confirmed live orientation-leak path into Badge/Interview/Japanese/Settings/Home. `close_manga_book_if_open()` not restoring orientation itself is a latent gap but every observed call site chains immediately into a portrait-restoring render.
10. Top-left Back / top-right Home convention doesn't apply inside Manga reading at all — Manga uses its own top-strip/bottom-strip scheme, disconnected from the app's header/footer convention used everywhere else. That inconsistency itself is worth calling out to Codex.

**Proposed control map** (the "Suggested design to evaluate" from the review prompt) is sound and directly addresses the defects above — recommend adopting it largely as specified, with the added requirement that overlay/menu chrome actually gets drawn (currently doesn't exist at all), and an explicit slice-position indicator ("2/3") plus first-run micro-hints given how invisible the current zones are.

## 5. Manga Rendering/Fit/Slice Findings

All fit-mode math verified correct (independently traced by the rendering-review agent):
- **Fit-page**: aspect-preserving, picks width- or height-limited scale by comparing `src_w*screen_h` vs `src_h*screen_w`. Correct.
- **Fit-width**: `dst_w = screen_w`, `dst_h` scaled proportionally. Correct.
- **Fit-height**: symmetric, correct.
- **Landscape fit-width is a genuine zoom**, not just "rotated full page" — confirmed via `toggle_manga_orientation()` auto-mode-switch plus the width value doubling from 540→960 logical pixels. This is intended behavior, not a bug (see Section 4).
- **Slice boundaries**: ceiling-division, clamped, no gaps or double-coverage.
- **Scaling method**: nearest-neighbor viewport mapping from JPEGDEC's native 1/1, 1/2, 1/4, 1/8 scale-decode levels — matches docs exactly ("functional, not high-quality").
- **PSRAM**: framebuffer slots and PNG decoder allocate via `MALLOC_CAP_SPIRAM`; only a small internal-RAM dither buffer (~17KB) is not PSRAM, and dithering is disabled by default (`kUseDither = false`), so this isn't a live risk.
- **PNG is confirmed fit-page-only** — hardcoded in `page_loader.cpp:428-432`, ignores the current fit/slice state entirely; `slice_count` forced to 1.
- **Real gap worth fixing**: `display_png()`/`display_png_at()` (used for splash/dialog images, not manga pages) lack the width guard that `display_png_fit()` has (`PNG_ROW_BUF_PIXELS = 4400`), theoretically overflowable by a malformed wide PNG — low real-world risk (P2) since these paths only render trusted embedded assets today, but worth hardening if PNG-from-SD paths grow.
- **No explicit JPEG dimension ceiling** before scale-factor selection — low risk given JPEGDEC's own bounds, but an explicit `if (src_w > N || src_h > N) return false;` would be cheap insurance.

**What's needed for PNG fit-width/slice**: generalize `display_png_impl()` into a `display_png_view()` analogous to `display_jpeg_view()`, applying the same fit-math and slice-offset logic already proven correct for JPEG (`image_display.cpp:226-355` as the template). Scoped, moderate-size change.

**What's needed for WebP**: no existing hook — new `PageImageFormat::WebP` enum value, a vendored decoder (libwebp or a lean ESP32 WebP decoder), and a new `draw_callback_webp()` following the JPEG/PNG pattern. This is a multi-day integration, not a quick add.

## 6. IMU/Gyro Feasibility Findings

**Verified directly**: `src/hal/touch.cpp:22` — `constexpr gpio_num_t PIN_GT911_INT = GPIO_NUM_48; // shared with BMI270 INT`. This is in the **active** ESP-IDF file, not the legacy tree — the current firmware's own author left a note that GPIO48 is shared with a BMI270 interrupt line. Combined with `docs/NEXT_WORK_QUEUE.md:18` ("Add a real BMI270/BMI270-like IMU driver only after confirming address...") this is strong, self-consistent in-repo evidence the chip is present, not a hypothesis.

- I2C bus: `I2C_NUM_0`, SDA=GPIO41, SCL=GPIO42, 400kHz — same bus GT911 touch uses. BMI270 standard addresses (0x68/0x69) are not yet confirmed by an actual register probe in this repo (no code has attempted it) — that's the one open unknown, not whether the chip exists.
- `src/main.cpp:11428` (`cfg.internal_imu = false;`) — this was set from the project's **first commit**, framed by README.md as a deliberate power/design choice for a static badge use case ("Badge mode is static by default... IMU polling is disabled"), not a discovered hardware failure. Disabling it says nothing about hardware capability.
- `platformio.ini` explicitly ignores M5Unified/M5GFX (`lib_ignore`) and `framework = espidf` — this is a pure ESP-IDF build. Pulling in M5Unified just for IMU access would fight the project's own architecture and is not recommended.
- No IMU driver exists anywhere in the ESP-IDF `src/CMakeLists.txt` component list today — this is purely a missing driver, not a missing chip.

**Recommendation: (A) implement a minimal ESP-IDF IMU driver next sprint**, reusing the existing `hal/touch.cpp` I2C init pattern as the template (it's the closest in-repo reference for size/complexity — ~250-300 lines, low flash/RAM cost). Before writing code, Codex should do a one-time register probe (read WHO_AM_I/CHIP_ID at 0x68 then 0x69) to nail down the exact chip variant, since "BMI270" is the expected part per docs but hasn't been read off silicon in this repo. Recommend: manga-only polling (~10-20Hz, well below GT911's 100Hz task), debounce with a 300-500ms settle window before triggering a rotation, and suppress polling while a page is actively decoding/rendering to avoid a rotation flip mid-render.

## 7. Manga Archive Format Findings

- **ZIP64 detection is correct and spec-appropriate**: scans for central-directory markers (`0x06064B50`/`0x07064B50`) in the archive tail, not file size — matches `DEVICE_LEARNINGS.md` claim exactly (verified: `paperbadge_main.cpp:281`, used at `:3167`).
- **The old 50MB size guard is gone**; current ceiling is governed by available PSRAM for the central-directory load, which happens **in one shot**, not streamed — realistic safe ceiling is bounded by directory *entry count* more than raw file size (a 150MB CBZ with a few hundred entries opens fine; a pathological archive with huge entry counts could exhaust PSRAM on the directory alone). Page *image* data itself **is** streamed one entry at a time through the existing 9-slot decode-cache — that part is memory-safe regardless of archive size.
- **Failure mode for a huge (1.4GB) non-ZIP64 CBZ**: graceful malloc failure surfaced through the existing generic archive-error screen (`paperbadge_main.cpp:3192-3207`) — not a crash, but the diagnostic doesn't distinguish "too big for RAM" from "corrupt" or "unsupported format," which is a real usability gap for the user's actual troubleshooting.
- **RAR/CBR detection is extension-based only** (`.cbr` suffix check) — a `.rar` renamed to `.zip` would pass the archive-open path and fail deep inside miniz with a confusing "no displayable entries" message rather than a clean RAR-specific error.
- **WebP has zero detection** anywhere — filtered out silently by the `.jpg/.png`-only entry filter if named `.webp`, or reaches JPEGDEC and fails opaquely if misnamed as `.jpg`.
- **Confirmed P0-worthy correctness bug: page sort is lexical (`strcmp`), not natural.** `cbz_book.cpp` sorts filenames as plain strings — `page10.jpg` sorts before `page2.jpg`. Any manga with ≥10 pages and non-zero-padded numbering renders **completely out of order**. This is a small, high-value fix (numeric-aware comparator) and should be near the top of next sprint's list — it's silent corruption, not a crash, which makes it worse (a user could read a scrambled book without ever seeing an error).
- **`__MACOSX/` and hidden-file entries are not filtered** — a Mac-zipped CBZ with `__MACOSX/thumbnail.png` metadata would be treated as a real page.
- **Nested folder structures work correctly** — full path used as filename, sorts (lexically, same caveat as above) fine for flat-numbered chapters.
- Error screen gives file size + ZIP64 status + generic possible-cause bullets but ultimately punts to "check serial log" — not actionable for a non-technical QA pass.

**Recommendation split**: fix natural sort and `__MACOSX` filtering in firmware (small, high-value, no new dependencies). Leave ZIP64 parsing, RAR/CBR, and WebP as host-preprocessing-only for now — each is a multi-day firmware investment for a use case the Mac preprocessor already covers.

## 8. EPUB Findings

**Correction to initial sub-agent finding**: the archive/EPUB investigation agent read `src/reader/EpubReader.h/.cpp` and concluded EPUB is entirely unimplemented (stub returns `false`). This was verified directly — that file **is** a stub, but it's dead legacy code: it `#include <Arduino.h>` and is **not** listed in `src/CMakeLists.txt`'s `SRCS`. The active EPUB parser is a set of free functions inline in `paperbadge_main.cpp` (`read_epub_text`, lines 789-915, plus helpers from line 650), read directly to confirm.

**Actual current caps** (verified, `paperbadge_main.cpp`):
- File-size gate before any parsing: EPUB max 16MB, else "File too large" (line 2123-2126).
- `kMaxXmlBytes = 512KB` — for `container.xml` and the OPF package file.
- `kMaxHtmlEntryBytes = 2MB` — **per individual spine XHTML file**; an entry over this is skipped (with a serial warning), not fatal to the whole book.
- `kMaxExtractedTextBytes = 2MB` — total across all stripped spine text; once hit, extraction stops and a "[Partial EPUB...]" notice is appended (line 910-911) — this **is** a real, working partial-extraction path.

**Likely real cause of "~10MB EPUBs usually fail"**: two plausible mechanisms, both consistent with the code:
1. A single-file EPUB (one big XHTML chapter >2MB uncompressed — common in auto-converted or scanned EPUBs) gets **skipped entirely** by the per-entry cap; if it's the *only* spine entry, `result.text` stays empty and the user sees "EPUB parsed, but no readable text was extracted" (line 906-908) — a hard failure with no partial content, even though the file "should" open.
2. A normal chaptered EPUB with many small chapters can genuinely exceed 2MB of *cumulative stripped text* well before the file itself is 10MB compressed — this correctly triggers graceful truncation, not failure, but the user may perceive a truncated 200KB-of-text result as "the book didn't really open."

**Parser quality, verified by reading the code**: OPF/spine parsing is a **substring scanner**, not a real XML parser — it looks for literal `<rootfile`, `<item`, `<spine`, `<itemref` tags. This works for typical unprefixed OPF/container XML (the common case) but would silently fail to find spine items if a producer namespace-prefixes element names (e.g., `<opf:item>`) — rare but not impossible from some EPUB-generation tools. Relative href resolution (`resolve_zip_href`/`normalize_zip_path`) correctly handles `../` and subdirectory paths. HTML entity handling reuses `strip_html`/`xml_attr_unescape` — bounded by the same entry-size caps, no unbounded buffer risk found. No UTF-8/Japanese-specific caveats surfaced in the extraction path (it's byte-level text pass-through, cap boundaries could theoretically split a multi-byte UTF-8 sequence mid-character since the truncation is a byte-count `resize()`, not a codepoint-aware cut — worth a one-line fix to snap the cut point back to a valid UTF-8 boundary).

**Recommendation**: raise or better-communicate the per-entry cap distinctly from the total cap (a user should be told *which* limit they hit), and snap truncation to a UTF-8 character boundary. Chapter-lazy pagination (only decode/extract the current chapter) is the "big" fix if 2MB total text remains too small for real books, but is a bigger lift — worth scoping for a P1/P2 sprint rather than immediate.

## 9. Font Lab Findings

- **Real, not label-only**: `font_lab_assets.cpp` contains actual 24×24px 1bpp glyph bitmap arrays per candidate (~165 codepoints each: ASCII + hiragana/katakana + ~50 kanji + CJK punctuation for the Japanese-capable faces; Inter and Source Serif 4 are Latin-only).
- Rendered via the same sparse-font lookup path as production fonts (`XTEinkFont::pixel()`), at genuine 24px native and 48px scaled samples — confirmed in the render call sites.
- **Clearly scoped as QA-only in the UI text** ("Production runtime faces: BIZ UDGothic, IPAex Gothic" is shown directly on the Font Lab screen) — low risk of user confusion about "have I changed my reading font."
- **Confirmed not wired to production rendering** — `select_japanese_font()` only ever toggles between BIZ UDGothic and IPAex Gothic; there is no code path from Font Lab selection to the `g_font` pointer used everywhere else. This is by design, matching docs, not a bug.
- Tool-output consistency: the committed `font_lab_assets.cpp/.h` structurally matches what `tools/font_candidates.py --emit-firmware` would generate (array names, glyph counts, fingerprint marker) — plausibly real tool output, not hand-edited.

**To promote a candidate to production** would require: extending the font-face enum, adding real `XTEinkFont` globals backed by a *much larger* glyph subset than Font Lab's 165 codepoints (production BIZ/IPA subsets are ~3,500 chars), and wiring a third option into the Settings font picker. This is a meaningfully larger content-generation task (glyph coverage), not just plumbing — flag this expectation before scheduling it.

## 10. Settings/Refresh Findings

Verified persisted vs. dead settings:

| Setting | Persisted? | Actually affects rendering? |
|---|---|---|
| Refresh profile (Fast/Balanced/Clean) | Yes | **Yes** — drives `refresh_page_turn_mode()`/cadence counter feeding real `GC16`/`GC16Full` flush calls |
| Clean refresh cadence (`full_refresh_pages`) | Yes | **Yes** — modulo counter (`g_pages_since_full`) verified live |
| Reader Size (S/M/L/XL) | Yes | **Yes** — drives `reader_line_gap()`/`reader_lines_per_page()`, changes actual pagination |
| Japanese Size | Yes | **Yes** — drives `japanese_line_gap()`/`japanese_choice_height()` |
| Japanese font face (BIZ/IPA) | Yes | **Yes** — swaps `g_font` pointer used by all rendering |
| **Interview Size** | Yes | **No — confirmed dead.** Persists and displays, but no interview render function reads it; interview text uses hardcoded gaps. **P1**: user-visible setting with zero effect, actively misleading. |
| **Western Font Profile** | Yes | **No — confirmed decorative.** Only changes a label string ("Firmware sans" vs "FontLab Inter preview"); never touches an actual font. **P2**. |

Settings navigation: no dead-ends found — every sub-page has a working Back path to the settings menu. No orientation-leak risk into Settings — `restore_app_orientation()` fires at the top of every settings render, same pattern as the rest of the app.

**Recommendation**: wire Interview Size into `render_interview_practice`/drill/feedback gap calculations (small, mirrors the Japanese-size pattern already proven in the same file) — quick, high-value fix. Either implement or remove the Western Font Profile toggle; leaving a fake toggle in Settings actively erodes user trust in the rest of Settings.

## 11. Badge/Interview/Japanese Regression Findings

**No regressions found.** Verified (independently, both by the dedicated review agent and cross-checked against direct reading of the orientation-restore pattern in Section 4):

- Badge: EN/JA toggle, QR zoom/return, no-footer, and the 180° final sleep/power-off frame all present and unaffected by Manga's landscape state — the final-frame path uses a locally-scoped `previous_orientation` variable, never touches `g_manga_landscape`.
- Interview: all 9 render functions (menu, card list, practice, drill Q/feedback, glossary list/detail, results) call `restore_app_orientation()` at entry — none can inherit landscape.
- Japanese: all 10+ render functions (source/unit/lesson, practice, feedback, mock, mock results, reference, results, font settings) do the same. Japanese font rendering routes exclusively through `active_font()`/`g_font`, unaffected by Font Lab additions.
- Single source of truth for orientation is `s_rotation` in `hal/display.cpp`, set exclusively through `set_screen_rotation()` — no second/competing global found.

The one thing worth flagging per the repo's guardrails: `applyTypographyFont`/`applySansBoldFont` (named in this repo's guardrails) don't actually exist in the active `paperbadge_main.cpp` — they're legacy-tree-only names. Codex should know this so it doesn't waste time hunting for them in the active file.

## 12. Host Tools/Schema Findings

Both `tools/manga_preprocess.py` and `tools/font_candidates.py` are plain Python 3 with portable `pathlib`-based paths, runnable on the user's Mac with `pip install Pillow` (+ optional `fonttools`, `google-generativeai` for OCR) — no Mac-specific blockers found. Every CLI flag claimed in `docs/MANGA_READER_LIMITS.md` (split, `--zip64 never`, `--downscale portrait/landscape-slices`, `--slices`, `--grayscale16`, `--ocr gemini`) exists in the actual `argparse` setup — docs are accurate, not aspirational.

**Important finding for prioritization**: the OCR sidecar's `text.json` is genuinely populated (raw Gemini output preserved, per-page/slice linkage present) when `--ocr gemini` runs, but `vocab.json`/`concepts.json`/`page_map.json` are currently **empty stub placeholders** (`{"items": []}`), not yet populated from the OCR response — schema exists, extraction logic doesn't yet.

**Firmware consumes none of this today.** Grep across `src/` confirms zero read/parse calls for `manifest.json`, `page_index.json`, or any `ocr/*.json` file. `source_registry.json` is checked for *existence* (`stat()` at `paperbadge_main.cpp:2262`) but never opened or parsed. All of this host-side work is currently **inert with respect to firmware** — real infrastructure for a future sprint, but zero present-day user-facing benefit. Worth being explicit with the user about this so "upgrade manga preprocessor" doesn't get credited with capabilities the device doesn't yet use.

## 13. Recommended Next Codex Sprint Plan

**P0 — fix before anything else:**

| Item | Files/functions | Risk | Build/flash impact | Codex should | QA |
|---|---|---|---|---|---|
| Backward slice/page nav lands wrong slice | `paperbadge_main.cpp:3253-3258` (`handle_manga_reading`) | Low — isolated state-order fix | None | Implement | Load a 3-slice landscape page, tap back from slice 0, confirm lands on last slice of prior page |
| Natural (not lexical) page sort in CBZ | `src/comic/cbz_book.cpp` sort comparator (~line 123-140) | Low — self-contained comparator swap | None | Implement | Open a CBZ with `page1.jpg`...`page12.jpg` unpadded, confirm reading order 1→12 |
| No visual chrome / slice indicator in Manga reading | `render_manga_page()` `paperbadge_main.cpp:2005-2033` | Medium — touches the render/refresh-mode call user cares about most; must not break existing e-paper refresh cadence | Small flash increase (few more `draw_*` calls) | Implement, but **QA the refresh cadence carefully** since more draw calls per page-turn could interact with Fast/Balanced/Clean profiles | Confirm slice indicator ("2/3"), and that tap-zone hints don't visibly ghost/smear on partial refresh |

**P1 — high value, scoped:**

| Item | Files/functions | Risk | Build/flash impact | Codex should | QA |
|---|---|---|---|---|---|
| Adopt the proposed portrait/landscape control map (proportional zones, distinct fit/orientation vs next/prev regions) | `handle_manga_reading()`, possibly new overlay render path | Medium — UX-visible change to muscle memory from current QA notes | Minimal | Implement | Full manual tap-zone walkthrough in both orientations per QA checklist below |
| Minimal ESP-IDF BMI270 driver + manga-only auto-rotate | New `src/hal/imu.cpp/.hpp`, registered in `src/CMakeLists.txt`; hook into `toggle_manga_orientation()` path | Medium — new I2C traffic sharing bus with GT911; needs on-device address probe first | Small (<2KB flash estimate) | Implement, but **first task should be a register-probe-only diagnostic build** (log CHIP_ID at 0x68/0x69) before writing the real driver | Confirm touch responsiveness unaffected with IMU polling active; confirm rotation debounces (no flip mid-page-turn) |
| PNG fit-width/slice parity with JPEG | `image_display.cpp` (new `display_png_view`), `page_loader.cpp:428-432` | Low-medium — reuses proven JPEG fit/slice math | Small | Implement | PNG-in-CBZ page rotated to landscape now zooms/slices like JPEG does |
| Interview Size actually affects layout | `render_interview_practice`/drill/feedback gap calcs, mirroring `japanese_line_gap()` pattern | Low | None | Implement | Cycle Interview Size S→XL, confirm visible spacing change |
| Fix EPUB per-entry vs total-cap messaging + UTF-8-safe truncation boundary | `paperbadge_main.cpp` `read_epub_text` (~line 789-915) | Low | None | Implement | Open a single-big-chapter EPUB and a many-small-chapter EPUB near 10MB; confirm distinct, accurate error/partial messages |
| Archive error screen distinguishes OOM vs corrupt vs unsupported-format | `paperbadge_main.cpp:3192-3207` | Low | None | Implement | Trigger each failure mode, confirm distinct message |

**P2 — document or defer:**

| Item | Recommendation |
|---|---|
| ZIP64 firmware parsing | Document as host-preprocessing-only; not worth firmware complexity given `tools/manga_preprocess.py --zip64 never` already covers it |
| CBR/RAR firmware support | Document as host-convert-only; RAR licensing/decoder cost not justified |
| WebP decoder | Defer; scope only if user's actual library has meaningful WebP content — multi-day integration |
| Western Font Profile setting | Remove the toggle or implement it — currently pure UI debt |
| Sidecar (manifest/page_index/OCR json) firmware integration | Defer to a dedicated sprint once vocab/concepts extraction is actually populated (currently stub) |
| `__MACOSX`/hidden-file filtering in CBZ | Small firmware fix, bundle with the natural-sort fix since it's the same code region |

## 14. Exact QA Checklist for the User (Physical Device)

1. Open a small JPEG CBZ in portrait — confirm fullscreen fit-page, top-tap returns to library.
2. Rotate to landscape (bottom-right tap) — confirm it visually rotates to 960×540 and auto-switches to a zoomed fit-width view.
3. In landscape, tap the middle-left and middle-right zones — confirm slice advances/retreats; note whether you can currently tell how many slices remain (expected: no, until the P0 chrome fix lands).
4. From a mid-slice, tap backward repeatedly to cross a page boundary — confirm you land on the **last** slice of the previous page (currently: you'll land on slice 0 — this is the confirmed bug).
5. Open a CBZ with 10+ unpadded page filenames (`page1.jpg`...`page12.jpg`) — confirm reading order (currently: expect scrambled order, e.g., 1,10,11,12,2,3...).
6. Leave Manga to Settings, Home, Interview, Japanese, Badge from mid-landscape-read — confirm each screen renders in correct portrait orientation, not rotated.
7. Try a PNG-in-CBZ in landscape — confirm it stays fit-page (no zoom), unlike JPEG pages in the same archive.
8. Open a large non-ZIP64 CBZ (~100-150MB) — confirm it opens or fails gracefully with a readable message, not a hang/crash.
9. Try a `.cbr` file — confirm the clean "not supported" message (not a generic parse failure).
10. Open EPUBs: one with a single big chapter (~3MB+ uncompressed XHTML) and one with many small chapters totaling near 10MB — note exact error/partial-extraction text shown for each, to confirm the P1 cap-messaging fix targets the real complaint.
11. Font Lab: confirm all 11 candidate pages show distinct glyph shapes for both Latin and Japanese sample text, and that the screen clearly states production font stays BIZ/IPA.
12. Settings: cycle Reader Size, Japanese Size, Interview Size, Refresh Profile — note (for confirmation) that Interview Size currently produces **no visible change** (expected, pending fix).

## 15. Rollback Advice If the Flashed Build Misbehaves

- Current `main` and `heavy-paperbadge-sprint` are both clean ancestors of `font-manga-reader-sprint` with no divergence — a safe rollback target is re-flashing the `heavy-paperbadge-sprint` build (`git checkout heavy-paperbadge-sprint`, `pio run -t upload --upload-port /dev/cu.usbmodem1101`) if the manga/font sprint proves unstable on-device, since Badge/Interview/Japanese are unaffected either way per Section 11.
- If only Manga misbehaves post-QA-photos, the safer partial rollback is reverting the manga-specific commits (`ca400e2` orientation, `4b16d28` slice rendering, `f37a864` archive support) individually via `git revert`, keeping Font Lab (`ed4e23f`, `6dba52c`) and EPUB (`f7db450`) changes, since those are independently verified clean in this review.
- No data/state migration risk: `settings.kv` schema additions in this sprint are additive (new keys), so rolling back the binary while leaving the SD card's `settings.kv` in place should not corrupt settings — worst case, older firmware ignores the new keys.
