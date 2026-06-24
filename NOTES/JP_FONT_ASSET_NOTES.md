# Japanese Font Asset Notes

Branch: `hardware-japanese-final-polish`

## Built-in Japanese fonts available right now

M5GFX ships these lgfxJapanGothic bitmap fonts as part of its library headers:

| Font | Size (px) | Notes |
|------|-----------|-------|
| `lgfxJapanGothic_20` | 20 | Too small for user-facing Japanese — debug/diagnostics only |
| `lgfxJapanGothic_24` | 24 | Minimum readable size; used for meta/tags/labels |
| `lgfxJapanGothic_28` | 28 | Readable for compact screens |
| `lgfxJapanGothic_32` | 32 | Body text at Reader S |
| `lgfxJapanGothic_36` | 36 | Body text at Reader M, choices at Reader L |
| `lgfxJapanGothic_40` | 40 | Body text at Reader L |

These are selected by `applyGothicFont(px)` in `main.cpp`.

All are embedded in M5GFX and do not require SD or external files.
Built-in Gothic is **the current fallback for all Japanese rendering**.

## efontJA_24_b

M5GFX includes `efontJA_24_b` (24px bold embedded bitmap font) as part of the
`efont` family. To enable it:

```cpp
#include <M5GFX.h>
// No extra build_flags needed — it's in the M5GFX headers.
display.setFont(&fonts::efontJA_24_b);
```

**Usefulness:** Only 24px, which is the minimum tag size. It produces a bold,
slightly condensed look vs. lgfxJapanGothic_24. Useful for small metadata labels
(N3, W1D1, grammar tags) but **cannot serve as body text** because it does not
scale above 24px.

**Current decision:** Not embedded in this build. lgfxJapanGothic_24 serves the
same role at 24px with slightly more spacing. Add only if the bold density of
efontJA is visually preferred for tags.

## External font candidates

The following external fonts are candidates for future embedding as VLW files.
None are currently embedded in the firmware.

### BIZ UDPGothic Bold (recommended first choice)
- **License:** SIL OFL 1.1 — free to embed in firmware
- **Source:** Google Fonts `Bizier UDPGothic` or Adobe Fonts
- **Why:** Designed for clarity at small sizes on screen/print; JP JIS standard
- **Weight:** Regular + Bold available
- **Conversion route:** subset with pyftsubset → loadFont VLW or LGFX VLW conversion

### Noto Sans JP Bold
- **License:** SIL OFL 1.1 — free to embed
- **Source:** Google Fonts
- **Why:** Extensive glyph coverage, highly readable
- **Concern:** Full font is very large (~5 MB); must subset aggressively to N3 vocab

### Source Han Sans JP Bold (Adobe)
- **License:** SIL OFL 1.1
- **Source:** GitHub `adobe-fonts/source-han-sans`
- **Why:** Professional quality, excellent screen rendering
- **Concern:** Same subsetting requirement as Noto Sans JP

### M PLUS 1p
- **License:** SIL OFL 1.1
- **Source:** Google Fonts
- **Why:** Friendly, readable; good for UI text
- **Concern:** Less tested on e-paper than BIZ UD

## Recommended future route for embedding an external JP font

1. Subset the font to only the codepoints needed:
   - N3/N4 vocabulary kanji (~1000-2000 codepoints)
   - All Hiragana (U+3040-U+309F)
   - All Katakana (U+30A0-U+30FF)
   - Basic ASCII + punctuation

2. Use `pyftsubset` (from the `fonttools` package):
   ```bash
   pip install fonttools brotli
   python tools/subset_jp_font.py
   ```

3. Convert to LGFX/VLW format or use LGFX's `Font_src` with TTF support
   (M5GFX supports loading TTF from SPIFFS/SD via LovyanGFX).

4. Place subset file on SD at `/paperbadge/fonts/jp_body.vlw` or similar.

5. Load with `display.loadFont(kJpBodyFontPath)` on demand.

**Guardrail:** The total embedded firmware must stay under 12 MB. A properly
subsetted N3 font should be under 200 KB.

## What was NOT implemented

- No external font was fetched, subset, converted, or embedded in this pass.
- No VLW conversion chain was validated or added as tooling.
- `tools/prepare_jp_fonts.sh` and `tools/subset_jp_font.py` were not created because
  the full conversion chain (pyftsubset → VLW) was not validated to be clean.
- Font embedding was not blocked on or required; built-in Gothic is fully functional.
