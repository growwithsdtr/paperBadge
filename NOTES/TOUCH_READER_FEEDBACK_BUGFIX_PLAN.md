# Touch / Reader / Feedback Bugfix Plan

Branch: `touch-reader-feedback-bugfix-pass`

## Root causes of missed taps

### Primary cause: over-long debounce windows
After every clean refresh, `gInputLocked = true` for **600 ms** (`kInputCleanRefreshDebounceMs`).
After every fast refresh, locked for **250 ms** (`kInputDebounceMs`).

All interactive screen transitions use clean refresh (highQuality=true). The e-paper panel itself
takes 1–2 seconds to complete a clean waveform. `finishDisplayRefresh()` is called AFTER the
panel settles, then the 600 ms debounce starts. Total dead time: 1.5–2.6 s.

If the user taps during the 600 ms debounce window (i.e., within 600 ms of the screen finishing
its update), the tap is silently dropped and logged as "input locked". Because the user just
navigated somewhere and the screen just finished, this is "right after navigation". The perception
is that the **first post-navigation tap** often does not register.

Fix: reduce `kInputDebounceMs` 250→50 ms, `kInputCleanRefreshDebounceMs` 600→150 ms.

### Secondary cause: CPU scaling on interactive screens in Responsive profile
After 60 s idle, WarmIdle enters even in Responsive mode (`gPowerProfile == PowerProfile::Balanced`).
CPU scales to 80 MHz in `maybeScaleIdleCpu()`. At 80 MHz, the loop still runs at 50 ms
but the system is in a "degraded state" perception-wise; also any timing-sensitive I2C poll
may see slightly longer service times. More importantly, **the WarmIdle state itself signals an
error in product policy**: Responsive mode should never scale CPU while on an interactive screen.

Fix: `maybeScaleIdleCpu()` returns immediately if `gPowerProfile == PowerProfile::Balanced`.
Responsive profile → CPU stays at 240 MHz always.

### No missed taps from loop delay
`loopDelayMs()` now returns 50 ms in Responsive idle mode (fixed last pass). This is correct.
`M5.update()` called every 50 ms is sufficient for GT911 tap detection. Not the root cause.

### No missed taps from WarmIdle threshold directly
With threshold at 60 s, casual use won't enter WarmIdle. Not the primary cause.

## Reader still tiny and pale — exact render paths

`ReaderApp::render()` (line 64) sets `display.setTextFont(2); display.setTextSize(1)` globally
before calling any sub-render. That's the GLCD 6×8 pixel bitmap font at scale 1 = 6 px tall.

`renderLibrary()`:
- Title "Reader": `setTextFont(4)` — larger bitmap (OK but not FreeSansBold)
- Book titles: `setTextFont(2)` — 6 px GLCD font → **UNREADABLE**
- Metadata/path: `setTextFont(2)`, muted gray → **UNREADABLE AND PALE**
- Empty state messages: `setTextFont(2)` → **UNREADABLE**
- Footer buttons: `renderFooterButton()` → `setTextFont(2)` → **UNREADABLE**

`renderReading()`:
- Header (book title, page/offset status): `setTextFont(2)` → tiny
- Body text: `applyBodyFont()` → FreeSansBold12/18/24pt7b ← **correct (from last pass)**
- Page hint "Tap left/right zones": `setTextFont(2)`, muted gray → tiny (acceptable for hint)
- Footer buttons: `renderFooterButton()` → `setTextFont(2)` → tiny

`renderMessage()`:
- Title "Reader": `setTextFont(4)` (OK)
- Message text: `setTextFont(2)` → **UNREADABLE**
- Hint: `setTextFont(2)`, muted → tiny

`renderFooterButton()`: always `setTextFont(2)` → tiny labels

**Pale text**: `renderReader()` in main.cpp uses `prepareFullRefresh(refreshReason, false)` →
always fast refresh for ALL reader views including entry. Fast refresh leaves ghosting from previous
screen, making text appear pale/washed out. Fix: use `highQuality=true` for "reader entry" refresh.

## Japanese feedback/explanation render paths — cut-off and mixed font routing

**Cut-off**: At Reader L, feedback layout:
- header: 28px → 44px lineH
- promptY ≈ 86
- "Correct/Wrong" label: 36px
- correct answer: up to 2 lines × 52px = 104px
- answer sentence: up to 3 lines × 52px = 156px
- JP explanation: up to 3 lines × 52px = 156px
- EN explanation: up to 3 lines × 46px = 138px
Total: 86+36+12+104+12+156+10+156+10+138+8 = **728 px** on a 540 px tall display.

The footer is at `display.height() - 110 = 430 px`. Content overflows behind footer and off-screen.
No overflow guard exists. Content is drawn but not visible.

**Fix**: Paginate feedback into 2 pages:
- Page 0: "Correct/Wrong" + correct answer line (stops here; shows "More" instead of "Next")
- Page 1: answer sentence + JP explanation + EN explanation + grammar tag

Add `gJapaneseFeedbackPage` global (int8_t). Reset to 0 on question entry and on answer selection.
Modify touch handler: on feedback page 0, "Next" → `gJapaneseFeedbackPage = 1` (re-render).
On feedback page 1, "Next" → advance to next question.

**Mixed font routing (English rendered in Gothic)**:
`drawMixedJapaneseLabel()` checks `containsJapaneseCodepoint(text)`. If true → uses Gothic for
the **entire** text block, including English prose. The English explanation for Q003 is:
  "'Chigau' means to differ/be wrong. '違っていました' is its past-tense form."
→ contains Japanese → whole line uses Gothic → English in Japanese font.

Fix: for `item.explanationEnglish`, always use FreeSansBold (`japaneseModeEnglishPxForReader()`),
even when the explanation contains Japanese examples. Japanese examples will render as fallback
glyphs. English readability is the priority. Use `wrapMixedJapaneseText()` for correct character
width measurement, but render with FreeSansBold active.

## What this pass will NOT touch
- Deep sleep touch wake
- SD content registry
- EPUB rendering
- Interview render paths / applyTypographyFont / applySansBoldFont
- Japanese question content
- LightNap policy (Responsive already disables it via `profileAllowsLightSleep()`)
