# Responsiveness / Reader / JP Fonts Pass Plan

Branch: `responsiveness-reader-jp-fonts-pass`

## Root cause of poor first-tap responsiveness

`loopDelayMs()` returns **300ms** when `gIdleModeActive && isStaticIdleScreen(gScreen)` in
Responsive profile (`PowerProfile::Balanced`). Idle mode kicks in after 30 seconds.

After 30s idle, the main loop only runs every 300ms. A normal tap lasts 80-150ms.
If the loop is in `delay(300)` when the tap happens, `M5.update()` is never called
during the tap — the touch is lost completely. Subsequent taps eventually succeed
because the user taps harder/longer until one lands during a 50ms active window.

Note: `recordUserActivity()` does call `restoreActiveCpu()` on touch-press, but
if the loop is sleeping for 300ms, touch-press is never detected to trigger that path.

**Fixes:**
1. `loopDelayMs()`: Responsive profile caps idle delay at **50ms** (same as active)
2. `profileIdleScaleThresholdMs()`: Responsive WarmIdle threshold 30s → **60s**
   (reduces chance of entering idle mode quickly after settling on a menu)
3. Add a diagnostic serial log when a tap is ignored and why

## How Reader fonts are currently selected

`ReaderApp::render()` calls `display.setTextFont(2); display.setTextSize(1)` which
selects M5GFX built-in font #2 (small ~7×10px bitmap, similar to Arduino LCD font).
`renderReading()` then calls `display.setTextSize(textScale())` where `textScale()`
returns `fontSize_` (1–3). Even at scale 3, characters are ~21×30px — poor readability.

`charsPerLine()` and `lineHeight()` use pixel estimates that match the old font,
so with the old font content area is correct but unreadably tiny.

**Fix:** Replace `setTextFont(2)` + `setTextSize(scale)` in reading view with
FreeSansBold fonts already used by the main app:
- fontSize_=1 (Reader S): FreeSansBold12pt7b (~24px)
- fontSize_=2 (Reader M): FreeSansBold18pt7b (~34px)
- fontSize_=3 (Reader L): FreeSansBold24pt7b (~44px)

Update `lineHeight()` and `charsPerLine()` to match the new fonts.
Library list entries also get FreeSansBold12pt7b for readable book titles.

## How Japanese feedback/explanation English font is currently selected

At line ~7943: `const uint8_t enPx = 20;` — hardcoded 20px regardless of Settings.
`drawMixedJapaneseLabel()` is called with px=20 which → `applySansBoldFont(20)` → FreeSansBold9pt7b (tiny).

**Fix:** Add `japaneseModeEnglishPxForReader()` helper:
- Medium (Reader S): 24px → FreeSansBold12pt7b
- Large  (Reader M): 31px → FreeSansBold18pt7b
- XL     (Reader L): 31px → FreeSansBold18pt7b (safe fit even with all other large text)

Also fix `applyJapaneseEnglishLabelFont()` calls in Font Lab where 18px/20px is used
for descriptive text — those stay small since they are diagnostics, not user content.

## Japanese wrapping / kinsoku

`wrapJapaneseTextToLines()` correctly wraps by UTF-8 codepoint width (`display.textWidth()`).
Does NOT split multibyte sequences. Does NOT route Japanese through `sanitizeCoachText()`.

Missing: kinsoku (禁則処理) — forbidden-start and forbidden-end rules.

**Add** basic kinsoku to `wrapJapaneseTextToLines()`:
- Forbidden at line start: 、。」』）】！？ーっッ
  (move them back to the previous line by appending before checking width)
- Forbidden at line end: 「『（
  (push them to the next line)

Implementation: after tentative line split, check if the first char of the new line
is a forbidden-start character; if so, pop it back to the end of the previous line.
Do the same for forbidden-end: if the last char of the line is a forbidden-end,
delay it to the next line's start.

## External JP font experiment

`efontJA_24_b` is available in M5GFX headers (no extra build flag needed).
Include it in the Font Lab JP page 4 (font candidate comparison) to show it renders.
This is a real glyph comparison, not a stub.

No SD fonts, no TTF loading, no VLW conversion — built-in only.

## What this pass will NOT touch

- Deep-sleep touch wake (remains blocked)
- Dynamic frequency scaling (remains compile-time gated)
- Light sleep behavior (unchanged)
- BadgeSleepMode (unchanged)
- Japanese question content
- Interview content or rendering
- Badge screen
- EPUB renderer
- SQLite or SD source registry
