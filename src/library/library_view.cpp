#include "library_view.hpp"

#include "../font/text_render.hpp"
#include "../font/xteink_font.hpp"
#include "../hal/display.hpp"
#include "book_db.hpp"
#include "icons_data.hpp"
#include "thumbnail.hpp"

#include <cstdio>
#include <cstring>

#include <sys/stat.h>

extern "C" {
#include <epdiy.h>
}

namespace ps3::library_view {

namespace {

// --- Menu button helpers ---------------------------------------------------

const char* menu_label(MenuButton btn) {
    switch (btn) {
        case MenuButton::GoUp:     return "上へ";
        case MenuButton::PagePrev: return "左";
        case MenuButton::PageNext: return "右";
        case MenuButton::LastBook: return "前回";
        default:                   return "";
    }
}

int menu_label_codepoints(MenuButton btn) {
    switch (btn) {
        case MenuButton::GoUp:     return 2;
        case MenuButton::PagePrev: return 1;
        case MenuButton::PageNext: return 1;
        case MenuButton::LastBook: return 2;
        default:                   return 0;
    }
}

int menu_slot_w() {
    return ps3::display::width() / MENU_BUTTON_COUNT;
}

}  // namespace

bool menu_button_visible(MenuButton btn, const ps3::library::Library& lib) {
    switch (btn) {
        case MenuButton::GoUp:     return lib.can_go_up();
        case MenuButton::PagePrev: return lib.can_page_prev();
        case MenuButton::PageNext: return lib.can_page_next();
        // LastBook is always visible — the tap handler resolves the
        // most-recently-read CBZ from BookDb. When there is no last
        // book (fresh install) the handler shows a brief "no last
        // book" toast instead of opening anything, so we let the
        // user discover the affordance even before they've read
        // anything.
        case MenuButton::LastBook: return true;
        default:                   return false;
    }
}

MenuButton menu_button_hit_test(int tap_x,
                                const ps3::library::Library& lib) {
    const int btn_w = menu_slot_w();
    if (btn_w <= 0) return MenuButton::None;
    const int slot = tap_x / btn_w;
    if (slot < 0 || slot >= MENU_BUTTON_COUNT) return MenuButton::None;
    const auto btn = static_cast<MenuButton>(slot);
    return menu_button_visible(btn, lib) ? btn : MenuButton::None;
}

namespace {

// Returns the embedded icon glyph for `btn` if assets/icons/<stem>.png
// was present at build time; data=nullptr means "no icon, use text".
struct IconGlyph {
    const uint8_t* data;
    int            w;
    int            h;
};
IconGlyph icon_for(MenuButton btn) {
    switch (btn) {
        case MenuButton::GoUp:
            return ICON_GOUP_PRESENT
                       ? IconGlyph{ICON_GOUP_DATA, ICON_GOUP_W, ICON_GOUP_H}
                       : IconGlyph{nullptr, 0, 0};
        case MenuButton::PagePrev:
            return ICON_PAGEPREV_PRESENT
                       ? IconGlyph{ICON_PAGEPREV_DATA,
                                   ICON_PAGEPREV_W, ICON_PAGEPREV_H}
                       : IconGlyph{nullptr, 0, 0};
        case MenuButton::PageNext:
            return ICON_PAGENEXT_PRESENT
                       ? IconGlyph{ICON_PAGENEXT_DATA,
                                   ICON_PAGENEXT_W, ICON_PAGENEXT_H}
                       : IconGlyph{nullptr, 0, 0};
        case MenuButton::LastBook:
            return ICON_LASTBOOK_PRESENT
                       ? IconGlyph{ICON_LASTBOOK_DATA,
                                   ICON_LASTBOOK_W, ICON_LASTBOOK_H}
                       : IconGlyph{nullptr, 0, 0};
        default:
            return {nullptr, 0, 0};
    }
}

void render_menu(const ps3::library::Library& lib,
                 const ps3::font::XTEinkFont& font) {
    const int btn_w = menu_slot_w();
    const int btn_h = MENU_Y_BOTTOM - MENU_Y_TOP;

    // Divider line at the bottom of the menu strip, separating the
    // 4-button row from the tile grid below.
    epd_draw_hline(0, MENU_Y_BOTTOM - 1,
                   ps3::display::width(), 0x00,
                   ps3::display::framebuffer());

    for (int slot = 0; slot < MENU_BUTTON_COUNT; ++slot) {
        const auto btn = static_cast<MenuButton>(slot);
        if (!menu_button_visible(btn, lib)) continue;

        const int x = slot * btn_w;
        const int y = MENU_Y_TOP;

        const IconGlyph g = icon_for(btn);
        if (g.data) {
            // Reuse the thumbnail blitter — same 4 bpp pack convention,
            // and it already handles the INVERTED_PORTRAIT rotation per
            // pixel. ThumbnailImage::packed is non-const but the blit
            // path is read-only on src, so the const_cast is safe.
            ps3::library::ThumbnailImage img{};
            img.packed = const_cast<uint8_t*>(g.data);
            img.width  = g.w;
            img.height = g.h;
            const int dst_x = x + (btn_w - g.w) / 2;
            const int dst_y = y + (btn_h - g.h) / 2;
            ps3::library::blit_thumbnail(img, dst_x, dst_y, g.w, g.h);
        } else {
            const int label_w = menu_label_codepoints(btn) * font.width();
            const int text_x  = x + (btn_w - label_w) / 2;
            const int text_y  = y + (btn_h - font.height()) / 2;
            ps3::font::draw_text(text_x, text_y, menu_label(btn), font);
        }
    }
}

}  // namespace

TileRect tile_rect(int idx) {
    const int row = idx / TILE_COLS;
    const int col = idx % TILE_COLS;
    TileRect r{};
    r.x = TILE_AREA_X + TILE_OUTER_MARGIN + col * (TILE_W + TILE_INNER_GAP);
    r.y = TILE_AREA_Y + TILE_OUTER_MARGIN + row * (TILE_H + TILE_INNER_GAP);
    r.w = TILE_W;
    r.h = TILE_H;
    return r;
}

namespace {

// Folder tiles render as three overlapping rectangles to suggest a
// stack of papers. The frontmost (which contains the thumbnail or
// fallback text) is shifted toward the bottom-left of the tile so
// the back layers' top-right corners peek out.
constexpr int FOLDER_LAYER_OFFSET = 6;

// Draws the tile frame and returns the rectangle inside which content
// (thumbnail / text) should be drawn. CBZ tiles get a single-rect
// border, folder tiles get a stacked-paper look.
TileRect draw_tile_frame(ps3::library::EntryKind kind, const TileRect& r) {
    uint8_t* fb = ps3::display::framebuffer();

    if (kind != ps3::library::EntryKind::Folder) {
        EpdRect epd_r{};
        epd_r.x = r.x; epd_r.y = r.y;
        epd_r.width = r.w; epd_r.height = r.h;
        epd_draw_rect(epd_r, 0x00, fb);
        return r;
    }

    const int w = r.w - 2 * FOLDER_LAYER_OFFSET;
    const int h = r.h - 2 * FOLDER_LAYER_OFFSET;

    // Two back layers (deepest first). White fill is needed so each
    // layer hides the previous layer's interior border lines.
    for (int layer = 2; layer >= 1; --layer) {
        EpdRect back{};
        back.x = r.x + layer * FOLDER_LAYER_OFFSET;
        back.y = r.y + (2 - layer) * FOLDER_LAYER_OFFSET;
        back.width = w; back.height = h;
        epd_fill_rect(back, 0xFF, fb);
        epd_draw_rect(back, 0x00, fb);
    }

    // Front layer (will receive the thumbnail / text).
    EpdRect front{};
    front.x = r.x; front.y = r.y + 2 * FOLDER_LAYER_OFFSET;
    front.width = w; front.height = h;
    epd_fill_rect(front, 0xFF, fb);
    epd_draw_rect(front, 0x00, fb);

    return TileRect{front.x, front.y, w, h};
}

// Lex-smallest BookRecord whose path is a child (any depth) of
// `folder_path` and has a generated thumbnail. Used for the folder
// tile's representative cover.
const ps3::library::BookRecord* find_first_thumbed_child(
        const ps3::library::BookDb& db, const char* folder_path) {
    const size_t prefix_len = std::strlen(folder_path);
    const ps3::library::BookRecord* best = nullptr;
    for (int i = 0; i < db.count(); ++i) {
        const ps3::library::BookRecord& r = db.at(i);
        if (std::strncmp(r.path, folder_path, prefix_len) != 0) continue;
        if (r.path[prefix_len] != '/') continue;
        if (!r.thumb[0]) continue;
        if (!best ||
            ps3::library::natural_compare(r.path, best->path) < 0) {
            best = &r;
        }
    }
    return best;
}

// Returns true if a thumbnail was successfully drawn into `content`;
// false to fall through to the text fallback.
bool draw_tile_thumbnail(int idx,
                         const ps3::library::Library& lib,
                         const ps3::library::BookDb& db,
                         const TileRect& content) {
    using namespace ps3::library;
    const Entry& e = lib.at(idx);

    char path[MAX_PATH_LEN];
    if (!lib.full_path_of(idx, path, sizeof(path))) return false;

    const char* thumb_name = nullptr;
    if (e.kind == EntryKind::Cbz) {
        const BookRecord* rec = db.find(path);
        if (!rec || !rec->thumb[0]) return false;
        thumb_name = rec->thumb;
    } else if (e.kind == EntryKind::Folder) {
        const BookRecord* rec = find_first_thumbed_child(db, path);
        if (!rec) return false;
        thumb_name = rec->thumb;
    } else {
        return false;
    }

    char thumb_path[MAX_PATH_LEN];
    BookDb::make_thumb_path(thumb_name, thumb_path, sizeof(thumb_path));
    struct stat st;
    if (::stat(thumb_path, &st) != 0) return false;

    ThumbnailImage img = load_thumbnail(thumb_path);
    if (!img.packed) return false;

    // Inset the thumbnail by 1 px on each side so the front-rect
    // border remains visible.
    blit_thumbnail(img, content.x + 1, content.y + 1,
                   content.w - 2, content.h - 2);
    free_thumbnail(img);
    return true;
}

}  // namespace

void render(const ps3::library::Library& lib,
            const ps3::library::BookDb& db,
            const ps3::font::XTEinkFont& font) {
    using namespace ps3::library;

    // Top zone: 4-slot button strip.
    render_menu(lib, font);

    if (lib.count() == 0) {
        const TileRect r = tile_rect(0);
        ps3::font::draw_text(r.x, r.y + (r.h - font.height()) / 2,
                             "(empty)", font);
        return;
    }

    constexpr int TEXT_PAD = 6;

    for (int i = 0; i < lib.count(); ++i) {
        const Entry& e = lib.at(i);
        const TileRect tile = tile_rect(i);

        // Draws the tile frame (single-rect for CBZ, stacked papers
        // for Folder) and returns the inner rect for content.
        const TileRect content = draw_tile_frame(e.kind, tile);

        if (draw_tile_thumbnail(i, lib, db, content)) continue;

        // Wrap the name across multiple lines so we can show as much
        // of long titles as possible. The split is purely by codepoint
        // count — Japanese names have no spaces so word boundaries
        // aren't useful here.
        const int chars_per_line = (content.w - TEXT_PAD * 2) / font.width();
        const int max_lines = (content.h - TEXT_PAD * 2) / font.height();
        if (chars_per_line <= 0 || max_lines <= 0) continue;

        // Pre-walk the name and stash up to max_lines line-start byte
        // offsets + lengths.
        struct LineSlice { const char* start; size_t bytes; };
        LineSlice slices[8] = {};
        const int slot_cap = max_lines < 8 ? max_lines : 8;
        int line_count = 0;

        const char* p = e.name;
        while (*p && line_count < slot_cap) {
            const char* line_start = p;
            int chars_taken = 0;
            while (*p && chars_taken < chars_per_line) {
                const unsigned char c = static_cast<unsigned char>(*p);
                int len = 1;
                if      ((c & 0x80) == 0)    len = 1;
                else if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else                          len = 4;
                // Advance exactly `len` bytes, stopping at NUL.
                // Stepping past the terminator on the last codepoint
                // drops its tail byte for two-byte+ sequences, so
                // re-check `*p` between increments.
                for (int j = 0; j < len; ++j) {
                    if (!*p) break;
                    ++p;
                }
                ++chars_taken;
            }
            slices[line_count].start = line_start;
            slices[line_count].bytes = static_cast<size_t>(p - line_start);
            ++line_count;
        }

        // Vertical-center the block of text within the content rect.
        const int block_h = line_count * font.height();
        int y = content.y + (content.h - block_h) / 2;

        char buf[MAX_NAME_LEN];
        for (int li = 0; li < line_count; ++li) {
            size_t n = slices[li].bytes;
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            std::memcpy(buf, slices[li].start, n);
            buf[n] = '\0';
            ps3::font::draw_text(content.x + TEXT_PAD, y, buf, font);
            y += font.height();
        }
    }
}

int hit_test(int tap_x, int tap_y, const ps3::library::Library& lib) {
    for (int i = 0; i < lib.count(); ++i) {
        const TileRect r = tile_rect(i);
        if (tap_x >= r.x && tap_x < r.x + r.w &&
            tap_y >= r.y && tap_y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

}  // namespace ps3::library_view
