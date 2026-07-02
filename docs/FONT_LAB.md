# Font Lab

On-device path:

- Settings -> Fonts -> Font Lab.
- Normal firmware embeds Font Lab bitmap subsets in `src/font/font_lab_assets.*`; no SD font copy is required.
- Production runtime fonts remain BIZ UDGothic Regular and IPAex Gothic.
- Font Lab candidates are QA-only preview subsets. Selecting or viewing them does not change Reader, Interview, Japanese, Badge, Manga, or Settings runtime text.
- Settings labels this feature as `Lab preview` so it is not confused with a live Western/runtime font picker.

## Runtime Model

The ESP32 firmware does not render runtime TTF, OTF, or variable fonts. Font Lab candidates are produced on the Mac by rasterizing source outline fonts into fixed bitmap glyph cells, then embedding sparse firmware arrays.

Current embedded candidate preview sizes:

- 24 px native bitmap cells.
- 48 px preview by 2x scaling the 24 px bitmap.
- 20, 28, 32, 36, and 40 px candidate previews are not embedded yet. They would require regenerated fixed bitmap assets, not a variable-size runtime slider.
- There is no runtime variable font rendering, no TTF/OTF parser, and no runtime outline rasterizer in the ESP32 firmware.

Production runtime fonts:

| Font | Runtime use | Japanese | Latin | Weight |
|---|---|---:|---:|---|
| BIZ UDGothic | Settings-selectable production face | Yes | Yes | Regular |
| IPAex Gothic | Settings-selectable production face | Yes | Yes | Regular |

Font Lab QA preview candidates:

| Candidate | Runtime use | Japanese subset | Latin subset | Weight |
|---|---|---:|---:|---|
| BIZ UDPGothic | QA preview only | Yes | Yes | Regular |
| BIZ UDPGothic | QA preview only | Yes | Yes | Bold |
| Noto Sans JP | QA preview only | Yes | Yes | Regular |
| Noto Sans JP | QA preview only | Yes | Yes | Medium |
| Noto Sans JP | QA preview only | Yes | Yes | Bold |
| M PLUS 1p | QA preview only | Yes | Yes | Regular |
| M PLUS 1p | QA preview only | Yes | Yes | Bold |
| M PLUS Rounded 1c | QA preview only | Yes | Yes | Regular |
| M PLUS Rounded 1c | QA preview only | Yes | Yes | Bold |
| Inter | QA preview only | No, Latin only in this subset | Yes | Regular |
| Source Serif 4 | QA preview only | No, Latin only in this subset | Yes | Regular |

The on-device Font Lab flow is intentionally spacious:

- Page 1 explains production versus QA preview fonts.
- Page 2 shows the production runtime faces only.
- Page 3 is the candidate index.
- Later pages show one candidate per page with 24 px native and 48 px scaled samples.
- Latin-only candidates are labeled Latin only and do not show Japanese tofu-box rows.
- Preview samples draw inside taller fixed cells with conservative tracking so 24 px and 48 px samples do not clip into labels, neighboring lines, or the footer.

## Promotion Checklist

Promoting a candidate to a production runtime font is a content and QA task, not just a Settings toggle. Required work:

- Generate a production-scale glyph subset comparable to the BIZ/IPA coverage, not the small Font Lab physical-QA subset.
- Add the generated bitmap arrays as a production `XTEinkFont` asset.
- Add a real font-face enum entry and Settings option.
- Verify Japanese Practice, Mock, Reference, Interview, Reader, Badge, and Settings screens for coverage, line height, and wrapping.
- Keep `firmware.bin` below the 12 MB app limit.

Host tool:

```sh
tools/font_candidates.py --help
tools/font_candidates.py --download
tools/font_candidates.py --download --subset --emit-firmware
```

Notes:

- Downloads go to ignored `generated-assets/fontlab/source`.
- License/metadata files and `fontlab_manifest.json` are written under `generated-assets/fontlab`.
- Outline subsetting uses `fonttools` when installed; firmware embedding only requires Pillow.
- The firmware asset subset covers the Font Lab physical-QA sample glyphs, not a complete Japanese runtime font.
