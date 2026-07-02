# Manga Reader Limits

Firmware path:

- Supported archive target: CBZ/ZIP with JPEG pages; PNG pages are also accepted and rendered through PNGdec.
- Non-ZIP64 CBZ files over 50 MB are no longer rejected just because of size.
- ZIP64: detected by ZIP64 end-of-central-directory markers and rejected with a specific diagnostic.
- CBR/RAR: not linked into firmware; convert with the Mac preprocessor or external tools.
- WebP in CBZ: not implemented in firmware. WebP-only archives now report that no JPEG/PNG pages are available instead of failing generically.
- Fullscreen reading is active; top tap returns to library and center tap opens the manga overlay.
- Portrait mode defaults to fit-page.
- Manual landscape now uses epdiy landscape rotation and touch remapping.
- Landscape entry from fit-page switches to fit-width by default.
- JPEG fit-page / fit-width / fit-height render through the decoder viewport path.
- JPEG landscape slice paging shows vertical sections of a fit-width page before advancing to the next page.
- PNG pages now use the same fit/slice viewport path as JPEG pages, subject to PNGdec row-buffer limits.
- IMU auto-rotation is not enabled. This branch adds a probe that logs reads at candidate BMI270 I2C addresses `0x68` and `0x69`; a real driver/autorotate mode should wait for physical serial confirmation.
- CBZ page order uses natural numeric sorting, so `page10.jpg` follows `page9.jpg`; hidden dotfiles and `__MACOSX` entries are ignored.

Touch map:

- Top strip: return to Manga Library.
- Center body: open/close the overlay menu.
- Portrait body: left third = previous page/slice, right third = next page/slice, honoring the right-binding setting.
- Landscape body: left/right regions step backward/forward through slices before crossing page boundaries; moving backward from slice 1 lands on the previous page's last slice.
- Overlay buttons: Fit, Orientation, Refresh profile, Clean now, Library, Close.
- First-use manga hints appear for the first few page renders after opening a book.

Error messages:

- ZIP64, CBR/RAR, corrupt/unreadable ZIP, allocation failure, no displayable JPEG/PNG pages, and WebP-only/unsupported-image archives now get distinct user-facing diagnostics.
- Decode/render failures report page number, slice number, fit mode, orientation, and whether the failure was a cache hit or fetch path in serial logs.

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
- JPEG scaling uses nearest-neighbor viewport mapping from JPEGDEC output; it is functional, not a high-quality resampler.
- IMU auto-rotation remains manual/pending until the probe identifies the exact device and address on hardware.
