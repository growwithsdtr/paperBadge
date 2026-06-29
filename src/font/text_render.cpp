#include "text_render.hpp"

#include "../hal/display.hpp"
#include "utf8.hpp"

namespace ps3::font {

bool is_half_width(uint32_t cp) {
    // ASCII printable.
    if (cp >= 0x0020 && cp <= 0x007E) return true;
    // Latin-1 supplement — accented Latin chars, drawn duospace by
    // Japanese fonts that follow MS PGothic conventions.
    if (cp >= 0x00A0 && cp <= 0x00FF) return true;
    // Halfwidth Katakana / Hangul / arrows in U+FF00 block.
    if (cp >= 0xFF61 && cp <= 0xFFDC) return true;
    if (cp >= 0xFFE8 && cp <= 0xFFEE) return true;
    return false;
}

namespace {

// Draw `cp`'s glyph clipped to a `cell_w`-wide cell. Pixels outside
// the cell are NOT touched — half-width glyphs leave the right half
// of their nominal 24-wide cell alone, which avoids clobbering the
// next character's column or the framebuffer beyond. Background
// pixels inside the cell are still painted, so a half-width glyph
// still produces a clean rectangle of its advance width.
void draw_glyph(int x, int y, uint32_t cp, const XTEinkFont& font,
                uint8_t fg, uint8_t bg, int cell_w) {
    for (int gy = 0; gy < font.height(); ++gy) {
        for (int gx = 0; gx < cell_w; ++gx) {
            const uint8_t color = font.pixel(cp, gx, gy) ? fg : bg;
            ps3::display::put_pixel(x + gx, y + gy, color);
        }
    }
}

}  // namespace

int text_width(const char* s, const XTEinkFont& font) {
    if (!s) return 0;
    int w = 0;
    const int full_w = font.width();
    uint32_t cp = 0;
    while ((s = utf8_next(s, cp)) != nullptr && cp != 0) {
        if (cp == '\n') continue;
        w += char_advance(cp, full_w);
    }
    return w;
}

int draw_text(int x, int y, const char* s, const XTEinkFont& font,
              uint8_t fg, uint8_t bg) {
    const int start_x = x;
    const int full_w  = font.width();
    uint32_t cp = 0;
    while ((s = utf8_next(s, cp)) != nullptr) {
        if (cp == 0) break;
        if (cp == '\n') {
            x = start_x;
            y += font.height();
            continue;
        }
        const int adv = char_advance(cp, full_w);
        draw_glyph(x, y, cp, font, fg, bg, adv);
        x += adv;
    }
    return y + font.height();
}

}  // namespace ps3::font
