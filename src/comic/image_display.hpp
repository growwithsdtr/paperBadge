#pragma once

#include <cstddef>
#include <cstdint>

#include "../system/settings.hpp"  // for ContrastContext

namespace ps3::comic {

// Decode a JPEG image from memory and blit it onto a 4 bpp packed
// framebuffer in panel-native orientation (epd_width()/epd_height()).
// Image is scaled to fit and centered in the screen logical
// resolution.
//
// `dest_fb`: if non-null, write into this buffer (caller-owned, must
//   be at least `epd_width() * epd_height() / 2` bytes). If null,
//   write into the active epdiy framebuffer.
//
// Does NOT call display::clear() or display::flush(); the caller is
// expected to compose the page and trigger the panel update. (When
// dest_fb is non-null the caller is also responsible for clearing it
// to white before calling — display_jpeg only writes pixels inside
// the image rect.)
//
// `ctx` selects which contrast curve to apply during 8→4-bit pack.
// ContrastContext::Off (default) skips the curve entirely; callers
// that paint user content (comic pages = Reading) opt in by passing
// the appropriate context so the same image stays consistent
// regardless of which call site triggered the decode.
//
// Returns true on success.
bool display_jpeg(const uint8_t* data, size_t size,
                  uint8_t* dest_fb = nullptr,
                  ps3::settings::ContrastContext ctx
                      = ps3::settings::ContrastContext::Off);

// PNG counterpart of display_jpeg(). Same blit conventions: writes
// directly into the 4 bpp packed framebuffer in panel-native
// orientation, centers the image inside the logical screen, leaves
// the surrounding pixels untouched (caller is expected to clear).
// Pixels are converted from PNG via PNGdec::getLineAsRGB565() and
// reduced to 4-bit grayscale using BT.601 luma weights.
bool display_png(const uint8_t* data, size_t size,
                 uint8_t* dest_fb = nullptr,
                 ps3::settings::ContrastContext ctx
                     = ps3::settings::ContrastContext::Off);

// BMP counterpart. Supports the formats that mainstream paint tools
// emit out of the box: 24 bpp BGR, 32 bpp BGRA (alpha discarded),
// and 8 bpp paletted (greyscale or colour palette). 1 / 4 bpp are
// not handled — callers expected to convert before placing the
// file. Same centering / no-clear semantics as display_jpeg.
bool display_bmp(const uint8_t* data, size_t size,
                 uint8_t* dest_fb = nullptr,
                 ps3::settings::ContrastContext ctx
                     = ps3::settings::ContrastContext::Off);

// Read the native pixel dimensions of a PNG without running a full
// decode. Parses just the PNG signature and IHDR chunk header; no
// allocations, no zlib. Returns false on a malformed or truncated
// stream.
bool png_size(const uint8_t* data, size_t size, int* out_w, int* out_h);

// Like display_png() but paints the image with its top-left corner
// at (dst_x, dst_y) instead of centering it. Used by the loading
// dialog to place the splash inside a bordered popup. Same blit
// rules as display_png (4 bpp pack, rotation-aware, no clearing).
bool display_png_at(const uint8_t* data, size_t size,
                    int dst_x, int dst_y,
                    uint8_t* dest_fb = nullptr,
                    ps3::settings::ContrastContext ctx
                        = ps3::settings::ContrastContext::Off);

}  // namespace ps3::comic
