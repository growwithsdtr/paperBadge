#pragma once

#include <cstddef>

namespace ps3::library {

constexpr int MAX_ENTRIES   = 9;    // 3x3 tile grid in the Middle zone.
                                    // One page of the bookshelf.
// Hard cap on entries per folder. Anything above this is silently
// dropped at scan time (with a log line). 500 * sizeof(Entry) ~= 98 KB
// of PSRAM, allocated once per Library.
constexpr int MAX_RAW_ENTRIES = 500;
// Generous so cur_path_ + "/" + dirent NAME_MAX never threatens to
// overflow even with deep nesting and long Japanese filenames.
constexpr int MAX_PATH_LEN  = 1024;
constexpr int MAX_NAME_LEN  = 192;

enum class EntryKind {
    Folder,
    Cbz,
    // Epub support is deferred until the EPUB reader exists.
};

// True if `name` starts with '.' or '_'. Used by directory walks
// (Library refresh and the recursive thumbnail generator) to skip
// hidden / metadata entries. Empty strings return false; callers
// handle the empty case alongside this check.
inline bool is_hidden_name(const char* name) {
    return name && (name[0] == '.' || name[0] == '_');
}

struct Entry {
    EntryKind kind;
    char      name[MAX_NAME_LEN];   // basename, UTF-8, NUL-terminated
};

// strcmp-style comparator that treats runs of ASCII digits as
// integers, so "1巻" sorts before "10巻". Used both by the
// bookshelf sort and by library_view to pick the lex-smallest book
// inside a folder for the folder thumbnail.
int natural_compare(const char* a, const char* b);

// Browses a fixed root folder on the SD card (typically "/sdcard/books"),
// keeps a sorted list of every entry in the folder the user is
// currently in (up to MAX_RAW_ENTRIES), and presents one MAX_ENTRIES
// page of them at a time via at()/count(). enter() descends into a
// subfolder, go_up() moves back toward the root, page_prev/next()
// flip pages within the current folder.
//
// Single-instance, owned by the application; not thread-safe.
class Library {
 public:
    Library() = default;
    ~Library();

    Library(const Library&)            = delete;
    Library& operator=(const Library&) = delete;

    // Open the bookshelf at `root_path`. Allocates the entries buffer
    // in PSRAM on first call. The library remembers `root_path` as
    // the home position; go_up() will refuse to go above it.
    bool open(const char* root_path);

    // Same as open(root_path), but starts inside `cur_path` instead
    // of at the root. `cur_path` must be either equal to `root_path`
    // or a subdirectory under it; otherwise we silently fall back
    // to displaying the root. Used by the deep-sleep session
    // restore so a deep-sleep -> reset cycle returns to the folder
    // the user was browsing.
    bool open_at(const char* root_path, const char* cur_path);

    // Re-scan the current folder. Useful after the SD has been
    // modified externally; called automatically by enter()/go_up().
    // Resets the page index to 0.
    bool refresh();

    // Descend into the folder at index `idx` (current-page index).
    // Fails (returns false) if the entry is not a folder, the index
    // is out of range, or the child folder cannot be opened.
    bool enter(int idx);

    // Move one level up toward the root. No-op + returns false at the
    // root.
    bool go_up();
    bool can_go_up() const;

    // Read accessors. `count()` is the number of entries on the
    // current page (0..MAX_ENTRIES); `at(idx)` indexes into that page.
    const char*  root_path()    const { return root_path_; }
    const char*  current_path() const { return cur_path_; }
    int          count()        const;
    const Entry& at(int idx)    const;
    int          total_count()  const { return total_; }

    // Pagination across the current folder.
    int  page()         const { return page_; }
    int  total_pages()  const;
    bool can_page_prev() const { return page_ > 0; }
    bool can_page_next() const;
    bool page_prev();   // returns true if the page changed
    bool page_next();

    // Jump straight to a specific pagination page index. Used by the
    // session restore path so a deep-sleep on page N of a long
    // folder returns to page N. Out-of-range values are clamped to
    // a valid index (0 if the folder is empty).
    void go_to_page(int page);

    // Build the full filesystem path of the entry at `idx` (current
    // page) into `out`. Returns false if `idx` is out of range or the
    // buffer is too small.
    bool full_path_of(int idx, char* out, size_t out_size) const;

 private:
    char    root_path_[MAX_PATH_LEN] = {};
    char    cur_path_[MAX_PATH_LEN]  = {};
    Entry*  entries_ = nullptr;     // PSRAM, capacity MAX_RAW_ENTRIES
    int     total_   = 0;           // entries actually filled
    int     page_    = 0;
};

}  // namespace ps3::library
