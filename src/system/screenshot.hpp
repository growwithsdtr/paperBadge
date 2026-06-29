#pragma once

#include <cstddef>

namespace ps3::screenshot {

// Captures the active 4 bpp framebuffer to a PNG file under
// /sdcard/screenshot/. The file is named `screenshot_NNNN.png` with
// NNNN being the lowest 0-padded 4-digit number not currently taken
// in that directory; an empty directory starts at 0001.
//
// The PNG is written as 4-bit grayscale (color type 0, bit depth 4)
// using zlib (via miniz) for IDAT compression. Logical 540 x 960
// orientation is preserved — the saved image looks like what's on
// screen, regardless of the runtime rotation toggle.
//
// On success, writes the chosen filename (basename only, e.g.
// "screenshot_0042.png") into `out_name` (sized via `out_name_size`)
// and returns true. On failure (SD not mounted, mkdir failed,
// fopen failed, miniz error, etc.) returns false and `out_name` is
// left untouched.
bool capture_to_sd(char* out_name, std::size_t out_name_size);

}  // namespace ps3::screenshot
