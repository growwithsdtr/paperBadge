# Study UI Power Reader Overhaul Summary

Branch: `study-ui-power-reader-overhaul`
Commit: `6fc7ef0`
Build: SUCCESS — Flash 69.5% (4.55 MB / 6.55 MB), RAM 71.1%

---

## Power policy

| Profile | WarmIdle | LightSleep |
|---------|----------|-----------|
| Responsive | 5 min (was 60s) | disabled |
| Balanced | 2 min (was 15s) | 5 min (was 10min) |
| Max Battery | 1 min (was 5s) | 4 min (was 5min) |

No deep-sleep, no experimental DFS. CPU restored before touch processing.
Power Lab page 0 shows live: profile, WarmIdle ETA, LightNap ETA, CPU MHz.

## Japanese typography

All main study content roles unified to the same size family per Reader setting:

| Setting | Body / Prompt / Choice / Explanation | EN Explanation | Meta / Tag |
|---------|--------------------------------------|----------------|------------|
| Reader L (XL) | 40px | 32px | 28px / 24px |
| Reader M (Large) | 36px | 28px | 24px / 24px |
| Reader S (Medium) | 32px | 24px | 24px / 24px |

Named aliases: `japaneseStudyBodyPx`, `japaneseStudyChoicePx`, `japaneseStudyExplanationPx`,
`japaneseStudyMetaPx`, `japaneseStudyEnglishPx`, `japaneseStudyFooterPx`.

## Feedback pagination behavior

Content-aware algorithm:
1. Pre-measures all blocks (result, answer, JP sentence, JP explanation, EN explanation, grammar tag)
2. If all fit on one screen → single page, footer shows Next → next question
3. If not → page 0 shows result + answer + answer sentence + as much JP explanation as fits;
   page 1 shows EN explanation + grammar tag
4. Page 0 never renders as "Correct/Wrong + Tap More" with nothing else

## Mixed-script renderer behavior

`drawMixedScriptWrappedText(text, ...)`:
- Calls `splitIntoScriptRuns()` → detects script per UTF-8 codepoint
- English/ASCII runs → `applySansBoldFont()` (FreeSansBold)
- Japanese runs (U+3040+) → `applyGothicFont()` (lgfxJapanGothic)
- Word-level wrapping for English; character-level for Japanese
- Used in EN explanation field of Japanese feedback

Result: Q003 "違っていました" renders in Gothic; surrounding English in FreeSansBold. No tofu.

## Reader embedded sample behavior

When SD has no books, Library shows:
- Header: "Embedded sample documents / SD not mounted or no books found."
- Row: "Sample EN — Metro Study" (English, ~400 words)
- Row: "Sample JP — N3 Vocab" (Japanese, hiragana/kanji N3 vocabulary)

Opening either sample uses `TxtReader::openFromString()` — no SD required.
Full Reader features work: font size, page turns, tap-zone navigation, footer buttons.
State is not persisted (SD unavailable), but page turns work within session.

## Reader tap-zone navigation

Already implemented in prior pass. Left third → prev page, right third → next page.
Footer buttons also work. No change needed.

## External JP font status

Not embedded in this pass. Built-in `lgfxJapanGothic` (20-40px) remains the font for all JP rendering.

Tooling created:
- `tools/prepare_jp_fonts.sh` — downloads BIZ UDPGothic Bold (SIL OFL), calls subset script
- `tools/subset_jp_font.py` — pyftsubset wrapper for N3 codepoints

Font Lab page 4 now shows: "External JP font: not embedded. Run: tools/prepare_jp_fonts.sh then rebuild."

Conversion path (subset TTF → firmware array) is not yet implemented. See `NOTES/JP_FONT_ASSET_NOTES.md`.

## Remaining risks

1. **Mixed-script wrapping edge cases**: The word-level tokenizer may break at unexpected places
   for very long Japanese runs not separated by spaces. Visual QA required.
2. **Feedback single-page accuracy**: Block height pre-measurement uses conservative line limits (4 per block).
   Edge cases with very long explanation text may still overflow; test with verbose Q items.
3. **Embedded sample JP text**: If the JP sample has kinsoku violations at some font sizes,
   wrapping may look unusual. Test at all three Reader sizes.
4. **WarmIdle at 5 min**: For Max Battery users this may be too long. Monitor battery life feedback.

## Physical QA checklist

### Power
- [ ] Responsive: use device 2 min, all taps instant, no CPU downscale in serial
- [ ] Responsive: idle 5 min on Home, one tap — wakes and acts without repeated taps
- [ ] Power Lab page 0: WarmIdle shows correct "X left" countdown

### Japanese typography
- [ ] Reader L: Q prompt and 4 choices are consistent 40px
- [ ] Reader L: feedback first screen shows result + answer + explanation content
- [ ] Q003: EN explanation no tofu for 違っていました
- [ ] Q004: EN explanation no tofu for 引っ越した
- [ ] EN words in FreeSansBold; JP examples in Gothic on same screen

### Reader
- [ ] No SD: library shows "Embedded sample documents"
- [ ] Open English sample: readable text, multiple pages at L
- [ ] Open JP sample: kana/kanji readable, wrapping correct
- [ ] Tap right third: next page; tap left third: prev page
- [ ] Footer Library/Home buttons return to previous screens

### Font Lab
- [ ] Page 4: shows "not embedded" + script path

### Interview regression
- [ ] Practice, Drills, Exam all render and respond to taps
