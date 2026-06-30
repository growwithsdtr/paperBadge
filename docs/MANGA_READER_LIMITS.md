# Manga Reader Limits

Firmware path:

- Supported archive target: CBZ/ZIP with JPEG pages; PNG pages are also accepted and rendered through PNGdec.
- Non-ZIP64 CBZ files over 50 MB are no longer rejected just because of size.
- ZIP64: detected by ZIP64 end-of-central-directory markers and rejected with a specific diagnostic.
- CBR/RAR: not linked into firmware; convert with the Mac preprocessor or external tools.
- WebP in CBZ: not implemented in firmware.
- Fullscreen reading is active; top tap returns to library.
- Portrait mode defaults to fit-page.
- Manual landscape now uses epdiy landscape rotation and touch remapping.
- Landscape entry from fit-page switches to fit-width by default.
- JPEG fit-page / fit-width / fit-height render through the decoder viewport path.
- JPEG landscape slice paging shows vertical sections of a fit-width page before advancing to the next page.
- PNG pages currently render fit-page only; PNG slice/fit-width is a follow-up.
- IMU auto-rotation is not enabled: the old M5Unified app disabled internal IMU, and this ESP-IDF tree has no BMI270 driver/config yet.

Host preprocessing:

```sh
tools/manga_preprocess.py book.cbz --out out/book --max-mb 150 --zip64 never --downscale portrait
tools/manga_preprocess.py book.cbz --out out/book_slices --downscale landscape-slices --slices 4 --grayscale16
tools/manga_preprocess.py book.cbz --out out/book_ocr --ocr gemini --ocr-translate
```

Outputs:

- non-ZIP64 CBZ chunks
- `manifest.json`
- `page_index.json`
- optimized image folder
- optional `ocr/` sidecars when `--ocr gemini` is used

Known limitations:

- ZIP64 remains unsupported in firmware.
- CBR/RAR remains host-convert only.
- WebP remains unsupported in firmware.
- PNG support is fit-page only in this sprint.
- JPEG scaling uses nearest-neighbor viewport mapping from JPEGDEC output; it is functional, not a high-quality resampler.
