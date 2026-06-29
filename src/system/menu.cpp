#include "menu.hpp"

#include "../comic/touch_zones.hpp"   // TOOLBAR_HEIGHT
#include "../font/text_render.hpp"
#include "../font/utf8.hpp"
#include "../font/xteink_font.hpp"
#include "../hal/display.hpp"
#include "settings.hpp"

#include <cstdio>
#include <cstring>

#include <esp_log.h>

extern "C" {
#include <epdiy.h>
}

namespace ps3::menu {

namespace {

constexpr const char* TAG = "menu";

// Same vertical layout as settings.cpp, so the two overlays feel
// consistent. The header gap (y < ITEM_TOP) is non-interactive so a
// stray tap on the toolbar/menu boundary doesn't fire the first row.
constexpr int ITEM_TOP    = ps3::comic::TOOLBAR_HEIGHT + 20;   // 80
constexpr int ITEM_HEIGHT = 80;
constexpr int MARGIN_X    = 20;

// Bookshelf "サムネイル作成" row geometry. The row hosts a left-
// anchored label plus two side-by-side text buttons on the right
// (全更新 / 差分のみ). Sizes are derived from the font metrics at
// render() time so the same helper computes hit-bounds in
// dispatch_tap() — keeps the two in sync without a precomputed
// rect being passed around.
constexpr int THUMB_BTN_GAP   = 8;    // gap between the two buttons
constexpr int THUMB_BTN_PAD_V = 12;   // vertical padding inside row
constexpr int THUMB_LABEL_GAP = 16;   // gap between label and first button

const char* const kThumbLabel    = "サムネイル作成";
const char* const kThumbBtn1Text = "全更新";    // force regenerate
const char* const kThumbBtn2Text = "差分のみ";  // missing only

struct ThumbButtons {
    int row_top;
    int btn_y;
    int btn_h;
    int btn_w;
    int btn1_x;   // 全更新
    int btn2_x;   // 差分のみ
};

ThumbButtons thumb_button_geom(int row_idx,
                               const ps3::font::XTEinkFont& font) {
    ThumbButtons g{};
    const int screen_w = ps3::display::width();
    g.row_top = ITEM_TOP + row_idx * ITEM_HEIGHT;
    g.btn_y   = g.row_top + THUMB_BTN_PAD_V;
    g.btn_h   = ITEM_HEIGHT - 2 * THUMB_BTN_PAD_V;
    const int label_w = ps3::font::text_width(kThumbLabel, font);
    const int btn_area_left  = MARGIN_X + label_w + THUMB_LABEL_GAP;
    const int btn_area_right = screen_w - MARGIN_X;
    const int btn_area_w     = btn_area_right - btn_area_left;
    g.btn_w  = (btn_area_w - THUMB_BTN_GAP) / 2;
    g.btn1_x = btn_area_left;
    g.btn2_x = btn_area_left + g.btn_w + THUMB_BTN_GAP;
    return g;
}

// Allowed cycle for full_refresh_pages. Picked to span "every page
// is GC16Full" (1 = max quality, slow) through "longer GL16 streaks
// before cleanup" (20 = faster, more lingering ghost).
constexpr int kFullRefreshOptions[]  = { 1, 5, 10, 15, 20 };
constexpr int kFullRefreshOptionCount =
    sizeof(kFullRefreshOptions) / sizeof(kFullRefreshOptions[0]);

int next_full_refresh(int cur) {
    for (int i = 0; i < kFullRefreshOptionCount; ++i) {
        if (kFullRefreshOptions[i] == cur) {
            return kFullRefreshOptions[(i + 1) % kFullRefreshOptionCount];
        }
    }
    return 10;  // unknown stored value: snap back to default
}

// Row layout per context:
//   Bookshelf : [0] コントラスト, [1] サムネイル作成 (split buttons)
//   Reading   : [0] コントラスト, [1] 完全更新頻度, [2] ページめくり方向
int row_count(Context ctx) {
    return (ctx == Context::Reading) ? 3 : 2;
}

int list_bottom(Context ctx) {
    return ITEM_TOP + row_count(ctx) * ITEM_HEIGHT;
}

// Which row in `ctx` is the サムネイル作成 split-button row, or -1
// when the context doesn't have one. Currently only Bookshelf.
int thumb_row_idx(Context ctx) {
    return (ctx == Context::Bookshelf) ? 1 : -1;
}

struct Row {
    const char* label;
    char        value[32];   // empty for special-rendered rows
};

void format_contrast(int level, char* out, size_t out_size) {
    if (level <= 0) {
        std::snprintf(out, out_size, "標準");
    } else {
        std::snprintf(out, out_size, "+%d", level);
    }
}

void build_rows(Context ctx, Row* rows) {
    auto& s = ps3::settings::state();
    rows[0].label    = "コントラスト";
    rows[0].value[0] = '\0';
    const int contrast = (ctx == Context::Reading) ? s.reading_contrast
                                                   : s.bookshelf_contrast;
    format_contrast(contrast, rows[0].value, sizeof(rows[0].value));

    if (ctx == Context::Reading) {
        rows[1].label = "完全更新頻度";
        std::snprintf(rows[1].value, sizeof(rows[1].value),
                      "%dページごと", s.full_refresh_pages);
        rows[2].label = "ページめくり方向";
        std::snprintf(rows[2].value, sizeof(rows[2].value),
                      "%s", s.right_binding ? "右綴じ" : "左綴じ");
    } else {
        // Bookshelf: row 1 = サムネイル作成 — rendered specially with
        // two text buttons. Leave `value` empty so the generic
        // right-aligned text path is skipped.
        rows[1].label    = kThumbLabel;
        rows[1].value[0] = '\0';
    }
}

void render_thumb_row(int row_idx, const ps3::font::XTEinkFont& font) {
    const ThumbButtons g = thumb_button_geom(row_idx, font);
    uint8_t* fb = ps3::display::framebuffer();
    const int text_y = g.row_top + (ITEM_HEIGHT - font.height()) / 2;

    // Outline the two buttons (1 px black rect, white interior).
    EpdRect r1{ g.btn1_x, g.btn_y, g.btn_w, g.btn_h };
    EpdRect r2{ g.btn2_x, g.btn_y, g.btn_w, g.btn_h };
    epd_draw_rect(r1, 0x00, fb);
    epd_draw_rect(r2, 0x00, fb);

    // Centered captions inside each button.
    const int t1_w = ps3::font::text_width(kThumbBtn1Text, font);
    const int t2_w = ps3::font::text_width(kThumbBtn2Text, font);
    ps3::font::draw_text(g.btn1_x + (g.btn_w - t1_w) / 2,
                         text_y, kThumbBtn1Text, font);
    ps3::font::draw_text(g.btn2_x + (g.btn_w - t2_w) / 2,
                         text_y, kThumbBtn2Text, font);
}

}  // namespace

void render(const ps3::font::XTEinkFont& font, Context ctx) {
    Row rows[3];
    build_rows(ctx, rows);

    const int screen_w = ps3::display::width();
    uint8_t*  fb       = ps3::display::framebuffer();
    const int n        = row_count(ctx);
    const int thumb_i  = thumb_row_idx(ctx);

    for (int i = 0; i < n; ++i) {
        const int row_top = ITEM_TOP + i * ITEM_HEIGHT;
        const int text_y  = row_top + (ITEM_HEIGHT - font.height()) / 2;

        // Left-aligned label (every row has one).
        ps3::font::draw_text(MARGIN_X, text_y, rows[i].label, font);

        if (i == thumb_i) {
            // Special: two text buttons in place of the value.
            render_thumb_row(i, font);
        } else if (rows[i].value[0]) {
            // Right-aligned value. Menu values are short (`+3`,
            // `標準`, `10ページごと`) so no truncation needed —
            // text_width() handles half-width digits.
            const int text_w = ps3::font::text_width(rows[i].value, font);
            const int x      = screen_w - MARGIN_X - text_w;
            ps3::font::draw_text(x, text_y, rows[i].value, font);
        }

        // Divider line at the bottom of each row, matching settings.
        epd_draw_hline(MARGIN_X, row_top + ITEM_HEIGHT - 1,
                       screen_w - 2 * MARGIN_X, 0x00, fb);
    }
}

TapResult dispatch_tap(int x, int y, Context ctx,
                       const ps3::font::XTEinkFont& font) {
    if (y < ITEM_TOP) return TapResult::None;
    const int bottom = list_bottom(ctx);
    if (y >= bottom) return TapResult::OutsideList;
    const int idx = (y - ITEM_TOP) / ITEM_HEIGHT;
    if (idx < 0 || idx >= row_count(ctx)) return TapResult::None;

    auto& s = ps3::settings::state();

    // Bookshelf row 1 is the split-button サムネイル作成. Same
    // geometry calc as render() so the hit-test bands line up with
    // the drawn buttons.
    if (ctx == Context::Bookshelf && idx == thumb_row_idx(ctx)) {
        const ThumbButtons g = thumb_button_geom(idx, font);
        const bool in_y = (y >= g.btn_y && y < g.btn_y + g.btn_h);
        if (in_y) {
            if (x >= g.btn1_x && x < g.btn1_x + g.btn_w) {
                ESP_LOGI(TAG, "menu: thumbnail regen-all");
                return TapResult::ThumbnailRegenAll;
            }
            if (x >= g.btn2_x && x < g.btn2_x + g.btn_w) {
                ESP_LOGI(TAG, "menu: thumbnail regen-missing");
                return TapResult::ThumbnailRegenMissing;
            }
        }
        // Tap on the row but outside either button — ignore so the
        // user doesn't fire an action by hitting the label area.
        return TapResult::None;
    }

    if (idx == 0) {
        // コントラスト — cycle the per-context level.
        if (ctx == Context::Reading) {
            ps3::settings::cycle_contrast(
                ps3::settings::ContrastContext::Reading);
            ESP_LOGI(TAG, "menu: reading_contrast=%d",
                     s.reading_contrast);
        } else {
            ps3::settings::cycle_contrast(
                ps3::settings::ContrastContext::Bookshelf);
            ESP_LOGI(TAG, "menu: bookshelf_contrast=%d",
                     s.bookshelf_contrast);
        }
        return TapResult::ContrastChanged;
    }
    if (idx == 1 && ctx == Context::Reading) {
        // 完全更新頻度 — cycle through {1, 5, 10, 15, 20}.
        s.full_refresh_pages = next_full_refresh(s.full_refresh_pages);
        ps3::settings::save();
        ESP_LOGI(TAG, "menu: full_refresh_pages=%d", s.full_refresh_pages);
        return TapResult::FullRefreshChanged;
    }
    if (idx == 2 && ctx == Context::Reading) {
        // ページめくり方向 — toggle 左綴じ ↔ 右綴じ. The reading
        // tap handler in main.cpp reads s.right_binding fresh on
        // every MiddleLeft/Right tap so the change takes effect the
        // moment the user closes this overlay; no other state needs
        // to be invalidated.
        s.right_binding = !s.right_binding;
        ps3::settings::save();
        ESP_LOGI(TAG, "menu: right_binding=%d", s.right_binding ? 1 : 0);
        return TapResult::PageDirectionChanged;
    }
    return TapResult::None;
}

}  // namespace ps3::menu
