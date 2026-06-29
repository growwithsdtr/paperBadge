#pragma once

#include <cstddef>
#include <cstdint>

namespace ps3::comic {

// True if `name` ends in `.jpg` or `.jpeg` (case-insensitive).
// Used both when filtering CBZ entries on open() and when picking
// the cover candidate for thumbnail generation — keep the two
// callers in sync via this single predicate so a CBZ that opens
// successfully (because there is at least one JPEG inside) can
// also produce a thumbnail.
bool has_jpeg_extension(const char* name, std::size_t len);


// An open CBZ archive on the SD card. Holds the miniz handle for the
// lifetime of the book and lets the caller pull individual pages out
// without re-opening the file each time.
//
// The "page list" is built on open(): every entry whose name doesn't
// look like a directory marker (size > 0, no trailing '/') is treated
// as content and sorted by entry name.
class CbzBook {
 public:
    CbzBook();
    ~CbzBook();

    CbzBook(const CbzBook&) = delete;
    CbzBook& operator=(const CbzBook&) = delete;

    bool open(const char* path);
    void close();
    bool is_open() const { return zip_ != nullptr; }

    int page_count() const { return page_count_; }

    // Decompress page `idx` (0-based) into a freshly heap-allocated
    // buffer. Caller frees with free(). Optionally fills `name_buf`
    // with the entry name (UTF-8 if it was stored that way; raw
    // bytes otherwise).
    bool extract(int idx,
                 uint8_t** out_data, size_t* out_size,
                 char* name_buf, size_t name_buf_size);

 private:
    void* zip_;            // mz_zip_archive* (PIMPL via void*)
    int*  page_indices_;   // entry indices, sorted by entry name
    int   page_count_;
};

}  // namespace ps3::comic
