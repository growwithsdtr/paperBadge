# Font Lab

On-device:

- Settings -> Fonts -> Font Lab.
- Pages cover embedded BIZ UDGothic and IPAex Gothic, western samples, Japanese samples, size profiles, and candidate pipeline notes.
- Normal firmware embeds only current small XTEink subsets to protect flash size.

Host tool:

```sh
tools/font_candidates.py --help
tools/font_candidates.py --download
tools/font_candidates.py --download --subset
```

Candidate workflow:

- Downloads open-license candidates to `generated-assets/fontlab/source`.
- Optional `fonttools` subset output goes to `generated-assets/fontlab/subset`.
- Writes `fontlab_manifest.json`.
- Verify upstream license files before embedding candidates in normal firmware.

Current embedded candidates:

- BIZ UDGothic Regular
- IPAex Gothic

Scaffolded candidates:

- Noto Sans JP
- BIZ UDPGothic
- M PLUS 1p
- M PLUS Rounded 1c
- efontJA and Western serif/sans candidates for a separate FontLab build
