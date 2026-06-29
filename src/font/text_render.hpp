#pragma once

#include <cstdint>

#include "xteink_font.hpp"

namespace ps3::font {

// True for codepoints that should be drawn at half the font's full
// width — primarily ASCII printable, Latin-1 supplement, and the
// halfwidth katakana / hangul forms. Matches BIZ UDGothic's duospace
// ASCII layout where the visible glyph occupies the left ceil(W/2)
// pixels of each cell.
bool is_half_width(uint32_t cp);

// Per-codepoint advance in pixels for a font whose full-width cell
// is `full_w`. Half-width codepoints get `full_w / 2`, everything
// else gets `full_w`. Used both for layout (positioning the next
// glyph) and for clipping the glyph cell at draw time so half-width
// chars don't bleed background pixels into the next char's space.
inline int char_advance(uint32_t cp, int full_w) {
    return is_half_width(cp) ? (full_w >> 1) : full_w;
}

// Total drawn width of a UTF-8 string in pixels, summing each
// character's advance. `\n` is treated as zero-width (callers that
// need multi-line widths should split on newlines themselves).
int text_width(const char* utf8_str, const XTEinkFont& font);

// Draw a UTF-8 string starting at (x, y) with the given fixed-width
// font. `fg` and `bg` are 4-bit gray (0=black .. 15=white). Each
// glyph advances x by char_advance(cp, font.width()) — half-width
// codepoints render in their own narrow cell, full-width in the
// full cell. `\n` wraps to (start_x, current_y + font.height()).
//
// Returns the y coordinate of the next line (handy when chaining).
int draw_text(int x, int y, const char* utf8_str,
              const XTEinkFont& font,
              uint8_t fg = 0, uint8_t bg = 15);

}  // namespace ps3::font
