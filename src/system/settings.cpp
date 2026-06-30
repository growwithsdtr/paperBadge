#include "settings.hpp"

#include "../comic/touch_zones.hpp"   // TOOLBAR_HEIGHT
#include "../font/text_render.hpp"
#include "../font/utf8.hpp"
#include "../font/xteink_font.hpp"
#include "../hal/display.hpp"
#include "../hal/touch.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>

extern "C" {
#include <epdiy.h>
}

namespace ps3::settings {

namespace {

constexpr const char* TAG          = "settings";
constexpr const char* SETTINGS_PATH = "/sdcard/paperBadge/state/settings.kv";
constexpr const char* SETTINGS_TMP  = "/sdcard/paperBadge/state/settings.kv.tmp";

// Layout. The toolbar (y=0..TOOLBAR_HEIGHT=60) is reserved for the
// caller; a 20 px header gap keeps the first row clear of the
// toolbar divider. The list sits in y=80..480 (5 × 80 px). Anything
// below the last row (y >= 480) is treated as empty space and used
// as a close-on-tap area, the same way the toolbar slot for Setting
// can be used as a toggle. Taps in the header gap (y=60..80) do
// nothing — close behaviour was deliberately moved off the Top zone
// because the Top zone overlaps the first settings row.
constexpr int ITEM_TOP    = ps3::comic::TOOLBAR_HEIGHT + 20;   // 80
constexpr int ITEM_HEIGHT = 80;
// 5 rows now that システムフォント is gated off — see build_rows.
// When the SD-loaded system font is revived, bump back to 6 and
// uncomment the row in build_rows + the case in dispatch_tap.
constexpr int ITEM_COUNT  = 5;
constexpr int LIST_BOTTOM = ITEM_TOP + ITEM_COUNT * ITEM_HEIGHT;  // 480
constexpr int MARGIN_X    = 20;

// Absolute black points (8-bit) for each contrast level. Level 0 is
// pass-through; higher levels bend more of the lower range to pure
// black, which counteracts faded scans where pure black ended up as
// mid-gray after compression. Step size stays at +20 after the
// initial +30 jump so each additional level is a comparable bump
// regardless of where in the range the user is.
constexpr int kContrastBlack[] = { 0, 30, 50, 70, 90, 110, 130 };
constexpr int kContrastLevels  = sizeof(kContrastBlack) / sizeof(kContrastBlack[0]);

State s_state;

// ---- Toggle cycles -------------------------------------------------

int next_sleep(int cur) {
    switch (cur) {
        case 0:  return 5;
        case 5:  return 10;
        case 10: return 15;
        default: return 0;
    }
}

int next_power_off(int cur) {
    switch (cur) {
        case 0:  return 20;
        case 20: return 30;
        case 30: return 60;
        default: return 0;
    }
}

// ---- Display helpers -----------------------------------------------

// Format a "minutes" value as the right-aligned label shown next to
// sleep / power-off rows. 0 → "なし", positive → "<n>分".
void format_minutes(int m, char* out, size_t out_size) {
    if (m <= 0) {
        std::snprintf(out, out_size, "なし");
    } else {
        std::snprintf(out, out_size, "%d分", m);
    }
}

struct Row {
    const char* label;
    // Sized to fit "default" (7 ASCII × 24 px = 168) or a moderately
    // long Japanese filename. The right-align renderer will let
    // longer values bleed through; truncation is handled at write
    // time below.
    char        value[64];
};

// Copy `src` into `dst` (truncated to `dst_size` capacity), keeping
// the leading codepoints. UTF-8 boundary is respected so we don't
// leave a half-decoded byte at the end. When `src` is empty, writes
// "default" as the placeholder for the unset state.
void copy_image_label(char* dst, size_t dst_size, const char* src) {
    if (!src || !src[0]) {
        std::snprintf(dst, dst_size, "default");
        return;
    }
    // Bound writes so they always end at a UTF-8 boundary.
    size_t pos = 0;
    uint32_t cp = 0;
    const char* next = src;
    while ((next = ps3::font::utf8_next(next, cp)) != nullptr && cp != 0) {
        const size_t advance = static_cast<size_t>(next - src) - pos;
        if (pos + advance + 1 > dst_size) break;
        std::memcpy(dst + pos, src + pos, advance);
        pos += advance;
    }
    dst[pos] = '\0';
}

// Truncate `src` to at most `max_chars` UTF-8 codepoints, writing
// the result to `dst`. Used by render() to clip long filenames so
// they don't overdraw the row label. Same UTF-8 boundary contract
// as copy_image_label.
void truncate_utf8(const char* src, int max_chars,
                   char* dst, size_t dst_size) {
    if (dst_size == 0) return;
    int chars = 0;
    size_t pos = 0;
    uint32_t cp = 0;
    const char* next = src;
    while ((next = ps3::font::utf8_next(next, cp)) != nullptr && cp != 0) {
        if (chars >= max_chars) break;
        const size_t advance = static_cast<size_t>(next - src) - pos;
        if (pos + advance + 1 > dst_size) break;
        std::memcpy(dst + pos, src + pos, advance);
        pos += advance;
        ++chars;
    }
    dst[pos] = '\0';
}

void build_rows(Row* rows) {
#if 0  // システムフォント — currently disabled, revivable. The SD-
       // loaded XTEink path is gated off; the firmware ships with a
       // baked-in BIZ UDGothic UI subset instead. To restore: shift
       // the remaining rows back by 1, set ITEM_COUNT to 6, and
       // re-enable the matching cases in dispatch_tap + the
       // PickSystemFont handler in main.cpp / partitions.csv.
    rows[0].label = "システムフォント";
    copy_image_label(rows[0].value, sizeof(rows[0].value),
                     s_state.system_font);
#endif

    rows[0].label = "スリープ時間";
    format_minutes(s_state.sleep_minutes,
                   rows[0].value, sizeof(rows[0].value));

    rows[1].label = "電源オフ時間";
    format_minutes(s_state.power_off_minutes,
                   rows[1].value, sizeof(rows[1].value));

    rows[2].label = "画面回転";
    rows[2].value[0] = '\0';   // value-less item (action toggles in place)

    rows[3].label = "スリープ画面";
    copy_image_label(rows[3].value, sizeof(rows[3].value),
                     s_state.sleep_image);

    rows[4].label = "電源オフ画面";
    copy_image_label(rows[4].value, sizeof(rows[4].value),
                     s_state.power_off_image);
}

}  // namespace

State& state() { return s_state; }

void render(const ps3::font::XTEinkFont& font) {
    Row rows[ITEM_COUNT];
    build_rows(rows);

    const int screen_w = ps3::display::width();
    uint8_t*  fb       = ps3::display::framebuffer();

    // Reserve at least this much horizontal padding between the
    // label's right edge and the value's left edge so the two never
    // touch even when the value has been truncated to fit.
    constexpr int LABEL_VALUE_GAP = 24;  // = 1 codepoint at 24 px font width

    for (int i = 0; i < ITEM_COUNT; ++i) {
        const int row_top = ITEM_TOP + i * ITEM_HEIGHT;
        const int text_y  = row_top + (ITEM_HEIGHT - font.height()) / 2;

        // Left-aligned label
        ps3::font::draw_text(MARGIN_X, text_y, rows[i].label, font);

        // Right-aligned value (skipped when the row has no value
        // such as "画面回転", which acts in place on tap). Long
        // values (filenames) are clipped to whatever still fits to
        // the right of the label; otherwise they would overdraw
        // the label text. Width math uses text_width() so values
        // mixing ASCII and CJK (filenames like `mincho-24.bin`) get
        // accurate per-codepoint advances rather than assuming all
        // chars are full-width.
        if (rows[i].value[0]) {
            const int label_end_x =
                MARGIN_X + ps3::font::text_width(rows[i].label, font);
            const int avail_w =
                (screen_w - MARGIN_X) - (label_end_x + LABEL_VALUE_GAP);
            if (avail_w >= font.width()) {
                // max_chars is still computed against full-width to
                // keep clipping conservative — a mostly-ASCII value
                // would otherwise spill past the truncation boundary
                // if we over-counted how many fit.
                const int max_chars = avail_w / font.width();
                char clipped[sizeof(rows[i].value)];
                truncate_utf8(rows[i].value, max_chars,
                              clipped, sizeof(clipped));
                const int text_w = ps3::font::text_width(clipped, font);
                const int x      = screen_w - MARGIN_X - text_w;
                ps3::font::draw_text(x, text_y, clipped, font);
            }
        }

        // Divider line at the bottom of each row.
        epd_draw_hline(MARGIN_X, row_top + ITEM_HEIGHT - 1,
                       screen_w - 2 * MARGIN_X, 0x00, fb);
    }
}

TapResult dispatch_tap(int x, int y) {
    (void)x;  // taps are row-based; full row width is the hit area
    // Header gap (y=TOOLBAR_HEIGHT..ITEM_TOP) is non-interactive.
    if (y < ITEM_TOP) return TapResult::None;
    // Empty area below the list — repurposed as a close-on-tap zone.
    if (y >= LIST_BOTTOM) return TapResult::OutsideList;
    const int idx = (y - ITEM_TOP) / ITEM_HEIGHT;
    if (idx < 0 || idx >= ITEM_COUNT) return TapResult::None;

    switch (idx) {
#if 0  // システムフォント — currently disabled, revivable. Restore
       // in concert with build_rows + ITEM_COUNT (see comment there)
       // and shift the indices below back by 1.
        case 0:   // システムフォント — caller opens the font picker
            return TapResult::PickSystemFont;
#endif
        case 0:   // スリープ時間 — toggle
            s_state.sleep_minutes = next_sleep(s_state.sleep_minutes);
            save();
            return TapResult::ValueChanged;
        case 1:   // 電源オフ時間 — toggle
            s_state.power_off_minutes =
                next_power_off(s_state.power_off_minutes);
            save();
            return TapResult::ValueChanged;
        case 2: {  // 画面回転 — flip immediately
            s_state.rotation_inverted = !s_state.rotation_inverted;
            ps3::display::set_inverted(s_state.rotation_inverted);
            ps3::touch::set_inverted(s_state.rotation_inverted);
            save();
            return TapResult::RotationChanged;
        }
        case 3:   // スリープ画面 — caller opens the image picker
            return TapResult::PickSleepImage;
        case 4:   // 電源オフ画面 — caller opens the image picker
            return TapResult::PickPowerOffImage;
    }
    return TapResult::None;
}

bool save() {
    FILE* fp = std::fopen(SETTINGS_TMP, "w");
    if (!fp) {
        ESP_LOGE(TAG, "fopen %s for write failed", SETTINGS_TMP);
        return false;
    }
    std::fprintf(fp, "sleep_minutes=%d\n",     s_state.sleep_minutes);
    std::fprintf(fp, "power_off_minutes=%d\n", s_state.power_off_minutes);
    std::fprintf(fp, "rotation_inverted=%d\n",
                 s_state.rotation_inverted ? 1 : 0);
    std::fprintf(fp, "sleep_image=%s\n",     s_state.sleep_image);
    std::fprintf(fp, "power_off_image=%s\n", s_state.power_off_image);
    std::fprintf(fp, "system_font=%s\n",     s_state.system_font);
    std::fprintf(fp, "bookshelf_contrast=%d\n", s_state.bookshelf_contrast);
    std::fprintf(fp, "reading_contrast=%d\n",   s_state.reading_contrast);
    std::fprintf(fp, "full_refresh_pages=%d\n", s_state.full_refresh_pages);
    std::fprintf(fp, "refresh_profile=%d\n",    s_state.refresh_profile);
    std::fprintf(fp, "reader_font_level=%d\n",  s_state.reader_font_level);
    std::fprintf(fp, "interview_font_level=%d\n", s_state.interview_font_level);
    std::fprintf(fp, "japanese_font_level=%d\n", s_state.japanese_font_level);
    std::fprintf(fp, "japanese_font_face=%d\n", s_state.japanese_font_face);
    std::fprintf(fp, "western_font_profile=%d\n", s_state.western_font_profile);
    std::fprintf(fp, "right_binding=%d\n",
                 s_state.right_binding ? 1 : 0);
    std::fclose(fp);

    std::remove(SETTINGS_PATH);   // FATFS rename refuses to overwrite
    if (std::rename(SETTINGS_TMP, SETTINGS_PATH) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed", SETTINGS_TMP, SETTINGS_PATH);
        return false;
    }
    ESP_LOGI(TAG,
             "saved %s: sleep=%d power_off=%d rot=%d sleep_img=%s pw_img=%s "
             "bookshelf_contrast=%d reading_contrast=%d full_refresh_pages=%d "
             "refresh_profile=%d reader_font=%d interview_font=%d japanese_font=%d "
             "jp_face=%d western_profile=%d right_binding=%d",
             SETTINGS_PATH, s_state.sleep_minutes,
             s_state.power_off_minutes,
             s_state.rotation_inverted ? 1 : 0,
             s_state.sleep_image, s_state.power_off_image,
             s_state.bookshelf_contrast, s_state.reading_contrast,
             s_state.full_refresh_pages, s_state.refresh_profile,
             s_state.reader_font_level, s_state.interview_font_level,
             s_state.japanese_font_level, s_state.japanese_font_face,
             s_state.western_font_profile,
             s_state.right_binding ? 1 : 0);
    return true;
}

bool load() {
    FILE* fp = std::fopen(SETTINGS_PATH, "r");
    if (!fp) {
        ESP_LOGI(TAG, "no settings at %s — using defaults", SETTINGS_PATH);
        return true;
    }

    char line[128];
    while (std::fgets(line, sizeof(line), fp)) {
        // Strip trailing CR/LF.
        size_t len = std::strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        // Split key=value at the first '='. Lines without '=' are
        // skipped (treats blank / comment lines as harmless).
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (std::strcmp(key, "sleep_minutes") == 0) {
            s_state.sleep_minutes = std::atoi(val);
        } else if (std::strcmp(key, "power_off_minutes") == 0) {
            s_state.power_off_minutes = std::atoi(val);
        } else if (std::strcmp(key, "rotation_inverted") == 0) {
            s_state.rotation_inverted = (std::atoi(val) != 0);
        } else if (std::strcmp(key, "sleep_image") == 0) {
            std::strncpy(s_state.sleep_image, val,
                         sizeof(s_state.sleep_image) - 1);
            s_state.sleep_image[sizeof(s_state.sleep_image) - 1] = '\0';
        } else if (std::strcmp(key, "power_off_image") == 0) {
            std::strncpy(s_state.power_off_image, val,
                         sizeof(s_state.power_off_image) - 1);
            s_state.power_off_image[sizeof(s_state.power_off_image) - 1] = '\0';
        } else if (std::strcmp(key, "system_font") == 0) {
            std::strncpy(s_state.system_font, val,
                         sizeof(s_state.system_font) - 1);
            s_state.system_font[sizeof(s_state.system_font) - 1] = '\0';
        } else if (std::strcmp(key, "bookshelf_contrast") == 0) {
            int v = std::atoi(val);
            if (v < 0 || v >= kContrastLevels) v = 0;
            s_state.bookshelf_contrast = v;
        } else if (std::strcmp(key, "reading_contrast") == 0) {
            int v = std::atoi(val);
            if (v < 0 || v >= kContrastLevels) v = 0;
            s_state.reading_contrast = v;
        } else if (std::strcmp(key, "contrast") == 0) {
            // Legacy migration: settings.kv from before the per-screen
            // split stored a single `contrast=` value that fed both
            // screens. Apply it to both new fields so a returning user
            // doesn't lose their prior preference. The next save()
            // will overwrite the file with the new key names and this
            // line stops mattering.
            int v = std::atoi(val);
            if (v < 0 || v >= kContrastLevels) v = 0;
            s_state.bookshelf_contrast = v;
            s_state.reading_contrast   = v;
        } else if (std::strcmp(key, "full_refresh_pages") == 0) {
            // Validate against the menu's allowed cycle so a hand-
            // edited file with a junk value can't disable cleanups
            // entirely (which would let ghosting accumulate without
            // bound). Unknown values fall back to the default.
            const int v = std::atoi(val);
            switch (v) {
                case 1: case 5: case 10: case 15: case 20:
                    s_state.full_refresh_pages = v;
                    break;
                default:
                    s_state.full_refresh_pages = 10;
                    break;
            }
        } else if (std::strcmp(key, "refresh_profile") == 0) {
            const int v = std::atoi(val);
            s_state.refresh_profile = (v >= 0 && v <= 2) ? v : 1;
        } else if (std::strcmp(key, "reader_font_level") == 0) {
            const int v = std::atoi(val);
            s_state.reader_font_level = (v >= 0 && v <= 3) ? v : 1;
        } else if (std::strcmp(key, "interview_font_level") == 0) {
            const int v = std::atoi(val);
            s_state.interview_font_level = (v >= 0 && v <= 3) ? v : 1;
        } else if (std::strcmp(key, "japanese_font_level") == 0) {
            const int v = std::atoi(val);
            s_state.japanese_font_level = (v >= 0 && v <= 3) ? v : 1;
        } else if (std::strcmp(key, "japanese_font_face") == 0) {
            const int v = std::atoi(val);
            s_state.japanese_font_face = (v == 0) ? 0 : 1;
        } else if (std::strcmp(key, "western_font_profile") == 0) {
            const int v = std::atoi(val);
            s_state.western_font_profile = (v >= 0 && v <= 1) ? v : 0;
        } else if (std::strcmp(key, "right_binding") == 0) {
            s_state.right_binding = (std::atoi(val) != 0);
        }
        // Unknown keys are silently ignored — preserves forward-
        // compatibility when older firmware reads a newer file.
    }
    std::fclose(fp);

    ESP_LOGI(TAG,
             "loaded %s: sleep=%d power_off=%d rot=%d sleep_img=%s pw_img=%s "
             "bookshelf_contrast=%d reading_contrast=%d full_refresh_pages=%d "
             "refresh_profile=%d reader_font=%d interview_font=%d japanese_font=%d "
             "jp_face=%d western_profile=%d right_binding=%d",
             SETTINGS_PATH, s_state.sleep_minutes,
             s_state.power_off_minutes,
             s_state.rotation_inverted ? 1 : 0,
             s_state.sleep_image, s_state.power_off_image,
             s_state.bookshelf_contrast, s_state.reading_contrast,
             s_state.full_refresh_pages, s_state.refresh_profile,
             s_state.reader_font_level, s_state.interview_font_level,
             s_state.japanese_font_level, s_state.japanese_font_face,
             s_state.western_font_profile,
             s_state.right_binding ? 1 : 0);
    return true;
}

int contrast_for(ContrastContext ctx) {
    switch (ctx) {
        case ContrastContext::Reading:   return s_state.reading_contrast;
        case ContrastContext::Bookshelf: return s_state.bookshelf_contrast;
        case ContrastContext::Off:       return 0;
    }
    return 0;
}

int contrast_levels() {
    return kContrastLevels;
}

void cycle_contrast(ContrastContext ctx) {
    int* target = nullptr;
    switch (ctx) {
        case ContrastContext::Reading:   target = &s_state.reading_contrast;   break;
        case ContrastContext::Bookshelf: target = &s_state.bookshelf_contrast; break;
        case ContrastContext::Off:       return;
    }
    int n = *target + 1;
    if (n < 0 || n >= kContrastLevels) n = 0;
    *target = n;
    save();
}

void build_contrast_lut(uint8_t lut[256], ContrastContext ctx) {
    int level = contrast_for(ctx);
    if (level < 0 || level >= kContrastLevels) level = 0;
    const int B = kContrastBlack[level];
    if (B == 0) {
        for (int i = 0; i < 256; ++i) lut[i] = static_cast<uint8_t>(i);
        return;
    }
    const int span = 255 - B;
    for (int i = 0; i < 256; ++i) {
        if (i <= B) {
            lut[i] = 0;
        } else {
            const int v = ((i - B) * 255 + span / 2) / span;
            lut[i] = (v > 255) ? 255 : static_cast<uint8_t>(v);
        }
    }
}

void build_contrast_lut4(uint8_t lut4[16], ContrastContext ctx) {
    uint8_t lut8[256];
    build_contrast_lut(lut8, ctx);
    // Sample at the centre of each 4-bit bin so the curve is rounded
    // rather than truncated. Without the +0x08 offset, level=15
    // (input nibble 0xF == 8-bit 0xF0) would map to lut8[0xF0] >> 4
    // rather than lut8[0xFF] >> 4 — i.e. white would never reach 15.
    for (int g = 0; g < 16; ++g) {
        lut4[g] = lut8[(g << 4) | 0x08] >> 4;
    }
}

}  // namespace ps3::settings
