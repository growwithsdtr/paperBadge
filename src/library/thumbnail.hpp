#pragma once

#include <cstddef>
#include <cstdint>

namespace ps3::library {

// On-disk format:
//   uint16_t width
//   uint16_t height
//   then ((width+1)/2) * height bytes of 4 bpp grayscale, packed two
//   pixels per byte:
//     - high nibble = even column
//     - low  nibble = odd column
//   - nibble value 0 = black, 0xF = white (matches the e-paper).
struct ThumbnailFileHeader {
    uint16_t width;
    uint16_t height;
};

struct ThumbDimensions {
    int w;
    int h;
};

// Generate a thumbnail from `cbz_path` (open the archive, decode the
// lex-smallest image entry at JPEG 1/8 scale, pack to 4 bpp) and write
// it atomically to `dest_path`. Returns the resulting (w, h) on
// success or {0, 0} on failure.
ThumbDimensions generate_thumbnail(const char* cbz_path,
                                    const char* dest_path);

// In-memory thumbnail.
struct ThumbnailImage {
    uint8_t* packed;       // 4 bpp, high=even col / low=odd col
    int      width;
    int      height;
};

// Load a thumbnail file into PSRAM. Returns img.packed=nullptr on
// failure. Caller releases with free_thumbnail().
ThumbnailImage load_thumbnail(const char* path);
void free_thumbnail(ThumbnailImage& img);

// Blit a loaded thumbnail onto the active e-paper framebuffer,
// scaled with nearest-neighbour into the rect (dst_x, dst_y, dst_w,
// dst_h). The source 4 bpp packed bytes are interpreted in the
// thumbnail file convention; the destination framebuffer rotation
// (EPD_ROT_INVERTED_PORTRAIT) is applied per-pixel.
void blit_thumbnail(const ThumbnailImage& img,
                    int dst_x, int dst_y, int dst_w, int dst_h);

}  // namespace ps3::library
