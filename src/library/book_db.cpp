#include "book_db.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace ps3::library {

namespace {
constexpr const char* TAG = "book_db";

// Parse a NUL-terminated line in-place. Returns the next field on
// each call (using `state` as a saveptr) by replacing tabs with NULs.
// Empty trailing fields are returned as empty strings.
char* tsv_next(char** state) {
    if (!state || !*state) return nullptr;
    char* p = *state;
    if (*p == '\0') return nullptr;
    char* tok = p;
    while (*p && *p != '\t' && *p != '\n' && *p != '\r') ++p;
    if (*p) {
        *p = '\0';
        *state = p + 1;
    } else {
        *state = p;  // points to NUL; next call returns nullptr
    }
    return tok;
}

void copy_field(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

}  // namespace

BookDb::BookDb()
    : records_(nullptr), count_(0), next_id_(1) {
    db_path_[0] = '\0';
}

BookDb::~BookDb() {
    if (records_) {
        std::free(records_);
        records_ = nullptr;
    }
}

bool BookDb::open(const char* db_path) {
    if (!db_path) return false;
    std::strncpy(db_path_, db_path, sizeof(db_path_) - 1);
    db_path_[sizeof(db_path_) - 1] = '\0';

    if (!records_) {
        records_ = static_cast<BookRecord*>(heap_caps_malloc(
            sizeof(BookRecord) * MAX_BOOKS_IN_DB, MALLOC_CAP_SPIRAM));
        if (!records_) {
            ESP_LOGE(TAG, "PSRAM alloc for %d records (%u bytes) failed",
                     MAX_BOOKS_IN_DB,
                     (unsigned)(sizeof(BookRecord) * MAX_BOOKS_IN_DB));
            return false;
        }
    }
    count_ = 0;
    next_id_ = 1;

    FILE* fp = std::fopen(db_path_, "r");
    if (!fp) {
        ESP_LOGI(TAG, "no DB at %s — starting empty", db_path_);
        return true;
    }

    char line[MAX_PATH_LEN + 256];
    bool first = true;
    while (std::fgets(line, sizeof(line), fp)) {
        if (first) {
            first = false;
            // Skip header if it starts with "id\t".
            if (std::strncmp(line, "id\t", 3) == 0) continue;
        }
        if (count_ >= MAX_BOOKS_IN_DB) {
            ESP_LOGW(TAG, "DB has more than %d records — truncating",
                     MAX_BOOKS_IN_DB);
            break;
        }
        if (!parse_line(line)) {
            ESP_LOGW(TAG, "skipping malformed DB line");
            continue;
        }
        const int id = records_[count_].id;
        if (id >= next_id_) next_id_ = id + 1;
        ++count_;
    }
    std::fclose(fp);

    ESP_LOGI(TAG, "loaded %s: %d records (next_id=%d)",
             db_path_, count_, next_id_);
    return true;
}

bool BookDb::parse_line(char* line) {
    if (!line || !line[0]) return false;
    // Caller (open()) gates on count_ < MAX_BOOKS_IN_DB before invoking
    // us. Guard the invariant locally so a future caller can't slip a
    // record past the end of records_.
    assert(count_ < MAX_BOOKS_IN_DB);
    BookRecord& r = records_[count_];
    std::memset(&r, 0, sizeof(r));

    char* state = line;
    char* f_id   = tsv_next(&state);
    char* f_path = tsv_next(&state);
    char* f_thmb = tsv_next(&state);
    char* f_lp   = tsv_next(&state);
    char* f_lra  = tsv_next(&state);
    // Optional — TSVs written before last_read_seq existed simply
    // don't have a 6th field, in which case the record loads with
    // seq=0 (never opened). The next open promotes it to max+1.
    char* f_seq  = tsv_next(&state);

    if (!f_id || !f_path || !*f_path) return false;

    r.id = std::atoi(f_id);
    copy_field(r.path, sizeof(r.path), f_path);
    copy_field(r.thumb, sizeof(r.thumb), f_thmb);
    r.last_page = f_lp ? std::atoi(f_lp) : 0;
    copy_field(r.last_read_at, sizeof(r.last_read_at), f_lra);
    r.last_read_seq = f_seq ? std::atoi(f_seq) : 0;
    return true;
}

bool BookDb::save() {
    if (db_path_[0] == '\0') return false;

    char tmp_path[MAX_PATH_LEN + 8];
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", db_path_);

    FILE* fp = std::fopen(tmp_path, "w");
    if (!fp) {
        ESP_LOGE(TAG, "fopen(%s) for write failed", tmp_path);
        return false;
    }

    std::fputs("id\tpath\tthumb\tlast_page\tlast_read_at\tlast_read_seq\n",
               fp);
    for (int i = 0; i < count_; ++i) {
        const BookRecord& r = records_[i];
        std::fprintf(fp, "%d\t%s\t%s\t%d\t%s\t%d\n",
                     r.id, r.path, r.thumb, r.last_page,
                     r.last_read_at, r.last_read_seq);
    }
    if (std::fflush(fp) != 0) {
        ESP_LOGW(TAG, "fflush warning on %s", tmp_path);
    }
    std::fclose(fp);

    // Atomic replace: rename(.tmp, real).
    std::remove(db_path_);   // FATFS rename fails if dest exists
    if (std::rename(tmp_path, db_path_) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed", tmp_path, db_path_);
        return false;
    }
    ESP_LOGI(TAG, "saved %s: %d records", db_path_, count_);
    return true;
}

const BookRecord* BookDb::find(const char* path) const {
    if (!path || !records_) return nullptr;
    for (int i = 0; i < count_; ++i) {
        if (std::strcmp(records_[i].path, path) == 0) return &records_[i];
    }
    return nullptr;
}

const BookRecord* BookDb::find_most_recent() const {
    if (!records_) return nullptr;
    const BookRecord* best = nullptr;
    for (int i = 0; i < count_; ++i) {
        const BookRecord& r = records_[i];
        if (r.last_read_seq <= 0) continue;  // never opened
        if (!best || r.last_read_seq > best->last_read_seq) {
            best = &r;
        }
    }
    return best;
}

int BookDb::max_read_seq() const {
    if (!records_) return 0;
    int m = 0;
    for (int i = 0; i < count_; ++i) {
        if (records_[i].last_read_seq > m) m = records_[i].last_read_seq;
    }
    return m;
}

BookRecord* BookDb::find_or_add(const char* path) {
    if (!path || !records_) return nullptr;
    for (int i = 0; i < count_; ++i) {
        if (std::strcmp(records_[i].path, path) == 0) return &records_[i];
    }
    if (count_ >= MAX_BOOKS_IN_DB) {
        ESP_LOGW(TAG, "DB full (%d), can't add %s", MAX_BOOKS_IN_DB, path);
        return nullptr;
    }
    BookRecord& r = records_[count_];
    std::memset(&r, 0, sizeof(r));
    r.id = next_id_++;
    copy_field(r.path, sizeof(r.path), path);
    ++count_;
    return &r;
}

void BookDb::make_thumb_name(int id, char* out, size_t out_size) {
    if (out_size == 0) return;
    std::snprintf(out, out_size, "%04d.thumb", id);
}

void BookDb::make_thumb_path(const char* thumb_name,
                             char* out, size_t out_size) {
    if (out_size == 0) return;
    if (!thumb_name || !thumb_name[0]) {
        out[0] = '\0';
        return;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    std::snprintf(out, out_size, "%s/%s", THUMB_DIR, thumb_name);
#pragma GCC diagnostic pop
}

}  // namespace ps3::library
