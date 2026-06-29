#pragma once

#include <cstdint>

namespace ps3::display {

// e-paper refresh waveform selection. Trade-off: lower latency, more ghosting.
enum class RefreshMode {
    GC16,      // 16-level grayscale, full ghost removal. Slowest, best
               //   quality. epdiy's HL layer skips columns whose pixels
               //   match the previous frame, so this is effectively a
               //   diff update — fast, but lets ageing diverge between
               //   "frequently-touched" and "always-white" columns.
    GC16Full,  // GC16 with the diff cache invalidated, so the waveform
               //   actually flows over the whole panel. Same wall time
               //   as GC16, but resets the per-column ageing drift
               //   that builds up over many partial updates. Use on
               //   the periodic full-refresh tick and at major scene
               //   transitions (startup, book close).
    GL16,      // 16-level grayscale, light update. Faster, mild ghosting.
    DU,        // 1-bit direct update. Fastest, no grayscale (text-only friendly).
};

// Initialise epdiy with the Paper S3 board definition. Allocates the
// epdiy framebuffer in PSRAM and powers the panel on.
bool init();

// Logical (rotated) resolution. After init() these report the orientation
// returned by epdiy after epd_set_rotation().
int width();
int height();

// Pointer to the epdiy 4 bpp framebuffer. Pixel layout: 2 pixels per byte,
// low nibble = even x, high nibble = odd x. White = 0xF, black = 0x0.
uint8_t* framebuffer();

// Set every pixel to white in the framebuffer (no panel update).
void clear();

// Push the current framebuffer to the panel using the given waveform.
// Blocks until the e-paper refresh completes.
void flush(RefreshMode mode = RefreshMode::GC16);

// Push only the rectangular area (x, y, w, h) — useful for popup
// dialogs and other small overlays where a full-screen update would
// be visually noisy. Coordinates are in logical (rotated) screen
// pixels. GC16Full isn't supported here (it's full-screen by
// design); GC16 / GL16 / DU all behave like flush() restricted to
// the given rect.
void flush_area(int x, int y, int w, int h,
                RefreshMode mode = RefreshMode::GC16);

// Draw a single pixel into the framebuffer. color is a 4-bit grayscale
// value in [0, 15]; 0 = black, 15 = white. Out-of-range coords are
// silently ignored.
void put_pixel(int x, int y, uint8_t color);

// Toggle the panel orientation by 180° at runtime.
//   inverted = false : EPD_ROT_INVERTED_PORTRAIT (the default)
//   inverted = true  : EPD_ROT_PORTRAIT (flipped 180°)
// Both rotations produce the same 540×960 portrait resolution, so
// width()/height() are unchanged; only "which edge is up" flips.
// The caller is responsible for repainting after the call, and for
// telling the touch driver about the same flip via
// ps3::touch::set_inverted().
void set_inverted(bool inverted);

// Current rotation flag (matches the most recent set_inverted call,
// false at boot). Direct framebuffer blitters that bypass
// epd_draw_pixel — display_jpeg, blit_thumbnail — read this to pick
// the right logical→native coordinate transform.
bool is_inverted();

}  // namespace ps3::display
