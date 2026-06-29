#include "thumbnail.hpp"

#include "../comic/cbz_book.hpp"
#include "../hal/display.hpp"
#include "../system/settings.hpp"
#include "library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <new>

#include <esp_heap_caps.h>
#include <esp_log.h>

extern "C" {
#include <epdiy.h>
#include "local/miniz.h"
}

#include "JPEGDEC.h"

namespace ps3::library {

namespace {
constexpr const char* TAG = "thumb";

// Shared state for the JPEGDEC draw callback. Only one generate_*
// call is in flight at a time (sequential, on the main task), so a
// few file-scope statics are simpler than threading state through
// JPEGDEC's user pointer.
uint8_t* s_decode_buf = nullptr;
int      s_decode_w   = 0;
int      s_decode_h   = 0;

int draw_callback_8bit(JPEGDRAW* p) {
    if (!s_decode_buf) return 0;
    const uint8_t* px = reinterpret_cast<const uint8_t*>(p->pPixels);
    for (int row = 0; row < p->iHeight; ++row) {
        const int dy = p->y + row;
        if (dy < 0 || dy >= s_decode_h) continue;
        const uint8_t* src = px + row * p->iWidth;
        uint8_t* dst       = s_decode_buf + dy * s_decode_w;
        const int cols = std::min(p->iWidth, s_decode_w - p->x);
        if (p->x < 0 || cols <= 0) continue;
        std::memcpy(dst + p->x, src, cols);
    }
    return 1;
}

}  // namespace

ThumbDimensions generate_thumbnail(const char* cbz_path,
                                    const char* dest_path) {
    ThumbDimensions result{0, 0};
    if (!cbz_path || !dest_path) return result;

    // ---- 1. Open zip ----
    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_file(&zip, cbz_path, 0)) {
        ESP_LOGE(TAG, "open %s failed", cbz_path);
        return result;
    }

    // ---- 2. Find lex-smallest JPEG entry ----
    // The decoder below is JPEGDEC, so we must mirror the JPEG-only
    // filter that cbz_book::open() applies. Without this, a CBZ
    // whose lex-smallest entry is "cover.png" or "ComicInfo.xml"
    // would silently fail thumbnail generation (decode returns
    // {0,0}) even though the book itself opens fine because there
    // is a JPEG further down the entry list.
    int  chosen = -1;
    char chosen_name[256] = {};
    const int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_uncomp_size == 0) continue;
        const size_t len = std::strlen(st.m_filename);
        if (len == 0 || st.m_filename[len - 1] == '/') continue;
        if (!ps3::comic::has_jpeg_extension(st.m_filename, len)) continue;
        if (chosen < 0 || std::strcmp(st.m_filename, chosen_name) < 0) {
            chosen = i;
            std::strncpy(chosen_name, st.m_filename, sizeof(chosen_name) - 1);
            chosen_name[sizeof(chosen_name) - 1] = '\0';
        }
    }
    if (chosen < 0) {
        ESP_LOGE(TAG, "no jpeg entries in %s", cbz_path);
        mz_zip_reader_end(&zip);
        return result;
    }

    // ---- 3. Extract to heap ----
    size_t jpg_size = 0;
    void*  jpg_data = mz_zip_reader_extract_to_heap(&zip, chosen, &jpg_size, 0);
    mz_zip_reader_end(&zip);
    if (!jpg_data) {
        ESP_LOGE(TAG, "extract %s failed", chosen_name);
        return result;
    }

    // ---- 4. Decode at 1/4 into an 8-bit grayscale buffer ----
    // JPEGDEC's internal state is large (an 8 KB+ MCU buffer plus
    // dequant tables) — keeping it on the stack overflowed the 32 KB
    // main-task stack when this was called from deep in the menu
    // dispatch. Allocate on the heap (PSRAM) instead.
    //
    // Earlier versions decoded at 1/8 directly. JPEGDEC's 1/8 path
    // skips the IDCT and uses just the DC coefficient of each 8x8
    // block, which gave a noticeably blocky thumbnail. We now decode
    // at 1/4 (full IDCT) and store the result verbatim — earlier
    // experiments box-filtered to 1/8 to keep file size small, but
    // the extra detail at 1/4 is worth the ~4x file size growth
    // (`(width+1)/2 * height` packed bytes per thumb, roughly 100KB
    // for a 1500x2200 page) once you factor in bilinear blit at
    // display time.
    auto* jpeg = static_cast<JPEGDEC*>(
        heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM));
    if (!jpeg) {
        ESP_LOGE(TAG, "alloc JPEGDEC (%u bytes) failed",
                 (unsigned)sizeof(JPEGDEC));
        std::free(jpg_data);
        return result;
    }
    new (jpeg) JPEGDEC();
    if (!jpeg->openRAM(reinterpret_cast<uint8_t*>(jpg_data),
                       static_cast<int>(jpg_size), draw_callback_8bit)) {
        ESP_LOGE(TAG, "JPEGDEC open failed");
        jpeg->~JPEGDEC();
        std::free(jpeg);
        std::free(jpg_data);
        return result;
    }
    const int src_w = jpeg->getWidth();
    const int src_h = jpeg->getHeight();
    const int out_w = src_w / 4;
    const int out_h = src_h / 4;
    if (out_w <= 0 || out_h <= 0) {
        ESP_LOGE(TAG, "1/4 of %dx%d gave zero", src_w, src_h);
        jpeg->close();
        jpeg->~JPEGDEC();
        std::free(jpeg);
        std::free(jpg_data);
        return result;
    }

    const size_t buf_size = static_cast<size_t>(out_w) * out_h;
    auto* buf = static_cast<uint8_t*>(
        heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
    if (!buf) {
        ESP_LOGE(TAG, "alloc 1/4 buf (%u bytes) failed", (unsigned)buf_size);
        jpeg->close();
        jpeg->~JPEGDEC();
        std::free(jpeg);
        std::free(jpg_data);
        return result;
    }
    std::memset(buf, 0xFF, buf_size);  // white

    s_decode_buf = buf;
    s_decode_w   = out_w;
    s_decode_h   = out_h;

    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    const int rc = jpeg->decode(0, 0, JPEG_SCALE_QUARTER);
    jpeg->close();
    jpeg->~JPEGDEC();
    std::free(jpeg);
    std::free(jpg_data);
    s_decode_buf = nullptr;
    s_decode_w   = s_decode_h = 0;

    if (rc != 1) {
        ESP_LOGE(TAG, "decode failed rc=%d", rc);
        std::free(buf);
        return result;
    }

    // ---- 5. Pack the 1/4 buffer to 4 bpp ----
    // Mid-bin rounding (`(v + 8) >> 4` clamped at 15) avoids the
    // slight darkward bias of a plain `v >> 4` truncate. Packing
    // convention: high nibble = even column, low nibble = odd —
    // matches blit_thumbnail's nibble extraction.
    const int    packed_pitch = (out_w + 1) / 2;
    const size_t packed_size  = static_cast<size_t>(packed_pitch) * out_h;
    auto* packed = static_cast<uint8_t*>(
        heap_caps_malloc(packed_size, MALLOC_CAP_SPIRAM));
    if (!packed) {
        ESP_LOGE(TAG, "alloc packed buf (%u bytes) failed",
                 (unsigned)packed_size);
        std::free(buf);
        return result;
    }
    std::memset(packed, 0xFF, packed_size);

    for (int y = 0; y < out_h; ++y) {
        const uint8_t* src = buf + y * out_w;
        uint8_t*       dst = packed + y * packed_pitch;
        for (int x = 0; x < out_w; x += 2) {
            const uint8_t e = src[x];
            const uint8_t hi = (e >= 248) ? 0xF
                                          : static_cast<uint8_t>((e + 8) >> 4);
            uint8_t lo = 0xF;
            if (x + 1 < out_w) {
                const uint8_t o = src[x + 1];
                lo = (o >= 248) ? 0xF
                                : static_cast<uint8_t>((o + 8) >> 4);
            }
            *dst++ = static_cast<uint8_t>((hi << 4) | lo);
        }
    }
    std::free(buf);

    // ---- 6. Write atomically (.tmp -> rename) ----
    char tmp_path[MAX_PATH_LEN + 8];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dest_path);
#pragma GCC diagnostic pop

    FILE* fp = std::fopen(tmp_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "fopen %s failed", tmp_path);
        std::free(packed);
        return result;
    }
    ThumbnailFileHeader header{static_cast<uint16_t>(out_w),
                                static_cast<uint16_t>(out_h)};
    if (std::fwrite(&header, sizeof(header), 1, fp) != 1 ||
        std::fwrite(packed, packed_size, 1, fp) != 1) {
        ESP_LOGE(TAG, "fwrite %s failed", tmp_path);
        std::fclose(fp);
        std::remove(tmp_path);
        std::free(packed);
        return result;
    }
    std::fclose(fp);
    std::free(packed);

    std::remove(dest_path);  // FATFS rename fails if dest exists
    if (std::rename(tmp_path, dest_path) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed", tmp_path, dest_path);
        return result;
    }

    result.w = out_w;
    result.h = out_h;
    ESP_LOGI(TAG, "%s -> %s (%dx%d)", cbz_path, dest_path, out_w, out_h);
    return result;
}

ThumbnailImage load_thumbnail(const char* path) {
    ThumbnailImage img{nullptr, 0, 0};
    if (!path) return img;

    FILE* fp = std::fopen(path, "rb");
    if (!fp) return img;

    ThumbnailFileHeader header{};
    if (std::fread(&header, sizeof(header), 1, fp) != 1) {
        std::fclose(fp);
        return img;
    }
    const int w = header.width;
    const int h = header.height;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        ESP_LOGW(TAG, "load %s: implausible dims %dx%d", path, w, h);
        std::fclose(fp);
        return img;
    }

    const size_t pitch = static_cast<size_t>((w + 1) / 2);
    const size_t bytes = pitch * h;
    auto* buf = static_cast<uint8_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (!buf) {
        ESP_LOGE(TAG, "load alloc %u bytes failed", (unsigned)bytes);
        std::fclose(fp);
        return img;
    }
    if (std::fread(buf, bytes, 1, fp) != 1) {
        ESP_LOGE(TAG, "load %s: short read", path);
        std::free(buf);
        std::fclose(fp);
        return img;
    }
    std::fclose(fp);

    img.packed = buf;
    img.width  = w;
    img.height = h;
    return img;
}

void free_thumbnail(ThumbnailImage& img) {
    if (img.packed) {
        std::free(img.packed);
        img.packed = nullptr;
    }
    img.width = img.height = 0;
}

void blit_thumbnail(const ThumbnailImage& img,
                    int dst_x, int dst_y, int dst_w, int dst_h) {
    if (!img.packed || img.width <= 0 || img.height <= 0) return;
    if (dst_w <= 0 || dst_h <= 0) return;

    uint8_t* fb = ps3::display::framebuffer();
    if (!fb) return;

    const int native_w        = epd_width();
    const int native_h        = epd_height();
    const int native_w_bytes  = native_w / 2;
    const int max_native_x    = native_w - 1;
    const int max_native_y    = native_h - 1;
    const int src_pitch       = (img.width + 1) / 2;
    // Logical→native transform depends on the runtime panel rotation
    // (see image_display.cpp for the same dual-form mapping). Without
    // this branch icons and bookshelf thumbs would stay glued to the
    // INVERTED_PORTRAIT layout even after the user toggles "screen
    // rotation" in settings.
    const bool inv = ps3::display::is_inverted();

    // Aspect-preserving fit: shrink the painted region to whichever
    // axis hits dst_w / dst_h first and centre the result inside the
    // tile.
    const int scaled_w = (img.width  * dst_h >= img.height * dst_w)
                         ? dst_w
                         : (img.width  * dst_h) / img.height;
    const int scaled_h = (img.width  * dst_h >= img.height * dst_w)
                         ? (img.height * dst_w) / img.width
                         : dst_h;
    if (scaled_w <= 0 || scaled_h <= 0) return;

    const int off_x = dst_x + (dst_w - scaled_w) / 2;
    const int off_y = dst_y + (dst_h - scaled_h) / 2;

    // Apply the same black-point lift that image_display uses for
    // full pages, so a contrast change is reflected on bookshelf
    // thumbs too. Thumbnails are stored 4-bit packed, so we build a
    // 16-entry LUT once per blit instead of indexing an 8-bit one.
    // Thumbnails always live on the bookshelf, so they read the
    // bookshelf-side level rather than the reading-side one.
    uint8_t lut4[16];
    ps3::settings::build_contrast_lut4(lut4, ps3::settings::ContrastContext::Bookshelf);

    // Bilinear resample with 16.16 fixed-point source coordinates and
    // 8-bit fractional weights. With thumbnails now stored at 1/4 of
    // the source page (vs. the older 1/8) the typical downscale ratio
    // is ~2x — bilinear gives noticeably smoother output than the
    // previous nearest-neighbour blit, especially on diagonals and
    // text. The contrast LUT is applied AFTER the blend so each
    // output pixel pays for one LUT lookup, not four.
    const int img_w  = img.width;
    const int img_h  = img.height;
    const int max_sx = img_w - 1;
    const int max_sy = img_h - 1;
    // (img_dim << 16) / scaled_dim: how far to advance in the source
    // per output pixel, as Q16.16. The +0 origin places the first
    // output sample at source 0; subsequent samples step by this
    // value. Last sample lands at (scaled-1)*step, just shy of img-1
    // (modulo rounding) — clamping below catches the edge.
    const int x_step = (img_w << 16) / scaled_w;
    const int y_step = (img_h << 16) / scaled_h;

    for (int row = 0; row < scaled_h; ++row) {
        const int ly = off_y + row;
        if (ly < 0 || ly >= native_w) continue;  // logical h == native w

        const int sy_q16 = row * y_step;
        int sy0 = sy_q16 >> 16;
        if (sy0 < 0)      sy0 = 0;
        if (sy0 > max_sy) sy0 = max_sy;
        int sy1 = (sy0 < max_sy) ? sy0 + 1 : sy0;
        const int fy  = (sy_q16 >> 8) & 0xFF;
        const int wy0 = 256 - fy;
        const int wy1 = fy;

        const uint8_t* row0 = img.packed + sy0 * src_pitch;
        const uint8_t* row1 = img.packed + sy1 * src_pitch;

        const int nx     = inv ? (max_native_x - ly) : ly;
        const bool nx_odd = (nx & 1);

        for (int col = 0; col < scaled_w; ++col) {
            const int lx = off_x + col;
            if (lx < 0 || lx >= native_h) continue;  // logical w == native h

            const int sx_q16 = col * x_step;
            int sx0 = sx_q16 >> 16;
            if (sx0 < 0)      sx0 = 0;
            if (sx0 > max_sx) sx0 = max_sx;
            int sx1 = (sx0 < max_sx) ? sx0 + 1 : sx0;
            const int fx  = (sx_q16 >> 8) & 0xFF;
            const int wx0 = 256 - fx;
            const int wx1 = fx;

            // Extract the four corner nibbles (sx0, sy0..sy1 and
            // sx1, sy0..sy1) from the 4-bit packed rows.
            const uint8_t p00b = row0[sx0 >> 1];
            const uint8_t p10b = row0[sx1 >> 1];
            const uint8_t p01b = row1[sx0 >> 1];
            const uint8_t p11b = row1[sx1 >> 1];
            const int v00 = (sx0 & 1) ? (p00b & 0x0F) : (p00b >> 4);
            const int v10 = (sx1 & 1) ? (p10b & 0x0F) : (p10b >> 4);
            const int v01 = (sx0 & 1) ? (p01b & 0x0F) : (p01b >> 4);
            const int v11 = (sx1 & 1) ? (p11b & 0x0F) : (p11b >> 4);

            // Bilinear: x-interp at top + bottom rows, then y-interp.
            // Range of `v`: 0..15*65536 (= 0..983040). +32768 rounds.
            const int top = v00 * wx0 + v10 * wx1;   // 0..3840
            const int bot = v01 * wx0 + v11 * wx1;
            const int v   = top * wy0 + bot * wy1;
            int gray4_raw = (v + 32768) >> 16;       // 0..15
            if (gray4_raw > 15) gray4_raw = 15;      // numerical safety
            const uint8_t gray4 = lut4[gray4_raw];

            const int ny = inv ? lx : (max_native_y - lx);
            uint8_t* fb_byte = fb + ny * native_w_bytes + (nx >> 1);
            if (nx_odd) {
                *fb_byte = (*fb_byte & 0x0F) | (gray4 << 4);
            } else {
                *fb_byte = (*fb_byte & 0xF0) | gray4;
            }
        }
    }
}

}  // namespace ps3::library
