#include "screenshot.hpp"

#include "../hal/display.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

extern "C" {
#include <epdiy.h>
#include "local/miniz.h"
}

namespace ps3::screenshot {

namespace {

constexpr const char* TAG       = "screenshot";
constexpr const char* DIR_PATH  = "/sdcard/screenshot";
constexpr const char* FILE_PREFIX = "screenshot_";
constexpr const char* FILE_SUFFIX = ".png";
constexpr int         FILENAME_DIGITS = 4;     // screenshot_0001.png
constexpr int         MAX_SEQUENCE     = 9999;

// PNG chunk helpers -------------------------------------------------------

void put_be32(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>(v >> 24);
    dst[1] = static_cast<uint8_t>(v >> 16);
    dst[2] = static_cast<uint8_t>(v >> 8);
    dst[3] = static_cast<uint8_t>(v);
}

// Write one PNG chunk (length + type + data + CRC32-of-(type+data)).
// Returns true on full write success.
bool write_chunk(FILE* fp, const char type[4],
                 const uint8_t* data, size_t data_len) {
    uint8_t length_be[4];
    put_be32(length_be, static_cast<uint32_t>(data_len));
    if (std::fwrite(length_be, 1, 4, fp) != 4) return false;
    if (std::fwrite(type, 1, 4, fp) != 4) return false;
    if (data_len > 0 && std::fwrite(data, 1, data_len, fp) != data_len) {
        return false;
    }

    // CRC is computed over type + data.
    mz_ulong crc = mz_crc32(0, nullptr, 0);
    crc = mz_crc32(crc, reinterpret_cast<const unsigned char*>(type), 4);
    if (data_len > 0) {
        crc = mz_crc32(crc, data, data_len);
    }
    uint8_t crc_be[4];
    put_be32(crc_be, static_cast<uint32_t>(crc));
    return std::fwrite(crc_be, 1, 4, fp) == 4;
}

// Filename sequencing -----------------------------------------------------

// Scan DIR_PATH for files matching `screenshot_NNNN.png` and return
// the largest NNNN seen. Returns 0 if the directory is missing /
// empty / contains no matching files.
int find_max_sequence() {
    DIR* dir = opendir(DIR_PATH);
    if (!dir) return 0;

    int max_seq = 0;
    const size_t prefix_len = std::strlen(FILE_PREFIX);
    const size_t suffix_len = std::strlen(FILE_SUFFIX);
    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        const char* name = de->d_name;
        const size_t nlen = std::strlen(name);
        if (nlen != prefix_len + FILENAME_DIGITS + suffix_len) continue;
        if (std::strncmp(name, FILE_PREFIX, prefix_len) != 0) continue;
        if (std::strcmp(name + nlen - suffix_len, FILE_SUFFIX) != 0) continue;

        // Parse the FILENAME_DIGITS-character integer between prefix and
        // suffix. Reject anything containing non-digits — we don't want
        // a stray screenshot_abcd.png to bump the counter.
        int n = 0;
        bool ok = true;
        for (size_t i = 0; i < FILENAME_DIGITS; ++i) {
            const char c = name[prefix_len + i];
            if (c < '0' || c > '9') { ok = false; break; }
            n = n * 10 + (c - '0');
        }
        if (ok && n > max_seq) max_seq = n;
    }
    closedir(dir);
    return max_seq;
}

// Framebuffer reader ------------------------------------------------------

// Returns the 4-bit grayscale value (0..15) at logical (lx, ly), with
// the runtime rotation translation baked in. Mirrors the inverse of
// the per-pixel transform in src/comic/image_display.cpp's draw
// callbacks.
//
// Nibble layout (epdiy / image_display / blit_thumbnail consensus):
//   - even nx → LOW  nibble of the byte  (`byte & 0x0F`)
//   - odd  nx → HIGH nibble of the byte  (`byte >> 4`)
// Getting this backwards swaps adjacent horizontal pixel pairs and
// makes the saved PNG look subtly smeared.
inline uint8_t logical_pixel4(const uint8_t* fb, int lx, int ly,
                              int native_w, int native_h, bool inv) {
    const int max_nx = native_w - 1;
    const int max_ny = native_h - 1;
    const int nx = inv ? (max_nx - ly) : ly;
    const int ny = inv ? lx           : (max_ny - lx);
    const int width_bytes = native_w / 2;
    const uint8_t byte = fb[ny * width_bytes + (nx >> 1)];
    return (nx & 1) ? static_cast<uint8_t>(byte >> 4)
                    : static_cast<uint8_t>(byte & 0x0F);
}

// PNG row builder ---------------------------------------------------------

// Pack the raw scanline buffer for one row of the logical image into
// `dst`. Layout: 1 filter byte (0 = no filter) + ceil(W/2) packed
// pixel bytes (PNG 4 bpp grayscale: leftmost pixel = high nibble).
// Returns the number of bytes written (= 1 + ceil(W/2)).
size_t encode_row(uint8_t* dst, int row, int logical_w, int logical_h,
                  const uint8_t* fb,
                  int native_w, int native_h, bool inv) {
    const int row_bytes = (logical_w + 1) / 2;
    dst[0] = 0;  // filter type 0 = None
    uint8_t* row_dst = dst + 1;
    std::memset(row_dst, 0, row_bytes);
    for (int x = 0; x < logical_w; ++x) {
        const uint8_t v = logical_pixel4(fb, x, row, native_w, native_h, inv);
        if ((x & 1) == 0) {
            row_dst[x >> 1] = static_cast<uint8_t>(v << 4);
        } else {
            row_dst[x >> 1] |= static_cast<uint8_t>(v & 0x0F);
        }
    }
    (void)logical_h;  // referenced only for symmetry
    return static_cast<size_t>(1 + row_bytes);
}

}  // namespace

bool capture_to_sd(char* out_name, size_t out_name_size) {
    if (!ps3::display::framebuffer()) {
        ESP_LOGE(TAG, "no framebuffer available");
        return false;
    }

    // Make sure /sdcard/screenshot/ exists.
    struct stat st{};
    if (::stat(DIR_PATH, &st) != 0) {
        if (mkdir(DIR_PATH, 0755) != 0) {
            ESP_LOGE(TAG, "mkdir %s failed", DIR_PATH);
            return false;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "%s exists but is not a directory", DIR_PATH);
        return false;
    }

    // Pick the next available filename.
    const int next_seq = find_max_sequence() + 1;
    if (next_seq > MAX_SEQUENCE) {
        ESP_LOGE(TAG, "filename counter exhausted (>%d)", MAX_SEQUENCE);
        return false;
    }
    char basename[40];
    std::snprintf(basename, sizeof(basename), "%s%0*d%s",
                  FILE_PREFIX, FILENAME_DIGITS, next_seq, FILE_SUFFIX);
    char path[160];
    std::snprintf(path, sizeof(path), "%s/%s", DIR_PATH, basename);

    // Logical / native dims. We always emit at logical orientation so
    // the saved PNG is human-friendly regardless of the runtime
    // rotation toggle.
    const int logical_w = ps3::display::width();
    const int logical_h = ps3::display::height();
    const int native_w  = epd_width();
    const int native_h  = epd_height();
    const bool inv      = ps3::display::is_inverted();
    const uint8_t* fb   = ps3::display::framebuffer();

    // Build the raw filtered scanline buffer (filter byte + packed
    // 4 bpp row, repeated for every logical row).
    const size_t row_bytes_packed = static_cast<size_t>((logical_w + 1) / 2);
    const size_t per_row          = 1 + row_bytes_packed;
    const size_t raw_size         = per_row * static_cast<size_t>(logical_h);
    auto* raw = static_cast<uint8_t*>(
        heap_caps_malloc(raw_size, MALLOC_CAP_SPIRAM));
    if (!raw) {
        ESP_LOGE(TAG, "alloc raw buffer (%u bytes) failed",
                 (unsigned)raw_size);
        return false;
    }
    for (int y = 0; y < logical_h; ++y) {
        encode_row(raw + y * per_row, y, logical_w, logical_h,
                   fb, native_w, native_h, inv);
    }

    // Compress to a zlib-wrapped IDAT payload. PNG specifies zlib
    // (RFC 1950) inside IDAT, which is exactly what mz_compress
    // produces.
    mz_ulong comp_cap  = mz_compressBound(static_cast<mz_ulong>(raw_size));
    auto* comp = static_cast<uint8_t*>(
        heap_caps_malloc(comp_cap, MALLOC_CAP_SPIRAM));
    if (!comp) {
        ESP_LOGE(TAG, "alloc compress buffer (%u bytes) failed",
                 (unsigned)comp_cap);
        std::free(raw);
        return false;
    }
    mz_ulong comp_size = comp_cap;
    const int zrc = mz_compress(comp, &comp_size, raw,
                                static_cast<mz_ulong>(raw_size));
    std::free(raw);
    if (zrc != MZ_OK) {
        ESP_LOGE(TAG, "mz_compress rc=%d", zrc);
        std::free(comp);
        return false;
    }

    // Open the output file (atomic-ish via .tmp + rename so a
    // mid-write crash doesn't leave a half-written PNG that future
    // sequence scans would still count).
    char tmp_path[176];
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE* fp = std::fopen(tmp_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "fopen %s failed", tmp_path);
        std::free(comp);
        return false;
    }

    bool ok = true;
    // PNG signature.
    const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::fwrite(sig, 1, 8, fp) != 8) ok = false;

    // IHDR — width, height, bit_depth=4, color_type=0 (grayscale),
    // compression=0, filter=0, interlace=0.
    if (ok) {
        uint8_t ihdr[13] = {};
        put_be32(ihdr,     static_cast<uint32_t>(logical_w));
        put_be32(ihdr + 4, static_cast<uint32_t>(logical_h));
        ihdr[8]  = 4;   // bit_depth
        ihdr[9]  = 0;   // color_type: grayscale
        ihdr[10] = 0;   // compression
        ihdr[11] = 0;   // filter
        ihdr[12] = 0;   // interlace
        if (!write_chunk(fp, "IHDR", ihdr, sizeof(ihdr))) ok = false;
    }

    // IDAT — zlib-wrapped compressed payload.
    if (ok && !write_chunk(fp, "IDAT", comp, comp_size)) ok = false;

    // IEND — empty.
    if (ok && !write_chunk(fp, "IEND", nullptr, 0)) ok = false;

    std::fclose(fp);
    std::free(comp);

    if (!ok) {
        ESP_LOGE(TAG, "write %s failed; removing partial file", tmp_path);
        std::remove(tmp_path);
        return false;
    }

    std::remove(path);  // FATFS rename refuses to overwrite
    if (std::rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed", tmp_path, path);
        std::remove(tmp_path);
        return false;
    }

    if (out_name && out_name_size > 0) {
        std::strncpy(out_name, basename, out_name_size - 1);
        out_name[out_name_size - 1] = '\0';
    }
    ESP_LOGI(TAG, "saved %s (%lu bytes compressed)", path,
             static_cast<unsigned long>(comp_size));
    return true;
}

}  // namespace ps3::screenshot
