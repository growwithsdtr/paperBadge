#include "image_display.hpp"

#include "../hal/display.hpp"
#include "../system/settings.hpp"

#include <cstdlib>
#include <algorithm>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

extern "C" {
#include <epdiy.h>
}

#include "JPEGDEC.h"
#include "PNGdec.h"

#include <new>

namespace ps3::comic {

namespace {
constexpr const char* TAG = "img";

// Pre-dither (Floyd-Steinberg 4 bpp) vs straight 8-bit truncate.
// Both paths produce a 16-level grayscale framebuffer; the dithered
// path is theoretically smoother on tonal regions, but on already-
// quantised manga scans it actually hurts text legibility. Default
// to OFF; expose as a runtime setting later.
constexpr bool kUseDither = false;

// State shared between the JPEGDEC decode() call and our MCU-row
// draw callback. Single-image-at-a-time only.
int s_offset_x = 0;
int s_offset_y = 0;
int s_logical_w = 0;
int s_logical_h = 0;
int s_native_w = 0;
int s_native_h = 0;
uint8_t* s_fb = nullptr;
// Cached panel rotation for the duration of one decode().
ps3::display::Rotation s_rotation = ps3::display::Rotation::InvertedPortrait;

// Black-point lift LUT, rebuilt at the start of every decode from
// the contrast level matching the caller's ContrastContext. The
// grayscale / PNG / BMP draw callbacks index into it before
// reducing to 4 bpp, so the inner loop stays branch-free.
uint8_t s_contrast_lut[256];

// PNG decode shares the same set of sentinel statics as the JPEG
// path, but the PNGdec API delivers a full image row per callback
// (rather than an MCU block), so the row-RGB565 buffer below stays
// alive only for the duration of one decode pass. Sized for the
// widest sane PNG we'd expect in a CBZ after host preprocessing.
constexpr int  PNG_ROW_BUF_PIXELS = 2048;
uint16_t s_png_row[PNG_ROW_BUF_PIXELS];
PNG*     s_png_obj = nullptr;

// Accumulated wall-time spent inside the JPEGDEC draw callback for
// the current decode. Reset by display_jpeg() before invoking decode,
// read after.
int64_t s_callback_total_us = 0;
bool s_png_scaled = false;
int s_png_src_w = 0;
int s_png_src_h = 0;
int s_png_dst_w = 0;
int s_png_dst_h = 0;
bool s_jpeg_scaled = false;
int s_jpeg_dec_w = 0;
int s_jpeg_dec_h = 0;
int s_jpeg_dst_w = 0;
int s_jpeg_dst_h = 0;

bool logical_to_native(int lx, int ly, int* nx_out, int* ny_out) {
    if (lx < 0 || ly < 0 || lx >= s_logical_w || ly >= s_logical_h) return false;
    const int max_native_x = s_native_w - 1;
    const int max_native_y = s_native_h - 1;
    int nx = lx;
    int ny = ly;
    switch (s_rotation) {
        case ps3::display::Rotation::Landscape:
            nx = lx;
            ny = ly;
            break;
        case ps3::display::Rotation::Portrait:
            nx = max_native_x - ly;
            ny = lx;
            break;
        case ps3::display::Rotation::InvertedLandscape:
            nx = max_native_x - lx;
            ny = max_native_y - ly;
            break;
        case ps3::display::Rotation::InvertedPortrait:
            nx = ly;
            ny = max_native_y - lx;
            break;
    }
    if (nx < 0 || ny < 0 || nx >= s_native_w || ny >= s_native_h) return false;
    if (nx_out) *nx_out = nx;
    if (ny_out) *ny_out = ny;
    return true;
}

void put_gray4_logical(int lx, int ly, uint8_t gray4) {
    int nx = 0;
    int ny = 0;
    if (!logical_to_native(lx, ly, &nx, &ny)) return;
    uint8_t* fb_byte = s_fb + ny * (s_native_w / 2) + (nx >> 1);
    if (nx & 1) {
        *fb_byte = (*fb_byte & 0x0F) | ((gray4 & 0x0F) << 4);
    } else {
        *fb_byte = (*fb_byte & 0xF0) | (gray4 & 0x0F);
    }
}

void put_luma_logical(int lx, int ly, int luma) {
    const uint8_t corrected = s_contrast_lut[luma & 0xFF];
    put_gray4_logical(lx, ly, corrected >> 4);
}

// EIGHT_BIT_GRAYSCALE callback: pPixels is `iWidth` bytes of 8-bit
// grayscale per row, MCU-block sized (16x16 typically).
int draw_callback_grayscale(JPEGDRAW* p) {
    const int64_t t0 = esp_timer_get_time();
    const uint8_t* px = reinterpret_cast<const uint8_t*>(p->pPixels);

    for (int row = 0; row < p->iHeight; ++row) {
        const int ly = s_offset_y + p->y + row;
        if (!s_jpeg_scaled && (ly < 0 || ly >= s_logical_h)) continue;

        // Clip the row to in-bounds cols (lx in [0, s_logical_w)).
        int col_min = 0;
        int col_max = p->iWidth;
        if (!s_jpeg_scaled) {
            col_min = -(s_offset_x + p->x);
            if (col_min < 0) col_min = 0;
            col_max = s_logical_w - (s_offset_x + p->x);
            if (col_max > p->iWidth) col_max = p->iWidth;
        }
        if (col_min >= col_max) continue;

        const uint8_t* src = px + row * p->iWidth + col_min;
        const int n = col_max - col_min;
        for (int i = 0; i < n; ++i) {
            const int sx = p->x + col_min + i;
            if (s_jpeg_scaled && s_jpeg_dec_w > 0 && s_jpeg_dec_h > 0) {
                const int sy = p->y + row;
                int dx0 = s_offset_x + (sx * s_jpeg_dst_w) / s_jpeg_dec_w;
                int dx1 = s_offset_x + ((sx + 1) * s_jpeg_dst_w + s_jpeg_dec_w - 1) / s_jpeg_dec_w;
                int dy0 = s_offset_y + (sy * s_jpeg_dst_h) / s_jpeg_dec_h;
                int dy1 = s_offset_y + ((sy + 1) * s_jpeg_dst_h + s_jpeg_dec_h - 1) / s_jpeg_dec_h;
                if (dx1 <= dx0) dx1 = dx0 + 1;
                if (dy1 <= dy0) dy1 = dy0 + 1;
                for (int dy = dy0; dy < dy1; ++dy) {
                    for (int dx = dx0; dx < dx1; ++dx) {
                        put_luma_logical(dx, dy, src[i]);
                    }
                }
            } else {
                put_luma_logical(s_offset_x + sx, ly, src[i]);
            }
        }
    }
    s_callback_total_us += esp_timer_get_time() - t0;
    return 1;
}

// FOUR_BIT_DITHERED callback. JPEGDEC packs:
//   - row stride: (p->iWidth + 1) / 2 bytes
//   - per byte: high nibble = even col, low nibble = odd col
//   - nibble value: 0 (black) .. 15 (white), Floyd-Steinberg
//     pre-dithered
//   - one callback per FULL MCU row (iMCUCount = cx) so dither
//     diffusion can run across the whole row.
//
// Same hoisting strategy as draw_callback_grayscale, with the same
// rotation-aware step direction.
int draw_callback_dithered(JPEGDRAW* p) {
    const uint8_t* px = reinterpret_cast<const uint8_t*>(p->pPixels);
    const int src_pitch = (p->iWidth + 1) / 2;

    for (int row = 0; row < p->iHeight; ++row) {
        const int ly = s_offset_y + p->y + row;
        if (!s_jpeg_scaled && (ly < 0 || ly >= s_logical_h)) continue;

        int col_min = 0;
        int col_max = p->iWidth;
        if (!s_jpeg_scaled) {
            col_min = -(s_offset_x + p->x);
            if (col_min < 0) col_min = 0;
            col_max = s_logical_w - (s_offset_x + p->x);
            if (col_max > p->iWidth) col_max = p->iWidth;
        }
        if (col_min >= col_max) continue;

        const uint8_t* src_row = px + row * src_pitch;
        for (int col = col_min; col < col_max; ++col) {
            const uint8_t pair = src_row[col >> 1];
            const uint8_t g = (col & 1) ? (pair & 0x0F) : (pair >> 4);
            const int sx = p->x + col;
            if (s_jpeg_scaled && s_jpeg_dec_w > 0 && s_jpeg_dec_h > 0) {
                const int sy = p->y + row;
                int dx0 = s_offset_x + (sx * s_jpeg_dst_w) / s_jpeg_dec_w;
                int dx1 = s_offset_x + ((sx + 1) * s_jpeg_dst_w + s_jpeg_dec_w - 1) / s_jpeg_dec_w;
                int dy0 = s_offset_y + (sy * s_jpeg_dst_h) / s_jpeg_dec_h;
                int dy1 = s_offset_y + ((sy + 1) * s_jpeg_dst_h + s_jpeg_dec_h - 1) / s_jpeg_dec_h;
                if (dx1 <= dx0) dx1 = dx0 + 1;
                if (dy1 <= dy0) dy1 = dy0 + 1;
                for (int dy = dy0; dy < dy1; ++dy) {
                    for (int dx = dx0; dx < dx1; ++dx) {
                        put_gray4_logical(dx, dy, g);
                    }
                }
            } else {
                put_gray4_logical(s_offset_x + sx, ly, g);
            }
        }
    }
    return 1;
}

}  // namespace

bool display_jpeg_view(const uint8_t* data, size_t size,
                       ImageFit fit,
                       int slice_index,
                       int* out_slice_count,
                       uint8_t* dest_fb,
                       ps3::settings::ContrastContext ctx) {
    JPEGDEC jpeg;

    JPEG_DRAW_CALLBACK* cb = kUseDither ? draw_callback_dithered
                                        : draw_callback_grayscale;
    if (!jpeg.openRAM(const_cast<uint8_t*>(data), static_cast<int>(size),
                      cb)) {
        ESP_LOGE(TAG, "openRAM failed (last error %d)", jpeg.getLastError());
        return false;
    }

    const int src_w = jpeg.getWidth();
    const int src_h = jpeg.getHeight();
    const int screen_w = ps3::display::width();
    const int screen_h = ps3::display::height();

    int dst_w = screen_w;
    int dst_h = screen_h;
    if (fit == ImageFit::Width) {
        dst_w = screen_w;
        dst_h = std::max(1, (src_h * screen_w) / std::max(1, src_w));
    } else if (fit == ImageFit::Height) {
        dst_h = screen_h;
        dst_w = std::max(1, (src_w * screen_h) / std::max(1, src_h));
    } else {
        if (src_w <= screen_w && src_h <= screen_h) {
            dst_w = src_w;
            dst_h = src_h;
        } else if (static_cast<int64_t>(src_w) * screen_h >
                   static_cast<int64_t>(src_h) * screen_w) {
            dst_w = screen_w;
            dst_h = std::max(1, (src_h * screen_w) / std::max(1, src_w));
        } else {
            dst_h = screen_h;
            dst_w = std::max(1, (src_w * screen_h) / std::max(1, src_h));
        }
    }

    int slice_count = dst_h > screen_h ? (dst_h + screen_h - 1) / screen_h : 1;
    if (slice_count < 1) slice_count = 1;
    if (out_slice_count) *out_slice_count = slice_count;
    if (slice_index < 0) slice_index = 0;
    if (slice_index >= slice_count) slice_index = slice_count - 1;
    int slice_y = 0;
    if (slice_count > 1) {
        slice_y = slice_index * screen_h;
        const int max_slice_y = std::max(0, dst_h - screen_h);
        if (slice_y > max_slice_y) slice_y = max_slice_y;
    }

    int scale = 1;
    int scale_flag = 0;
    if (src_w / 8 >= dst_w && src_h / 8 >= dst_h) {
        scale = 8; scale_flag = JPEG_SCALE_EIGHTH;
    } else if (src_w / 4 >= dst_w && src_h / 4 >= dst_h) {
        scale = 4; scale_flag = JPEG_SCALE_QUARTER;
    } else if (src_w / 2 >= dst_w && src_h / 2 >= dst_h) {
        scale = 2; scale_flag = JPEG_SCALE_HALF;
    }

    const int out_w = src_w / scale;
    const int out_h = src_h / scale;

    s_offset_x = (screen_w - dst_w) / 2;
    s_offset_y = (dst_h <= screen_h) ? (screen_h - dst_h) / 2 : -slice_y;
    s_logical_w = screen_w;
    s_logical_h = screen_h;
    s_native_w = epd_width();
    s_native_h = epd_height();
    s_fb = dest_fb ? dest_fb : ps3::display::framebuffer();
    s_rotation = ps3::display::rotation();
    s_jpeg_scaled = true;
    s_jpeg_dec_w = out_w;
    s_jpeg_dec_h = out_h;
    s_jpeg_dst_w = dst_w;
    s_jpeg_dst_h = dst_h;
    ps3::settings::build_contrast_lut(s_contrast_lut, ctx);

    s_callback_total_us = 0;
    const int64_t t_decode_0 = esp_timer_get_time();

    int rc = 0;
    if (kUseDither) {
        // JPEGDEC needs an external scratch buffer for FOUR_BIT_DITHERED.
        // Sized for the worst case (full screen width row group at the
        // largest MCU height of 16 px), used both as 8-bit input and
        // 4 bpp packed output. Internal RAM for cache locality.
        constexpr size_t kDitherBufBytes = 540 * 32;
        auto* dither = static_cast<uint8_t*>(
            heap_caps_malloc(kDitherBufBytes,
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!dither) {
            ESP_LOGE(TAG, "dither buffer alloc (%u bytes) failed",
                     (unsigned)kDitherBufBytes);
            jpeg.close();
            return false;
        }
        jpeg.setPixelType(FOUR_BIT_DITHERED);
        rc = jpeg.decodeDither(dither, scale_flag);
        std::free(dither);
    } else {
        jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
        rc = jpeg.decode(0, 0, scale_flag);
    }
    jpeg.close();
    s_jpeg_scaled = false;
    s_jpeg_dec_w = s_jpeg_dec_h = s_jpeg_dst_w = s_jpeg_dst_h = 0;

    if (rc != 1) {
        ESP_LOGE(TAG, "decode failed rc=%d, last error %d",
                 rc, jpeg.getLastError());
        return false;
    }

    const int64_t t_decode_1 = esp_timer_get_time();
    ESP_LOGI(TAG,
             "JPEG %dx%d->%dx%d decode=%dx%d (1/%d) slice=%d/%d total=%lld ms (cb=%lld ms / pure=%lld ms)%s",
             src_w, src_h, dst_w, dst_h, out_w, out_h, scale,
             slice_index + 1, slice_count,
             (long long)((t_decode_1 - t_decode_0) / 1000),
             (long long)(s_callback_total_us / 1000),
             (long long)((t_decode_1 - t_decode_0 - s_callback_total_us) / 1000),
             kUseDither ? " [dither]" : "");
    return true;
}

bool display_jpeg(const uint8_t* data, size_t size, uint8_t* dest_fb,
                  ps3::settings::ContrastContext ctx) {
    int slices = 1;
    return display_jpeg_view(data, size, ImageFit::Page, 0, &slices, dest_fb, ctx);
}

// --- PNG path ---------------------------------------------------------
//
// PNGdec calls back once per fully decoded image row (regardless of
// the on-disk pixel format). We funnel everything through
// getLineAsRGB565() so this code can stay format-agnostic
// (TRUECOLOR / GRAYSCALE / INDEXED / *_ALPHA all collapse to RGB565
// with white-background alpha compositing). From RGB565 we reduce
// to 4-bit grayscale with BT.601 luma weights.

namespace {

void put_png_luma_pixel(int lx, int ly, int luma) {
    put_luma_logical(lx, ly, luma);
}

int rgb565_luma(uint16_t rgb) {
    const int R = (rgb >> 11)         << 3;  // 5→8
    const int G = ((rgb >> 5) & 0x3F) << 2;  // 6→8
    const int B = (rgb & 0x1F)        << 3;  // 5→8
    return (R * 77 + G * 150 + B * 29) >> 8;  // BT.601
}

int draw_callback_png(PNGDRAW* p) {
    if (!s_png_obj) return 0;
    s_png_obj->getLineAsRGB565(p, s_png_row, PNG_RGB565_LITTLE_ENDIAN,
                               0xFFFFFFFFu);  // white background

    if (s_png_scaled) {
        if (p->iWidth > PNG_ROW_BUF_PIXELS || s_png_src_w <= 0 || s_png_src_h <= 0 ||
            s_png_dst_w <= 0 || s_png_dst_h <= 0) {
            return 1;
        }
        const int dst_y0 = s_offset_y + (p->y * s_png_dst_h) / s_png_src_h;
        int dst_y1 = s_offset_y + ((p->y + 1) * s_png_dst_h) / s_png_src_h;
        if (dst_y1 <= dst_y0) dst_y1 = dst_y0 + 1;
        for (int dy = dst_y0; dy < dst_y1; ++dy) {
            for (int dx = 0; dx < s_png_dst_w; ++dx) {
                const int sx = (dx * s_png_src_w) / s_png_dst_w;
                put_png_luma_pixel(s_offset_x + dx, dy, rgb565_luma(s_png_row[sx]));
            }
        }
        return 1;
    }

    const int ly = s_offset_y + p->y;
    if (ly < 0 || ly >= s_logical_h) return 1;

    int col_min = -s_offset_x;
    if (col_min < 0) col_min = 0;
    int col_max = s_logical_w - s_offset_x;
    if (col_max > p->iWidth) col_max = p->iWidth;
    if (col_max > PNG_ROW_BUF_PIXELS) col_max = PNG_ROW_BUF_PIXELS;
    if (col_min >= col_max) return 1;

    for (int col = col_min; col < col_max; ++col) {
        put_png_luma_pixel(s_offset_x + col, ly, rgb565_luma(s_png_row[col]));
    }
    return 1;
}

}  // namespace

bool png_size(const uint8_t* data, size_t size, int* out_w, int* out_h) {
    // PNG layout:
    //   [ 0.. 7] : signature 89 50 4E 47 0D 0A 1A 0A
    //   [ 8..11] : IHDR chunk length (== 13)
    //   [12..15] : "IHDR"
    //   [16..19] : width  (big-endian uint32)
    //   [20..23] : height (big-endian uint32)
    if (!data || size < 24 || !out_w || !out_h) return false;
    if (data[0] != 0x89 || data[1] != 'P' ||
        data[2] != 'N'  || data[3] != 'G') {
        return false;
    }
    *out_w = (int)((static_cast<uint32_t>(data[16]) << 24) |
                   (static_cast<uint32_t>(data[17]) << 16) |
                   (static_cast<uint32_t>(data[18]) << 8)  |
                    static_cast<uint32_t>(data[19]));
    *out_h = (int)((static_cast<uint32_t>(data[20]) << 24) |
                   (static_cast<uint32_t>(data[21]) << 16) |
                   (static_cast<uint32_t>(data[22]) << 8)  |
                    static_cast<uint32_t>(data[23]));
    return true;
}

namespace {

// Shared decode core. `dst_x` / `dst_y` are the on-screen logical
// coordinates of the image's top-left corner.
bool display_png_impl(const uint8_t* data, size_t size,
                      int dst_x, int dst_y,
                      uint8_t* dest_fb,
                      ps3::settings::ContrastContext ctx) {
    // PNGdec is a heavyweight object (state machine + zlib window
    // buffers); allocate it on PSRAM rather than the caller's stack.
    auto* png = static_cast<PNG*>(
        heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM));
    if (!png) {
        ESP_LOGE(TAG, "alloc PNG (%u bytes) failed",
                 (unsigned)sizeof(PNG));
        return false;
    }
    new (png) PNG();

    if (png->openRAM(const_cast<uint8_t*>(data), static_cast<int>(size),
                     draw_callback_png) != PNG_SUCCESS) {
        ESP_LOGE(TAG, "PNG openRAM failed (last error %d)",
                 png->getLastError());
        png->~PNG();
        std::free(png);
        return false;
    }

    const int src_w   = png->getWidth();
    const int src_h   = png->getHeight();

    s_offset_x = dst_x;
    s_offset_y = dst_y;
    s_logical_w = ps3::display::width();
    s_logical_h = ps3::display::height();
    s_native_w = epd_width();
    s_native_h = epd_height();
    s_fb = dest_fb ? dest_fb : ps3::display::framebuffer();
    s_rotation = ps3::display::rotation();
    s_png_obj = png;
    ps3::settings::build_contrast_lut(s_contrast_lut, ctx);

    const int64_t t_decode_0 = esp_timer_get_time();
    const int rc = png->decode(nullptr, 0);
    const int64_t t_decode_1 = esp_timer_get_time();

    s_png_obj = nullptr;
    png->close();
    png->~PNG();
    std::free(png);

    if (rc != PNG_SUCCESS) {
        ESP_LOGE(TAG, "PNG decode failed rc=%d", rc);
        return false;
    }

    ESP_LOGI(TAG, "PNG %dx%d @ (%d,%d) total=%lld ms",
             src_w, src_h, dst_x, dst_y,
             (long long)((t_decode_1 - t_decode_0) / 1000));
    return true;
}

}  // namespace

bool display_png(const uint8_t* data, size_t size, uint8_t* dest_fb,
                 ps3::settings::ContrastContext ctx) {
    int src_w = 0, src_h = 0;
    if (!png_size(data, size, &src_w, &src_h)) return false;
    const int dst_x = (ps3::display::width()  - src_w) / 2;
    const int dst_y = (ps3::display::height() - src_h) / 2;
    return display_png_impl(data, size, dst_x, dst_y, dest_fb, ctx);
}

bool display_png_at(const uint8_t* data, size_t size,
                    int dst_x, int dst_y, uint8_t* dest_fb,
                    ps3::settings::ContrastContext ctx) {
    return display_png_impl(data, size, dst_x, dst_y, dest_fb, ctx);
}

bool display_png_fit(const uint8_t* data, size_t size,
                     int max_w, int max_h, uint8_t* dest_fb,
                     ps3::settings::ContrastContext ctx) {
    int src_w = 0, src_h = 0;
    if (!png_size(data, size, &src_w, &src_h)) return false;
    if (src_w <= 0 || src_h <= 0 || max_w <= 0 || max_h <= 0) return false;
    if (src_w > PNG_ROW_BUF_PIXELS) {
        ESP_LOGE(TAG, "PNG fit source width %d exceeds row buffer %d",
                 src_w, PNG_ROW_BUF_PIXELS);
        return false;
    }

    int dst_w = max_w;
    int dst_h = (src_h * max_w) / src_w;
    if (dst_h > max_h) {
        dst_h = max_h;
        dst_w = (src_w * max_h) / src_h;
    }
    if (dst_w <= 0 || dst_h <= 0) return false;

    s_png_scaled = true;
    s_png_src_w = src_w;
    s_png_src_h = src_h;
    s_png_dst_w = dst_w;
    s_png_dst_h = dst_h;
    const int dst_x = (ps3::display::width() - dst_w) / 2;
    const int dst_y = (ps3::display::height() - dst_h) / 2;
    const bool ok = display_png_impl(data, size, dst_x, dst_y, dest_fb, ctx);
    s_png_scaled = false;
    s_png_src_w = s_png_src_h = s_png_dst_w = s_png_dst_h = 0;
    return ok;
}

bool display_png_view(const uint8_t* data, size_t size,
                      ImageFit fit,
                      int slice_index,
                      int* out_slice_count,
                      uint8_t* dest_fb,
                      ps3::settings::ContrastContext ctx) {
    int src_w = 0, src_h = 0;
    if (!png_size(data, size, &src_w, &src_h)) return false;
    if (src_w <= 0 || src_h <= 0) return false;
    if (src_w > PNG_ROW_BUF_PIXELS) {
        ESP_LOGE(TAG, "PNG view source width %d exceeds row buffer %d",
                 src_w, PNG_ROW_BUF_PIXELS);
        return false;
    }

    const int screen_w = ps3::display::width();
    const int screen_h = ps3::display::height();
    int dst_w = screen_w;
    int dst_h = screen_h;
    if (fit == ImageFit::Width) {
        dst_w = screen_w;
        dst_h = std::max(1, (src_h * screen_w) / std::max(1, src_w));
    } else if (fit == ImageFit::Height) {
        dst_h = screen_h;
        dst_w = std::max(1, (src_w * screen_h) / std::max(1, src_h));
    } else {
        if (src_w <= screen_w && src_h <= screen_h) {
            dst_w = src_w;
            dst_h = src_h;
        } else if (static_cast<int64_t>(src_w) * screen_h >
                   static_cast<int64_t>(src_h) * screen_w) {
            dst_w = screen_w;
            dst_h = std::max(1, (src_h * screen_w) / std::max(1, src_w));
        } else {
            dst_h = screen_h;
            dst_w = std::max(1, (src_w * screen_h) / std::max(1, src_h));
        }
    }

    int slice_count = dst_h > screen_h ? (dst_h + screen_h - 1) / screen_h : 1;
    if (slice_count < 1) slice_count = 1;
    if (out_slice_count) *out_slice_count = slice_count;
    if (slice_index < 0) slice_index = 0;
    if (slice_index >= slice_count) slice_index = slice_count - 1;
    int slice_y = 0;
    if (slice_count > 1) {
        slice_y = slice_index * screen_h;
        const int max_slice_y = std::max(0, dst_h - screen_h);
        if (slice_y > max_slice_y) slice_y = max_slice_y;
    }

    s_png_scaled = true;
    s_png_src_w = src_w;
    s_png_src_h = src_h;
    s_png_dst_w = dst_w;
    s_png_dst_h = dst_h;
    const int dst_x = (screen_w - dst_w) / 2;
    const int dst_y = (dst_h <= screen_h) ? (screen_h - dst_h) / 2 : -slice_y;
    const bool ok = display_png_impl(data, size, dst_x, dst_y, dest_fb, ctx);
    s_png_scaled = false;
    s_png_src_w = s_png_src_h = s_png_dst_w = s_png_dst_h = 0;
    if (ok) {
        ESP_LOGI(TAG, "PNG view %dx%d->%dx%d slice=%d/%d",
                 src_w, src_h, dst_w, dst_h, slice_index + 1, slice_count);
    }
    return ok;
}

// --- BMP path ---------------------------------------------------------
//
// Bog-standard Windows BMP loader, only what we need for a loading
// splash: BITMAPFILEHEADER + BITMAPINFOHEADER + raw pixel rows.
// Supports 24 / 32 bpp BGR(A) and 8 bpp paletted (the palette can be
// greyscale or colour; we collapse via BT.601 luma either way).
// Other variants are rejected with a log line.

namespace {

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// Reduce 8-bit RGB to a 4-bit grayscale nibble using BT.601, with
// the active contrast curve applied in 8-bit space first. Caller
// must have populated s_contrast_lut for the current decode.
inline uint8_t rgb_to_gray4(uint8_t R, uint8_t G, uint8_t B) {
    const int luma = (R * 77 + G * 150 + B * 29) >> 8;
    return static_cast<uint8_t>(s_contrast_lut[luma] >> 4);
}

}  // namespace

bool display_bmp(const uint8_t* data, size_t size, uint8_t* dest_fb,
                 ps3::settings::ContrastContext ctx) {
    if (!data || size < 54) {
        ESP_LOGE(TAG, "BMP too small (%u bytes)", (unsigned)size);
        return false;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        ESP_LOGE(TAG, "not a BMP (magic %02x %02x)", data[0], data[1]);
        return false;
    }

    const uint32_t pixel_offset = rd32(data + 10);
    const uint32_t info_size    = rd32(data + 14);
    const int32_t  width_s      = static_cast<int32_t>(rd32(data + 18));
    const int32_t  height_s     = static_cast<int32_t>(rd32(data + 22));
    const uint16_t bpp          = rd16(data + 28);
    const uint32_t compression  = rd32(data + 30);

    if (info_size < 40 || pixel_offset >= size) {
        ESP_LOGE(TAG, "BMP header out of range");
        return false;
    }
    if (compression != 0 /*BI_RGB*/ && compression != 3 /*BI_BITFIELDS*/) {
        ESP_LOGE(TAG, "BMP compression %u not supported",
                 (unsigned)compression);
        return false;
    }
    if (bpp != 24 && bpp != 32 && bpp != 8) {
        ESP_LOGE(TAG, "BMP %u bpp not supported", (unsigned)bpp);
        return false;
    }

    const bool    top_down = height_s < 0;
    const int32_t height   = top_down ? -height_s : height_s;
    const int32_t width    = width_s;
    if (width <= 0 || height <= 0) {
        ESP_LOGE(TAG, "BMP invalid dims %dx%d", width, height);
        return false;
    }

    // Per-row stride is rounded up to a 4-byte boundary in the BMP
    // pixel array (regardless of bpp).
    const int row_bytes  = (width * bpp + 7) / 8;
    const int row_stride = (row_bytes + 3) & ~3;
    const size_t pixel_array_size =
        static_cast<size_t>(row_stride) * static_cast<size_t>(height);
    if (pixel_offset + pixel_array_size > size) {
        ESP_LOGE(TAG, "BMP pixel data truncated");
        return false;
    }

    // 8 bpp uses a 256-entry palette right after the info header.
    // Each palette entry is BGR + reserved, 4 bytes total.
    const uint8_t* palette = nullptr;
    if (bpp == 8) {
        palette = data + 14 /*FILEHEADER*/ + info_size;
        if (palette + 256 * 4 > data + pixel_offset) {
            ESP_LOGE(TAG, "BMP palette overlaps pixel data");
            return false;
        }
    }

    const int screen_w = ps3::display::width();
    const int screen_h = ps3::display::height();
    const int offset_x = (screen_w - width) / 2;
    const int offset_y = (screen_h - height) / 2;

    s_logical_w = screen_w;
    s_logical_h = screen_h;
    s_native_w = epd_width();
    s_native_h = epd_height();
    s_fb = dest_fb ? dest_fb : ps3::display::framebuffer();
    s_rotation = ps3::display::rotation();

    const uint8_t* px_base = data + pixel_offset;
    const int      bytes_per_pixel = bpp / 8;

    // rgb_to_gray4() reads s_contrast_lut, so populate it once for
    // the duration of this BMP paint pass.
    ps3::settings::build_contrast_lut(s_contrast_lut, ctx);

    for (int row = 0; row < height; ++row) {
        const int ly = offset_y + row;
        if (ly < 0 || ly >= screen_h) continue;

        // BMP rows default to bottom-up; top_down flips the order.
        const int     src_row = top_down ? row : (height - 1 - row);
        const uint8_t* src    = px_base + src_row * row_stride;

        int col_min = -offset_x;
        if (col_min < 0) col_min = 0;
        int col_max = screen_w - offset_x;
        if (col_max > width) col_max = width;
        if (col_min >= col_max) continue;

        for (int col = col_min; col < col_max; ++col) {
            uint8_t R, G, B;
            if (bpp == 24 || bpp == 32) {
                // BMP stores BGR(A) — ignore A on 32 bpp.
                const uint8_t* p = src + col * bytes_per_pixel;
                B = p[0]; G = p[1]; R = p[2];
            } else {  // bpp == 8
                const uint8_t  idx = src[col];
                const uint8_t* pal = palette + idx * 4;
                B = pal[0]; G = pal[1]; R = pal[2];
            }
            const uint8_t gray4 = rgb_to_gray4(R, G, B);

            const int lx     = offset_x + col;
            put_gray4_logical(lx, ly, gray4);
        }
    }
    ESP_LOGI(TAG, "BMP %dx%d %u bpp painted", width, height, (unsigned)bpp);
    return true;
}

}  // namespace ps3::comic
