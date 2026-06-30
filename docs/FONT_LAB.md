# Font Lab

On-device:

- Settings -> Fonts -> Font Lab.
- Normal firmware now embeds Font Lab bitmap subsets in `src/font/font_lab_assets.*`; no SD font copy is required.
- Production runtime fonts remain BIZ UDGothic Regular and IPAex Gothic until a full production-safe face is chosen.
- Font Lab pages include index/comparison pages plus one page per candidate with 24 px native and 48 px scaled samples.

Embedded Font Lab candidates:

- BIZ UDPGothic Regular / Bold
- Noto Sans JP Regular / Medium / Bold
- M PLUS 1p Regular / Bold
- M PLUS Rounded 1c Regular / Bold
- Inter Regular
- Source Serif 4 Regular

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
