# Toolbar icons

PNG sources for the top toolbar (y=0..60 strip).

## Spec

- Filenames (lowercase, exact):
  - `sleep.png`   — left-side sleep button (taps trigger light-sleep
    until touch).
  - `menu.png`    — slot 1, reserved for a future "menu" view (no
    function yet, drawn for layout preview).
  - `setting.png` — slot 2, reserved for a future settings view (no
    function yet, drawn for layout preview).
  - `charge.png`  — right side, drawn just left of the battery
    percentage while the device is charging. Falls back to the
    " +" text suffix when this PNG is missing.
- Recommended size: **32 × 32 px**, square. The script keeps the
  PNG's native size — it never upscales. Anything larger than
  56 × 56 px (= toolbar height − margin) is downscaled to fit, with a
  console note.
- Pixel format: 8-bit grayscale or anything Pillow's `.convert("L")`
  can consume. RGBA / RGB is fine — alpha is composited onto white
  before flattening, so transparent backgrounds become white instead
  of black.

## How they get into firmware

`scripts/gen_icons.py` is a PlatformIO pre-build hook that emits one
packed 4 bpp `inline constexpr uint8_t` array per icon plus an
`ICON_TOOLBAR_<NAME>_PRESENT` flag into `src/library/icons_data.hpp`.
Missing PNGs leave the flag `false`, and `draw_toolbar()` in main.cpp
falls back to its text label ("⏻", or "OFF" while in standby).

The OFF-mode label always renders as text, since it acts as a status
indicator rather than a pressable button.

## Adding/changing icons

1. Drop the new PNG into this folder.
2. Re-run the build (`pio run -e paper_s3`). The pre-build hook
   regenerates `icons_data.hpp`; only sources that include it rebuild.
3. Removing a PNG flips `_PRESENT` back to `false` and the toolbar
   reverts to text for that slot.
