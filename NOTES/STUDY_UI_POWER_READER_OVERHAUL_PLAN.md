# Study UI Power Reader Overhaul Plan

Branch: `study-ui-power-reader-overhaul`

## Current state summary

### Japanese typography routing
- `japanesePromptPxForReader()`: Medium=32, XL=40, Large=36 ✓ correct
- `japaneseChoicePxForReader()`: Medium=32, XL=36, Large=36 ← XL should be 40
- `japaneseExplanationPxForReader()`: Medium=28, XL=36, Large=32 ← both too small
- `japaneseModeEnglishPxForReader()`: Medium=24, XL=31, Large=31 ← XL/Large too small
- `japaneseMetaPxForReader()`: Medium=24, XL=28, Large=24 ✓ correct

### Current feedback page model
- Page 0: "Correct/Wrong" + correct answer line + "Tap More for explanation." hint
- Page 1: answer sentence + JP explanation + EN explanation + grammar tag
- Problem: page 0 wastes an entire screen on only a hint

### Current mixed-script renderer behavior
- `drawMixedJapaneseLabel()`: if text contains Japanese → entire block renders in Gothic
- English explanations with JP examples → Gothic font → ugly English
- `wrapMixedJapaneseText()`: same problem — uses Gothic for the whole text if JP present
- EN explanation field is already special-cased with FreeSansBold, but JP chars still render as tofu

### Current Reader fallback behavior
- `renderLibrary()` shows "No TXT or MD books found." when SD has no books
- No embedded fallback documents exist
- Cannot test Reader without SD

### Current power policy behavior
- Responsive: WarmIdle@60s, LightSleep=disabled
- Balanced: WarmIdle@15s, LightSleep@10min
- Max Battery: WarmIdle@5s, LightSleep@5min
- Problem: 60s WarmIdle for Responsive is too aggressive; interactive menus start to downscale too soon

## Exact fixes to implement

### 1. Power policy (profileIdleScaleThresholdMs, profileLightSleepIdleMs)
- Responsive: WarmIdle@300s (5 min), LightSleep=disabled (unchanged)
- Balanced: WarmIdle@120s (2 min), LightSleep@300s (5 min)
- Max Battery: WarmIdle@60s (1 min), LightSleep@240s (4 min)

### 2. Japanese typography unification
Fix size helpers so all main-content roles are consistent:
- Reader L (XL): all body/choice/prompt/explanation = 40px; English = 32px
- Reader M (Large): all = 36px; English = 28px
- Reader S (Medium): all = 32px; English = 24px
Add public alias names: japaneseStudyBodyPx, japaneseStudyChoicePx, etc.

### 3. Feedback pagination rebuild
- Pre-measure all content blocks at current font sizes
- If all fit in available height: single page, Next = next question
- If not: page 0 = result + answer + JP sentence (+ part of JP explanation if it fits)
- Page 1 = remaining; page 0 footer shows "More" not just a hint

### 4. Mixed EN/JP renderer
- Add `splitIntoScriptRuns()`: splits UTF-8 text into runs of same script
- Add `drawMixedScriptLine()`: renders one line with per-run font switching
- Add `drawMixedScriptWrappedText()`: wraps and renders mixed-script paragraph
- Use in EN explanation field of Japanese feedback

### 5. Reader embedded samples
- Add `TxtReader::openFromString(title, content, charsPerLine, linesPerPage)`
- In ReaderApp: define kEmbeddedSamples[] with English + Japanese test text
- `renderLibrary()`: if SD has no books, show embedded samples with "Embedded sample" label
- `openBook()` / new `openEmbedded()`: open embedded sample by index

### 6. Font Lab external font tooling
- Create `tools/prepare_jp_fonts.sh` — downloads BIZ UDPGothic Bold, runs subsetting
- Create `tools/subset_jp_font.py` — pyftsubset wrapper for N3 codepoints
- Font Lab page 4: show BIZ status message with actionable tool path

### 7. Power Lab diagnostics
- Page 0 already shows: profile, WarmIdle ETA, LightNap ETA, CPU MHz
- Update after threshold changes so displayed values match new policy

### 8. Reader tap-zone navigation
- Left third (x < w/3) = prev page — already implemented in ReaderApp::handleTap
- Right third (x > 2w/3) = next page — already implemented
- Middle = buttons/menus only
- No change needed; verify footer buttons still work
