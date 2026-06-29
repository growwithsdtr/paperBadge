#pragma once

#include <cstddef>

#include "library.hpp"

namespace ps3::library {

// Per-book record persisted in /sdcard/temp/books.tsv. The TSV is
// the single source of truth for thumbnail association, reading
// progress, and any future per-book metadata.
//
// Layout on disk (TAB-separated, one record per line; first line is
// the header):
//   id\tpath\tthumb\tlast_page\tlast_read_at\tlast_read_seq
//
// Key is `path` (full SD-mount path). `id` is a stable internal
// integer used to derive the thumbnail filename (`{id:04d}.thumb`).
//
// `last_read_seq` is a monotonically increasing counter stamped
// whenever a book is opened — used to find the most recently read
// record without depending on `time(nullptr)` (which resets to 0
// at every hardware reset, making the human-readable
// `last_read_at` ISO 8601 string unreliable for cross-reboot
// recency comparison). Older TSV files written before the field
// existed load with last_read_seq=0, which still works for
// find_most_recent() — next open promotes the active book to a
// fresh max+1.

constexpr int MAX_BOOKS_IN_DB  = 200;
constexpr int THUMB_NAME_MAX   = 16;     // e.g. "0123.thumb"
constexpr int TIMESTAMP_MAX    = 32;     // ISO 8601 + slack

struct BookRecord {
    int  id;
    char path[MAX_PATH_LEN];
    char thumb[THUMB_NAME_MAX];          // empty = no thumbnail
    int  last_page;                      // 0 = not started
    char last_read_at[TIMESTAMP_MAX];    // empty = not started
    int  last_read_seq;                  // 0 = never opened
};

class BookDb {
 public:
    BookDb();
    ~BookDb();

    BookDb(const BookDb&)            = delete;
    BookDb& operator=(const BookDb&) = delete;

    // Load existing DB (if file is missing the DB starts empty).
    // Stores `db_path` for later save(). Returns false only on
    // unrecoverable errors (allocation, malformed TSV).
    bool open(const char* db_path);

    // Atomic save: write to "<db_path>.tmp", then rename.
    bool save();

    // Read accessors.
    int                count()         const { return count_; }
    const BookRecord&  at(int idx)     const { return records_[idx]; }
    const BookRecord*  find(const char* path) const;

    // Find the record with the largest `last_read_seq`. Records
    // with seq==0 (never opened, or migrated from a pre-seq TSV)
    // are skipped. Returns nullptr when no record has been opened
    // yet — caller should treat that as "no last book" rather than
    // an error.
    const BookRecord*  find_most_recent() const;

    // Return the highest `last_read_seq` currently stamped on any
    // record (0 when none have been opened yet). Callers stamp
    // freshly opened books with `max_read_seq() + 1` so the latest
    // open is always strictly newer than any prior open, regardless
    // of how time(nullptr) behaves across resets.
    int  max_read_seq() const;

    // Find or insert. Returns nullptr if the DB is full. Newly
    // inserted records get a fresh id and zero/empty fields.
    BookRecord* find_or_add(const char* path);

    // Helper: build "{id:04d}.thumb" into `out`.
    static void make_thumb_name(int id, char* out, size_t out_size);

    // Helper: full SD path of a thumbnail file.
    //   "/sdcard/temp/thumb/<thumb_name>"
    static void make_thumb_path(const char* thumb_name,
                                char* out, size_t out_size);

 private:
    bool parse_line(char* line);

    char         db_path_[MAX_PATH_LEN];
    BookRecord*  records_;     // PSRAM array of MAX_BOOKS_IN_DB
    int          count_;
    int          next_id_;
};

constexpr const char* BOOKS_DB_PATH = "/sdcard/temp/books.tsv";
constexpr const char* THUMB_DIR     = "/sdcard/temp/thumb";

}  // namespace ps3::library
