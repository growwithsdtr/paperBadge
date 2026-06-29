#pragma once

#include <cstdint>

namespace ps3::font {

// Decode the next UTF-8 codepoint starting at `s`.
// On success: writes the codepoint to `cp` and returns the pointer past
// the consumed bytes. Returns nullptr at end-of-string (cp set to 0).
// Malformed sequences yield U+FFFD and advance one byte.
// Codepoints above U+FFFF (outside the BMP) are reported as U+FFFD because
// the 1 bpp font binary only covers the BMP.
inline const char* utf8_next(const char* s, uint32_t& cp) {
    if (!s || !*s) { cp = 0; return nullptr; }
    const uint8_t c0 = static_cast<uint8_t>(s[0]);
    if (c0 < 0x80) {
        cp = c0;
        return s + 1;
    }
    if ((c0 & 0xE0) == 0xC0) {
        const uint8_t c1 = static_cast<uint8_t>(s[1]);
        if ((c1 & 0xC0) != 0x80) { cp = 0xFFFD; return s + 1; }
        cp = (uint32_t(c0 & 0x1F) << 6) | uint32_t(c1 & 0x3F);
        return s + 2;
    }
    if ((c0 & 0xF0) == 0xE0) {
        const uint8_t c1 = static_cast<uint8_t>(s[1]);
        const uint8_t c2 = static_cast<uint8_t>(s[2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) { cp = 0xFFFD; return s + 1; }
        cp = (uint32_t(c0 & 0x0F) << 12)
           | (uint32_t(c1 & 0x3F) << 6)
           | uint32_t(c2 & 0x3F);
        return s + 3;
    }
    if ((c0 & 0xF8) == 0xF0) {
        // 4-byte sequence (>= U+10000) — outside BMP. Skip it.
        cp = 0xFFFD;
        return s + 4;
    }
    cp = 0xFFFD;
    return s + 1;
}

}  // namespace ps3::font
