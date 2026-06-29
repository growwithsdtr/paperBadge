#include "display.hpp"

#include <cstring>

#include <esp_log.h>

extern "C" {
#include <epdiy.h>
#include <epd_highlevel.h>
#include <epd_display.h>
}

extern "C" const EpdBoardDefinition paper_s3_board;

namespace ps3::display {

namespace {
constexpr const char* TAG = "display";
EpdiyHighlevelState s_hl{};
uint8_t* s_fb = nullptr;
int s_w = 0;
int s_h = 0;
bool s_inited = false;
bool s_inverted = false;   // mirrors the most recent set_inverted()
constexpr int kTemperature = 20;
}  // namespace

bool init() {
    if (s_inited) return true;

    epd_set_board(&paper_s3_board);
    // Panel selection: M5Stack docs say ED047TC1 but the bundled epdiy
    // TC1 waveform under-drives the panel on this hardware (visibly
    // pale output). TC2 with its heavier 38-57 phase waveform gives
    // the contrast we want at the cost of a longer flush, so we use
    // TC2 until a better TC1 waveform is available.
    //
    // pclk: 20 MHz, the panel struct's design value. Reachable now
    // that lut.S (ESP32-S3 PIE vector LUT) is back in the build —
    // juicecultus had renamed it to .disabled in the "Paper S3 fixes"
    // commit; we re-enable it locally because the C-only fallback in
    // lut.c can't keep up above ~10 MHz (tearing then crash on the
    // first GC16). See patches/epdiy.patch for the build wiring.
    epd_init(epd_current_board(), &ED047TC2, EPD_OPTIONS_DEFAULT);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    epd_set_lcd_pixel_clock_MHz(20);

    s_hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_hl_set_all_white(&s_hl);
    s_fb = epd_hl_get_framebuffer(&s_hl);
    s_w = epd_rotated_display_width();
    s_h = epd_rotated_display_height();

    epd_poweron();
    epd_fullclear(&s_hl, kTemperature);

    ESP_LOGI(TAG, "epdiy ready: %dx%d, fb=%p", s_w, s_h, s_fb);
    s_inited = true;
    return true;
}

int width() { return s_w; }
int height() { return s_h; }
uint8_t* framebuffer() { return s_fb; }

void clear() {
    if (!s_inited) return;
    epd_hl_set_all_white(&s_hl);
}

void flush(RefreshMode mode) {
    if (!s_inited) return;

    if (mode == RefreshMode::GC16Full) {
        // True physical refresh.
        //
        // The naive "stamp back_fb to all black, then GC16" approach
        // does force every column to be marked dirty, but the GC16
        // waveform LUT is keyed on (back_nibble, front_nibble) — so
        // the resulting drive sequence is "0x0 (black) -> target",
        // applied while the *physical* panel still holds the old
        // image. Mismatched start state leaves visible ghosts.
        //
        // The correct sequence:
        //   1. epd_clear()       drives every electrode through 3
        //                        cycles of black/white inversion,
        //                        leaving the panel physically white
        //                        regardless of front_fb / back_fb.
        //   2. back_fb := 0xFF   so the diff cache matches the
        //                        panel's new physical state.
        //   3. GC16              repaints front_fb from a clean
        //                        white baseline; per-column ageing
        //                        is reset uniformly.
        //
        // front_fb is left untouched so the caller's image survives
        // the refresh. Cost: ~700 ms of visible flicker (the 3
        // inversion cycles), then a normal GC16. Reserved for scene
        // transitions and the periodic 10-page tick.
        const size_t fb_size =
            static_cast<size_t>(epd_width()) * epd_height() / 2;
        epd_clear();
        std::memset(s_hl.back_fb, 0xFF, fb_size);
        epd_hl_update_screen(&s_hl, MODE_GC16, kTemperature);
        return;
    }

    enum EpdDrawMode emode = MODE_GC16;
    switch (mode) {
        case RefreshMode::GC16: emode = MODE_GC16; break;
        case RefreshMode::GL16: emode = MODE_GL16; break;
        case RefreshMode::DU:   emode = MODE_DU;   break;
        case RefreshMode::GC16Full: break;  // handled above
    }
    epd_hl_update_screen(&s_hl, emode, kTemperature);
}

void flush_area(int x, int y, int w, int h, RefreshMode mode) {
    if (!s_inited) return;
    if (w <= 0 || h <= 0) return;

    // Clamp the requested rect to the screen so we never hand
    // epdiy an out-of-bounds area (it would assert / corrupt).
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;

    enum EpdDrawMode emode = MODE_GC16;
    switch (mode) {
        case RefreshMode::GC16: emode = MODE_GC16; break;
        case RefreshMode::GL16: emode = MODE_GL16; break;
        case RefreshMode::DU:   emode = MODE_DU;   break;
        // GC16Full is global by design (epd_clear() touches every
        // electrode). For an area-restricted call we just fall
        // through to GC16; the caller is asking for a popup-sized
        // refresh and shouldn't be paying for a full-screen reset.
        case RefreshMode::GC16Full: emode = MODE_GC16; break;
    }

    EpdRect area{};
    area.x = x;
    area.y = y;
    area.width  = w;
    area.height = h;
    epd_hl_update_area(&s_hl, emode, kTemperature, area);
}

void put_pixel(int x, int y, uint8_t color) {
    if (!s_inited) return;
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) return;
    epd_draw_pixel(x, y, (color & 0x0F) << 4, s_fb);
}

void set_inverted(bool inverted) {
    if (!s_inited) return;
    s_inverted = inverted;
    epd_set_rotation(inverted ? EPD_ROT_PORTRAIT
                              : EPD_ROT_INVERTED_PORTRAIT);
    // s_w / s_h don't change: both rotations are 540×960 portrait.
    // The caller is expected to repaint and tell touch::set_inverted.
}

bool is_inverted() { return s_inverted; }

}  // namespace ps3::display
