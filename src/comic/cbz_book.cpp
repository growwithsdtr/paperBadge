#include "cbz_book.hpp"

#include <algorithm>
#include <cctype>
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

char lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool has_extension_icase(const char* name, size_t len, const char* ext) {
    if (!name || !ext) return false;
    const size_t ext_len = std::strlen(ext);
    if (ext_len > len) return false;
    const char* tail = name + len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        if (lower_ascii(tail[i]) != lower_ascii(ext[i])) return false;
    }
    return true;
}

bool has_webp_extension(const char* name, size_t len) {
    return has_extension_icase(name, len, ".webp");
}

bool has_other_unsupported_image_extension(const char* name, size_t len) {
    return has_extension_icase(name, len, ".gif") ||
           has_extension_icase(name, len, ".bmp") ||
           has_extension_icase(name, len, ".tif") ||
           has_extension_icase(name, len, ".tiff") ||
           has_extension_icase(name, len, ".avif") ||
           has_extension_icase(name, len, ".heic") ||
           has_extension_icase(name, len, ".heif");
}

bool segment_equals_ci(const char* start, size_t len, const char* needle) {
    const size_t needle_len = std::strlen(needle);
    if (len != needle_len) return false;
    for (size_t i = 0; i < len; ++i) {
        if (lower_ascii(start[i]) != lower_ascii(needle[i])) return false;
    }
    return true;
}

bool entry_has_hidden_or_macosx_segment(const char* name, size_t len) {
    const char* segment = name;
    const char* end = name + len;
    for (const char* p = name; p <= end; ++p) {
        if (p != end && *p != '/') continue;
        const size_t segment_len = static_cast<size_t>(p - segment);
        if (segment_len > 0) {
            if (segment[0] == '.') return true;
            if (segment_equals_ci(segment, segment_len, "__MACOSX")) return true;
        }
        segment = p + 1;
    }
    return false;
}

int natural_compare(const char* a, const char* b) {
    while (*a != '\0' || *b != '\0') {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            const char* a_run = a;
            const char* b_run = b;
            while (*a == '0') ++a;
            while (*b == '0') ++b;
            const char* a_sig = a;
            const char* b_sig = b;
            while (std::isdigit(static_cast<unsigned char>(*a))) ++a;
            while (std::isdigit(static_cast<unsigned char>(*b))) ++b;
            const int a_sig_len = static_cast<int>(a - a_sig);
            const int b_sig_len = static_cast<int>(b - b_sig);
            if (a_sig_len != b_sig_len) return a_sig_len < b_sig_len ? -1 : 1;
            for (int i = 0; i < a_sig_len; ++i) {
                if (a_sig[i] != b_sig[i]) return a_sig[i] < b_sig[i] ? -1 : 1;
            }
            const int a_run_len = static_cast<int>(a - a_run);
            const int b_run_len = static_cast<int>(b - b_run);
            if (a_run_len != b_run_len) return a_run_len < b_run_len ? -1 : 1;
            continue;
        }
        const char la = lower_ascii(*a);
        const char lb = lower_ascii(*b);
        if (la != lb) return la < lb ? -1 : 1;
        if (*a != '\0') ++a;
        if (*b != '\0') ++b;
    }
    return 0;
}

}  // namespace

CbzBook::CbzBook()
    : zip_(nullptr),
      page_indices_(nullptr),
      page_count_(0),
      last_status_(CbzOpenStatus::None),
      last_stats_{} {}

CbzBook::~CbzBook() {
    close();
}

bool CbzBook::open(const char* path) {
    close();
    last_status_ = CbzOpenStatus::None;
    last_stats_ = {};

    mz_zip_archive* zip = static_cast<mz_zip_archive*>(
        std::calloc(1, sizeof(mz_zip_archive)));
    if (!zip) {
        ESP_LOGE(TAG, "alloc mz_zip_archive failed");
        last_status_ = CbzOpenStatus::ArchiveAllocFailed;
        return false;
    }

    if (!mz_zip_reader_init_file(zip, path, 0)) {
        ESP_LOGE(TAG, "open %s failed (zip init returned 0)", path);
        last_status_ = CbzOpenStatus::ZipInitFailed;
        std::free(zip);
        return false;
    }

    const int n = static_cast<int>(mz_zip_reader_get_num_files(zip));
    last_stats_.total_entries = n;
    if (n <= 0) {
        ESP_LOGE(TAG, "no entries in %s", path);
        last_status_ = CbzOpenStatus::NoDisplayablePages;
        mz_zip_reader_end(zip);
        std::free(zip);
        return false;
    }

    // First pass: collect candidate page indices.
    int* tmp = static_cast<int*>(std::malloc(sizeof(int) * n));
    if (!tmp) {
        ESP_LOGE(TAG, "alloc page index buffer failed");
        last_status_ = CbzOpenStatus::PageIndexAllocFailed;
        mz_zip_reader_end(zip);
        std::free(zip);
        return false;
    }
    int count = 0;
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(zip, i, &st)) continue;
        const size_t len = std::strlen(st.m_filename);
        // Japanese-locale Windows zip tools can set directory attributes on
        // real files, so keep using payload size/trailing slash as the guard.
        if (st.m_uncomp_size == 0 || len == 0 || st.m_filename[len - 1] == '/') {
            ++last_stats_.directory_entries;
            continue;
        }
        if (entry_has_hidden_or_macosx_segment(st.m_filename, len)) {
            ++last_stats_.hidden_entries;
            continue;
        }
        if (has_jpeg_extension(st.m_filename, len)) {
            ++last_stats_.jpeg_pages;
            tmp[count++] = i;
            continue;
        }
        if (has_png_extension(st.m_filename, len)) {
            ++last_stats_.png_pages;
            tmp[count++] = i;
            continue;
        }
        if (has_webp_extension(st.m_filename, len)) {
            ++last_stats_.webp_entries;
            continue;
        }
        if (has_other_unsupported_image_extension(st.m_filename, len)) {
            ++last_stats_.unsupported_image_entries;
        }
    }
    last_stats_.displayable_pages = count;

    if (count == 0) {
        ESP_LOGE(TAG,
                 "no displayable (jpg/jpeg/png) entries in %s "
                 "(entries=%d webp=%d unsupported_images=%d hidden=%d dirs=%d)",
                 path, n, last_stats_.webp_entries,
                 last_stats_.unsupported_image_entries,
                 last_stats_.hidden_entries, last_stats_.directory_entries);
        last_status_ = CbzOpenStatus::NoDisplayablePages;
        std::free(tmp);
        mz_zip_reader_end(zip);
        std::free(zip);
        return false;
    }

    // Second pass: natural-sort by entry filename so page10 follows page9.
    // Selection sort is simple and `count` (<= a few hundred) is small.
    for (int i = 0; i < count - 1; ++i) {
        int min_pos = i;
        mz_zip_archive_file_stat st_min;
        mz_zip_reader_file_stat(zip, tmp[min_pos], &st_min);
        for (int j = i + 1; j < count; ++j) {
            mz_zip_archive_file_stat st_j;
            mz_zip_reader_file_stat(zip, tmp[j], &st_j);
            if (natural_compare(st_j.m_filename, st_min.m_filename) < 0) {
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
    last_status_ = CbzOpenStatus::Ok;

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
