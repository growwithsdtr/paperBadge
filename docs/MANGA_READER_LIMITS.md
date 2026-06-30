# Manga Reader Limits

Firmware path:

- Supported archive target: CBZ/ZIP with JPEG pages.
- CBR/RAR: not linked into firmware; convert with the Mac preprocessor or external tools.
- ZIP64: not implemented in firmware miniz path; large archives fail safely with diagnostics.
- PNG/WebP in CBZ: explicitly diagnosed as unsupported for firmware reading in this sprint.
- Fullscreen reading is active; page header overlay was removed.
- Fit/orientation controls are scaffolded and logged, but true fit-width/fit-height crop/slice rendering still needs decoder work.

Host preprocessing:

```sh
tools/manga_preprocess.py book.cbz --out out/book --max-mb 45 --zip64 never --downscale portrait
tools/manga_preprocess.py book.cbz --out out/book_slices --downscale landscape-slices --slices 4 --grayscale16
```

Outputs:

- smaller CBZ chunks
- `manifest.json`
- `page_index.json`
- optimized image folder
- optional `ocr/` sidecars when `--ocr gemini` is used
