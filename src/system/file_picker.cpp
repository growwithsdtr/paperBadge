#include "file_picker.hpp"

#include "../comic/touch_zones.hpp"
#include "../font/text_render.hpp"
#include "../font/xteink_font.hpp"
#include "../hal/display.hpp"
#include "../hal/touch.hpp"
#include "../library/library.hpp"   // natural_compare, MAX_NAME_LEN

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include <dirent.h>
#include <sys/stat.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
#include <epdiy.h>
}

namespace ps3::file_picker {

namespace {

constexpr const char* TAG = "file_pick";

// Layout matches the bookshelf so the picker feels familiar:
//   y=0..60     toolbar
//   y=60..170   4-slot top menu (GoUp / PagePrev / PageNext / —)
//   y=170..960  3x3 tile area
constexpr int MENU_Y_TOP    = ps3::comic::TOOLBAR_HEIGHT;       // 60
constexpr int MENU_Y_BOTTOM = MENU_Y_TOP + ps3::comic::TOP_BAND_PX;  // 170
constexpr int MENU_BUTTONS  = 4;

constexpr int TILE_COLS = 3;
constexpr int TILE_ROWS = 3;
constexpr int TILES_PER_PAGE = TILE_COLS * TILE_ROWS;
constexpr int TILE_AREA_X = 0;
constexpr int TILE_AREA_Y = MENU_Y_BOTTOM;
constexpr int TILE_AREA_W = 540;
constexpr int TILE_AREA_H = 960 - TILE_AREA_Y;
constexpr int TILE_OUTER_MARGIN = 10;
constexpr int TILE_INNER_GAP    = 10;
constexpr int TILE_W = (TILE_AREA_W - 2 * TILE_OUTER_MARGIN
                        - (TILE_COLS - 1) * TILE_INNER_GAP) / TILE_COLS;
constexpr int TILE_H = (TILE_AREA_H - 2 * TILE_OUTER_MARGIN
                        - (TILE_ROWS - 1) * TILE_INNER_GAP) / TILE_ROWS;

// PSRAM-friendly upper bound. Each Entry is MAX_NAME_LEN bytes.
constexpr int MAX_ENTRIES = 200;

struct Entry {
    char name[ps3::library::MAX_NAME_LEN];
};

bool match_extension(const char* name,
                     const char* const* exts, int ext_count) {
    const size_t n = std::strlen(name);
    for (int i = 0; i < ext_count; ++i) {
        const char* ext = exts[i];
        const size_t el = std::strlen(ext);
        if (n >= el && strcasecmp(name + n - el, ext) == 0) return true;
    }
    return false;
}

// Walk `cfg.dir` and fill `entries[1..]` with the filenames of
// non-hidden entries whose extension matches one of `cfg.exts`,
// sorted by natural_compare. Slot 0 is reserved for the "default"
// sentinel (set here as an empty name). Returns the total filled
// count (>=1).
int scan(const Config& cfg, Entry* entries, int max_entries) {
    int count = 0;
    if (cfg.include_default) {
        entries[count].name[0] = '\0';   // [0] is the "default" sentinel
        ++count;
    }
    const int real_start = count;        // first index for actual files

    DIR* d = opendir(cfg.dir);
    if (!d) {
        ESP_LOGW(TAG, "opendir %s failed (missing dir?)", cfg.dir);
        return count;
    }
    struct dirent* de = nullptr;
    int dropped = 0;
    while ((de = readdir(d)) != nullptr) {
        const char* name = de->d_name;
        if (!name[0] || ps3::library::is_hidden_name(name)) continue;
        if (!match_extension(name, cfg.exts, cfg.ext_count)) continue;
        if (count >= max_entries) {
            ++dropped;
            continue;
        }
        std::strncpy(entries[count].name, name,
                     ps3::library::MAX_NAME_LEN - 1);
        entries[count].name[ps3::library::MAX_NAME_LEN - 1] = '\0';
        ++count;
    }
    closedir(d);

    // Sort only the real entries (default sentinel, when present,
    // stays pinned at slot 0).
    if (count - real_start > 1) {
        std::sort(entries + real_start, entries + count,
                  [](const Entry& a, const Entry& b) {
                      return ps3::library::natural_compare(a.name, b.name) < 0;
                  });
    }
    if (dropped > 0) {
        ESP_LOGW(TAG, "scan %s: %d entries (dropped %d over cap %d)",
                 cfg.dir, count, dropped, max_entries);
    } else {
        ESP_LOGI(TAG, "scan %s: %d entries", cfg.dir, count);
    }
    return count;
}

struct Rect { int x, y, w, h; };

Rect tile_rect(int idx_on_page) {
    const int row = idx_on_page / TILE_COLS;
    const int col = idx_on_page % TILE_COLS;
    Rect r{};
    r.x = TILE_AREA_X + TILE_OUTER_MARGIN + col * (TILE_W + TILE_INNER_GAP);
    r.y = TILE_AREA_Y + TILE_OUTER_MARGIN + row * (TILE_H + TILE_INNER_GAP);
    r.w = TILE_W;
    r.h = TILE_H;
    return r;
}

int total_pages(int count) {
    if (count <= 0) return 0;
    return (count + TILES_PER_PAGE - 1) / TILES_PER_PAGE;
}

bool can_page_prev(int page) { return page > 0; }
bool can_page_next(int page, int count) {
    return page + 1 < total_pages(count);
}

enum class MenuButton { None, GoUp, PagePrev, PageNext };

bool menu_visible(MenuButton btn, int page, int count) {
    switch (btn) {
        case MenuButton::GoUp:     return true;
        case MenuButton::PagePrev: return can_page_prev(page);
        case MenuButton::PageNext: return can_page_next(page, count);
        default:                   return false;
    }
}

const char* menu_label(MenuButton btn) {
    switch (btn) {
        case MenuButton::GoUp:     return "戻る";
        case MenuButton::PagePrev: return "左";
        case MenuButton::PageNext: return "右";
        default:                   return "";
    }
}

int menu_label_codepoints(MenuButton btn) {
    switch (btn) {
        case MenuButton::GoUp:     return 2;
        case MenuButton::PagePrev: return 1;
        case MenuButton::PageNext: return 1;
        default:                   return 0;
    }
}

int menu_slot_w() { return ps3::display::width() / MENU_BUTTONS; }

MenuButton menu_hit_test(int tap_x, int page, int count) {
    const int btn_w = menu_slot_w();
    if (btn_w <= 0) return MenuButton::None;
    const int slot = tap_x / btn_w;
    if (slot < 0 || slot >= MENU_BUTTONS) return MenuButton::None;
    MenuButton btn = MenuButton::None;
    switch (slot) {
        case 0: btn = MenuButton::GoUp;     break;
        case 1: btn = MenuButton::PagePrev; break;
        case 2: btn = MenuButton::PageNext; break;
        default: return MenuButton::None;
    }
    return menu_visible(btn, page, count) ? btn : MenuButton::None;
}

void render_menu(const ps3::font::XTEinkFont& font, int page, int count) {
    const int btn_w = menu_slot_w();
    const int btn_h = MENU_Y_BOTTOM - MENU_Y_TOP;
    epd_draw_hline(0, MENU_Y_BOTTOM - 1,
                   ps3::display::width(), 0x00,
                   ps3::display::framebuffer());

    for (int slot = 0; slot < 3; ++slot) {
        MenuButton btn = MenuButton::None;
        switch (slot) {
            case 0: btn = MenuButton::GoUp;     break;
            case 1: btn = MenuButton::PagePrev; break;
            case 2: btn = MenuButton::PageNext; break;
        }
        if (!menu_visible(btn, page, count)) continue;

        const int x = slot * btn_w;
        const int y = MENU_Y_TOP;
        const int label_w =
            menu_label_codepoints(btn) * font.width();
        const int text_x = x + (btn_w - label_w) / 2;
        const int text_y = y + (btn_h - font.height()) / 2;
        ps3::font::draw_text(text_x, text_y, menu_label(btn), font);
    }
}

// Draw the contents of a tile: "default" placeholder for the
// sentinel entry, otherwise the filename word-wrapped to fit the
// tile (fixed-width font, codepoint-counted).
void render_tile(const Entry& entry, int idx_on_page,
                 const ps3::font::XTEinkFont& font,
                 bool is_current) {
    uint8_t* fb = ps3::display::framebuffer();
    const Rect r = tile_rect(idx_on_page);

    EpdRect epd_r{};
    epd_r.x = r.x; epd_r.y = r.y;
    epd_r.width = r.w; epd_r.height = r.h;
    epd_draw_rect(epd_r, 0x00, fb);
    if (is_current) {
        // Inset border to mark the currently selected entry.
        EpdRect inner{};
        inner.x = r.x + 1; inner.y = r.y + 1;
        inner.width = r.w - 2; inner.height = r.h - 2;
        epd_draw_rect(inner, 0x00, fb);
        EpdRect inner2{};
        inner2.x = r.x + 2; inner2.y = r.y + 2;
        inner2.width = r.w - 4; inner2.height = r.h - 4;
        epd_draw_rect(inner2, 0x00, fb);
    }

    // Layout the label (filename or "default" placeholder) into up
    // to `slot_cap` lines of `chars_per_line` codepoints.
    constexpr int TEXT_PAD = 6;
    const int chars_per_line = (r.w - TEXT_PAD * 2) / font.width();
    const int max_lines = (r.h - TEXT_PAD * 2) / font.height();
    if (chars_per_line <= 0 || max_lines <= 0) return;

    const char* label = entry.name[0] ? entry.name : "default";
    struct LineSlice { const char* start; size_t bytes; };
    LineSlice slices[8] = {};
    const int slot_cap = max_lines < 8 ? max_lines : 8;
    int line_count = 0;

    const char* p = label;
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

    const int block_h = line_count * font.height();
    int y = r.y + (r.h - block_h) / 2;

    char buf[ps3::library::MAX_NAME_LEN];
    for (int li = 0; li < line_count; ++li) {
        size_t n = slices[li].bytes;
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        std::memcpy(buf, slices[li].start, n);
        buf[n] = '\0';
        ps3::font::draw_text(r.x + TEXT_PAD, y, buf, font);
        y += font.height();
    }
}

void render(const Entry* entries, int count, int page, int current_idx,
            const ps3::font::XTEinkFont& font) {
    ps3::display::clear();
    render_menu(font, page, count);

    const int start = page * TILES_PER_PAGE;
    const int end   = std::min(start + TILES_PER_PAGE, count);
    for (int i = start; i < end; ++i) {
        render_tile(entries[i], i - start, font, i == current_idx);
    }

    ps3::display::flush(ps3::display::RefreshMode::GC16);
}

int tile_hit_test(int tap_x, int tap_y, int page, int count) {
    const int start = page * TILES_PER_PAGE;
    const int end   = std::min(start + TILES_PER_PAGE, count);
    for (int i = start; i < end; ++i) {
        const Rect r = tile_rect(i - start);
        if (tap_x >= r.x && tap_x < r.x + r.w &&
            tap_y >= r.y && tap_y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

int find_index_by_name(const Entry* entries, int count, const char* name) {
    if (!name) return 0;   // null treated as default
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(entries[i].name, name) == 0) return i;
    }
    return 0;  // fallback to default if the saved name no longer exists
}

}  // namespace

bool run(const ps3::font::XTEinkFont& font,
         const Config& config,
         const char* current,
         char* out, std::size_t out_size) {
    if (!out || out_size == 0) return false;
    if (!config.dir || !config.exts || config.ext_count <= 0) return false;

    Entry* entries = static_cast<Entry*>(
        heap_caps_malloc(sizeof(Entry) * MAX_ENTRIES, MALLOC_CAP_SPIRAM));
    if (!entries) {
        ESP_LOGE(TAG, "PSRAM alloc for %d entries (%u bytes) failed",
                 MAX_ENTRIES, (unsigned)(sizeof(Entry) * MAX_ENTRIES));
        return false;
    }

    const int count = scan(config, entries, MAX_ENTRIES);
    int current_idx = find_index_by_name(entries, count, current);
    int page = current_idx / TILES_PER_PAGE;

    render(entries, count, page, current_idx, font);
    ps3::touch::drain();   // discard the tap that opened the picker

    bool committed = false;
    while (true) {
        int tx = 0, ty = 0;
        if (!ps3::touch::poll_tap(&tx, &ty)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const auto zone = ps3::comic::classify(
            tx, ty, ps3::display::width(), ps3::display::height());

        // Toolbar Setting / Full / empty slot all cancel; Sleep and
        // Reload keep their normal meaning, but for simplicity and
        // because the picker is a transient modal we treat any
        // toolbar tap as cancel. Users can hit the GoUp button or
        // pick an entry to leave normally.
        if (zone == ps3::comic::TouchZone::Toolbar) {
            ESP_LOGI(TAG, "toolbar tap -> cancel");
            break;
        }

        if (zone == ps3::comic::TouchZone::Top) {
            const auto btn = menu_hit_test(tx, page, count);
            switch (btn) {
                case MenuButton::GoUp:
                    ESP_LOGI(TAG, "menu GoUp -> cancel");
                    goto done;
                case MenuButton::PagePrev:
                    if (can_page_prev(page)) {
                        --page;
                        render(entries, count, page, current_idx, font);
                    }
                    continue;
                case MenuButton::PageNext:
                    if (can_page_next(page, count)) {
                        ++page;
                        render(entries, count, page, current_idx, font);
                    }
                    continue;
                default:
                    continue;
            }
        }

        // Tile area (Middle + Bottom share the grid).
        const int idx = tile_hit_test(tx, ty, page, count);
        if (idx >= 0) {
            ESP_LOGI(TAG, "tile %d (%s) -> commit",
                     idx, entries[idx].name[0] ? entries[idx].name : "default");
            std::strncpy(out, entries[idx].name, out_size - 1);
            out[out_size - 1] = '\0';
            committed = true;
            break;
        }
    }

done:
    std::free(entries);
    return committed;
}

}  // namespace ps3::file_picker
