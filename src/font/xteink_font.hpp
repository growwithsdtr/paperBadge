#pragma once

#include <cstddef>
#include <cstdint>

namespace ps3::font {

// Read-only view over a 1 bpp glyph table. Two backing layouts are
// supported, picked at bind time:
//
// 1. Flat XTEink: the SD-loaded `.bin` (or its mmap'd flash partition
//    image). Layout per XTEinkFontBinary.cs:
//      - 65,536 glyphs covering Unicode BMP code points 0x0000..0xFFFF
//      - Each glyph is `ceil(W/8) * H` bytes, row-major, MSB-first
//        within byte; bit set = pixel drawn (foreground / dark)
//    The file does not encode its dimensions, so they must be supplied
//    at construction time and match the .bin actually used.
//
// 2. Sparse subset: a sorted codepoint table + parallel glyph blob in
//    the same packed format, used by the firmware-embedded fallback
//    (src/font/builtin_ui_font.{h,cpp}). Looked up via binary search,
//    so per-pixel cost is O(log N) instead of O(1) — ~12 comparisons
//    for the ~3,500-glyph BIZ UDGothic JIS-L1 subset, well below the
//    framebuffer write cost.
//
// Only one mode is active at a time; rebinding switches modes.
class XTEinkFont {
 public:
    XTEinkFont(int glyph_width, int glyph_height);

    // Bind to a flat, fully-populated 65,536-glyph blob. Returns false
    // if `size` is too small for that table at the configured
    // dimensions.
    bool bind(const uint8_t* data, size_t size);

    // Bind to a sparse codepoint+glyph pair. `codepoints` must be
    // sorted ascending and have `glyph_count` entries; `glyphs` must
    // hold `glyph_count * ceil(W/8) * H` bytes in the same MSB-first
    // packed layout as the flat blob, with the i-th glyph corresponding
    // to `codepoints[i]`. Returns false on a null pointer / zero count.
    bool bind_sparse(const uint16_t* codepoints, int glyph_count,
                     const uint8_t* glyphs);

    int  width()  const { return w_; }
    int  height() const { return h_; }
    bool ready()  const { return mode_ != Mode::None; }

    // Returns true if pixel (x, y) of the glyph for codepoint `cp` is
    // drawn. Out-of-range cp / x / y returns false. Codepoints not in
    // the sparse table also return false (caller can fall back to a
    // U+FFFD-style indicator if desired).
    bool pixel(uint32_t cp, int x, int y) const;

 private:
    enum class Mode { None, Flat, Sparse };

    int w_;
    int h_;
    int width_byte_;
    int char_byte_;
    Mode mode_ = Mode::None;

    // Flat mode
    const uint8_t* data_ = nullptr;

    // Sparse mode
    const uint16_t* sparse_cp_     = nullptr;
    int             sparse_count_  = 0;
    const uint8_t*  sparse_glyphs_ = nullptr;
};

}  // namespace ps3::font
