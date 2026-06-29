# Bookshelf menu icons

PNG sources for the 4-slot top menu (上へ / 左 / 右 / サムネ).

## Spec

- Filenames (lowercase, exact):
  - `folderup.png` — "go up one folder" button
  - `prev.png`     — page-prev button (currently hidden, reserved)
  - `next.png`     — page-next button (currently hidden, reserved)
  - `thumb.png`    — "regenerate thumbnails" button
- Recommended size: **64 × 64 px**, square. The script keeps the PNG's
  native size — it never upscales (LANCZOS upscaling on a 4 bpp
  e-paper render looks washed out). Anything larger than 128 × 100 px
  is downscaled to fit the menu strip, with a console note.
- Pixel format: 8-bit grayscale or anything Pillow's `.convert("L")` can
  consume (RGBA/RGB are fine — alpha is dropped, transparent pixels
  become whatever Pillow flattens to). The build script downsamples to
  4 bpp grayscale (16 levels), packed two pixels per byte.
- Background: white (matches the e-paper). Foreground: black/gray.
  There is no transparency in the on-device format.

## How they get into firmware

`scripts/gen_icons.py` runs as a PlatformIO pre-build step. For each
PNG it finds, it emits a packed 4 bpp `inline constexpr uint8_t` array
plus a `bool ICON_X_PRESENT` flag into `src/library/icons_data.hpp`.
Missing PNGs leave the flag `false`, and `library_view::render_menu()`
falls back to its text label ("上へ" / "サムネ" / etc.).

## Adding/changing icons

1. Drop the new PNG into this folder.
2. Re-run the build (`pio run -e paper_s3`). The pre-build hook
   regenerates `src/library/icons_data.hpp`; only sources that include
   that header rebuild.
3. If you removed a PNG, `_PRESENT` flips back to `false` and the menu
   reverts to text for that slot.
