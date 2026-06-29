#include "xteink_font.hpp"

namespace ps3::font {

XTEinkFont::XTEinkFont(int glyph_width, int glyph_height)
    : w_(glyph_width),
      h_(glyph_height),
      width_byte_((glyph_width + 7) / 8),
      char_byte_(((glyph_width + 7) / 8) * glyph_height) {}

bool XTEinkFont::bind(const uint8_t* data, size_t size) {
    const size_t required = static_cast<size_t>(char_byte_) * 0x10000u;
    if (data == nullptr || size < required) {
        return false;
    }
    data_          = data;
    sparse_cp_     = nullptr;
    sparse_glyphs_ = nullptr;
    sparse_count_  = 0;
    mode_          = Mode::Flat;
    return true;
}

bool XTEinkFont::bind_sparse(const uint16_t* codepoints, int glyph_count,
                             const uint8_t* glyphs) {
    if (!codepoints || !glyphs || glyph_count <= 0) {
        return false;
    }
    data_          = nullptr;
    sparse_cp_     = codepoints;
    sparse_glyphs_ = glyphs;
    sparse_count_  = glyph_count;
    mode_          = Mode::Sparse;
    return true;
}

bool XTEinkFont::pixel(uint32_t cp, int x, int y) const {
    if (x < 0 || x >= w_ || y < 0 || y >= h_) return false;

    const uint8_t* glyph = nullptr;

    if (mode_ == Mode::Flat) {
        if (cp >= 0x10000u || !data_) return false;
        glyph = data_ + static_cast<size_t>(cp) * char_byte_;
    } else if (mode_ == Mode::Sparse) {
        if (cp > 0xFFFFu || !sparse_cp_ || !sparse_glyphs_) return false;
        // Binary search the sorted codepoint table. Sparse subsets
        // (BIZ UDGothic JIS-L1) are ~3,500 entries → ~12 compares.
        int lo = 0;
        int hi = sparse_count_;
        while (lo < hi) {
            const int mid    = (lo + hi) >> 1;
            const uint16_t v = sparse_cp_[mid];
            if (v < cp) {
                lo = mid + 1;
            } else if (v > cp) {
                hi = mid;
            } else {
                lo = mid;
                hi = mid;  // exit loop signalling hit at lo
                break;
            }
        }
        if (lo >= sparse_count_ || sparse_cp_[lo] != cp) return false;
        glyph = sparse_glyphs_ + static_cast<size_t>(lo) * char_byte_;
    } else {
        return false;
    }

    const uint8_t byte = glyph[y * width_byte_ + (x >> 3)];
    return (byte & (0x80u >> (x & 7))) != 0;
}

}  // namespace ps3::font
