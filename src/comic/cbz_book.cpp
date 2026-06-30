#include "cbz_book.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>

extern "C" {
// Local-namespace miniz to avoid colliding with esp_rom/include/miniz.h
// (which is a stripped subset).
#include "local/miniz.h"
}

namespace ps3::comic {

bool has_jpeg_extension(const char* name, size_t len) {
    if (!name || len < 4) return false;
    const char* ext = name + len - 4;
    if ((ext[0] == '.') &&
        (ext[1] == 'j' || ext[1] == 'J') &&
        (ext[2] == 'p' || ext[2] == 'P') &&
        (ext[3] == 'g' || ext[3] == 'G')) {
        return true;
    }
    if (len >= 5) {
        const char* ext5 = name + len - 5;
        if ((ext5[0] == '.') &&
            (ext5[1] == 'j' || ext5[1] == 'J') &&
            (ext5[2] == 'p' || ext5[2] == 'P') &&
            (ext5[3] == 'e' || ext5[3] == 'E') &&
            (ext5[4] == 'g' || ext5[4] == 'G')) {
            return true;
        }
    }
    return false;
}

bool has_png_extension(const char* name, size_t len) {
    if (!name || len < 4) return false;
    const char* ext = name + len - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'p' || ext[1] == 'P') &&
           (ext[2] == 'n' || ext[2] == 'N') &&
           (ext[3] == 'g' || ext[3] == 'G');
}

namespace {
constexpr const char* TAG = "cbz";

// Filter by displayable page extensions at open time and surface the
// "no usable pages" condition as an open() failure, instead of
// silently building a page list whose first decode would just fail.
//
// Note: many Japanese-locale Windows zip tools store entry names in
// CP932 and set the DOS "subdirectory" attribute on real files, so
// we still can't trust `m_is_directory`; the '/' trailing-slash check
// remains the directory guard.
bool entry_looks_like_file(const mz_zip_archive_file_stat& st) {
    if (st.m_uncomp_size == 0) return false;
    const size_t len = std::strlen(st.m_filename);
    if (len == 0) return false;
    if (st.m_filename[len - 1] == '/') return false;
    if (!has_jpeg_extension(st.m_filename, len) &&
        !has_png_extension(st.m_filename, len)) return false;
    return true;
}

}  // namespace

CbzBook::CbzBook()
    : zip_(nullptr), page_indices_(nullptr), page_count_(0) {}

CbzBook::~CbzBook() {
    close();
}

bool CbzBook::open(const char* path) {
    close();

    mz_zip_archive* zip = static_cast<mz_zip_archive*>(
        std::calloc(1, sizeof(mz_zip_archive)));
    if (!zip) {
        ESP_LOGE(TAG, "alloc mz_zip_archive failed");
        return false;
    }

    if (!mz_zip_reader_init_file(zip, path, 0)) {
        ESP_LOGE(TAG, "open %s failed (zip init returned 0)", path);
        std::free(zip);
        return false;
    }

    const int n = static_cast<int>(mz_zip_reader_get_num_files(zip));

    // First pass: collect candidate page indices.
    int* tmp = static_cast<int*>(std::malloc(sizeof(int) * n));
    if (!tmp) {
        ESP_LOGE(TAG, "alloc page index buffer failed");
        mz_zip_reader_end(zip);
        std::free(zip);
        return false;
    }
    int count = 0;
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(zip, i, &st)) continue;
        if (!entry_looks_like_file(st)) continue;
        tmp[count++] = i;
    }

    if (count == 0) {
        ESP_LOGE(TAG, "no displayable (jpg/jpeg/png) entries in %s (had %d files)",
                 path, n);
        std::free(tmp);
        mz_zip_reader_end(zip);
        std::free(zip);
        return false;
    }

    // Second pass: sort by entry filename so page order is stable.
    // Selection sort is simple and `count` (<= a few hundred) is small.
    for (int i = 0; i < count - 1; ++i) {
        int min_pos = i;
        mz_zip_archive_file_stat st_min;
        mz_zip_reader_file_stat(zip, tmp[min_pos], &st_min);
        for (int j = i + 1; j < count; ++j) {
            mz_zip_archive_file_stat st_j;
            mz_zip_reader_file_stat(zip, tmp[j], &st_j);
            if (std::strcmp(st_j.m_filename, st_min.m_filename) < 0) {
                min_pos = j;
                st_min = st_j;
            }
        }
        if (min_pos != i) {
            const int swap = tmp[i];
            tmp[i] = tmp[min_pos];
            tmp[min_pos] = swap;
        }
    }

    zip_ = zip;
    page_indices_ = tmp;
    page_count_ = count;

    ESP_LOGI(TAG, "opened %s: %d pages (of %d entries)", path, count, n);
    return true;
}

void CbzBook::close() {
    if (zip_) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(zip_));
        std::free(zip_);
        zip_ = nullptr;
    }
    std::free(page_indices_);
    page_indices_ = nullptr;
    page_count_ = 0;
}

bool CbzBook::extract(int idx,
                      uint8_t** out_data, size_t* out_size,
                      char* name_buf, size_t name_buf_size,
                      PageImageFormat* out_format) {
    if (!zip_ || idx < 0 || idx >= page_count_) return false;

    const int entry = page_indices_[idx];
    auto* zip = static_cast<mz_zip_archive*>(zip_);

    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(zip, entry, &st)) {
        ESP_LOGE(TAG, "stat page %d failed", idx);
        return false;
    }

    size_t out_sz = 0;
    void* buf = mz_zip_reader_extract_to_heap(zip, entry, &out_sz, 0);
    if (!buf) {
        ESP_LOGE(TAG, "extract page %d (%s) failed (likely OOM)",
                 idx, st.m_filename);
        return false;
    }

    *out_data = static_cast<uint8_t*>(buf);
    *out_size = out_sz;
    if (name_buf && name_buf_size > 0) {
        std::strncpy(name_buf, st.m_filename, name_buf_size - 1);
        name_buf[name_buf_size - 1] = '\0';
    }
    if (out_format) {
        const size_t len = std::strlen(st.m_filename);
        *out_format = has_png_extension(st.m_filename, len) ? PageImageFormat::Png
                                                            : PageImageFormat::Jpeg;
    }
    return true;
}

}  // namespace ps3::comic
