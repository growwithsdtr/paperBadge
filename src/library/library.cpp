#include "library.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include <dirent.h>
#include <sys/stat.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace ps3::library {

namespace {
constexpr const char* TAG = "library";

bool ends_with_icase(const char* s, const char* suffix) {
    const size_t ls = std::strlen(s);
    const size_t lp = std::strlen(suffix);
    if (lp > ls) return false;
    return strcasecmp(s + (ls - lp), suffix) == 0;
}

}  // namespace

// strcmp-style comparator that treats runs of ASCII digits as
// integers, so "1巻" sorts before "10巻". Non-digit bytes are
// compared byte-wise — fine for UTF-8 since we only need a stable
// ordering, not locale-aware collation. Leading zeros are stripped
// before length comparison so "01" and "1" tie numerically.
int natural_compare(const char* a, const char* b) {
    for (;;) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca == 0 || cb == 0) {
            return static_cast<int>(ca) - static_cast<int>(cb);
        }
        const bool da = ca >= '0' && ca <= '9';
        const bool db = cb >= '0' && cb <= '9';
        if (da && db) {
            while (*a == '0') ++a;
            while (*b == '0') ++b;
            const char* sa = a;
            const char* sb = b;
            while (*a >= '0' && *a <= '9') ++a;
            while (*b >= '0' && *b <= '9') ++b;
            const ptrdiff_t la = a - sa;
            const ptrdiff_t lb = b - sb;
            if (la != lb) return la < lb ? -1 : 1;
            const int cmp = std::strncmp(sa, sb, static_cast<size_t>(la));
            if (cmp != 0) return cmp;
            continue;
        }
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        ++a;
        ++b;
    }
}

Library::~Library() {
    if (entries_) {
        std::free(entries_);
        entries_ = nullptr;
    }
}

bool Library::open(const char* root_path) {
    if (!root_path) return false;
    if (!entries_) {
        entries_ = static_cast<Entry*>(heap_caps_malloc(
            sizeof(Entry) * MAX_RAW_ENTRIES, MALLOC_CAP_SPIRAM));
        if (!entries_) {
            ESP_LOGE(TAG, "PSRAM alloc for %d entries (%u bytes) failed",
                     MAX_RAW_ENTRIES,
                     (unsigned)(sizeof(Entry) * MAX_RAW_ENTRIES));
            return false;
        }
    }
    std::strncpy(root_path_, root_path, sizeof(root_path_) - 1);
    root_path_[sizeof(root_path_) - 1] = '\0';
    std::strncpy(cur_path_, root_path_, sizeof(cur_path_) - 1);
    cur_path_[sizeof(cur_path_) - 1] = '\0';
    return refresh();
}

bool Library::open_at(const char* root_path, const char* cur_path) {
    if (!open(root_path)) return false;
    if (!cur_path || !cur_path[0]) return true;
    if (std::strcmp(cur_path, root_path_) == 0) return true;  // == root

    // Reject cur_path that isn't a subdirectory of root (avoid
    // silently displaying somewhere unrelated if the session file
    // was tampered with, or if root_path was changed across reboots).
    const size_t root_len = std::strlen(root_path_);
    if (std::strncmp(cur_path, root_path_, root_len) != 0
        || cur_path[root_len] != '/') {
        ESP_LOGW(TAG, "open_at: %s not under %s — staying at root",
                 cur_path, root_path_);
        return true;
    }

    struct stat st;
    if (::stat(cur_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "open_at: %s does not exist — staying at root",
                 cur_path);
        return true;
    }

    std::strncpy(cur_path_, cur_path, sizeof(cur_path_) - 1);
    cur_path_[sizeof(cur_path_) - 1] = '\0';
    return refresh();
}

bool Library::refresh() {
    if (!entries_) return false;
    total_ = 0;
    page_  = 0;

    DIR* dir = opendir(cur_path_);
    if (!dir) {
        ESP_LOGE(TAG, "opendir(%s) failed", cur_path_);
        return false;
    }

    int dropped = 0;
    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        const char* name = de->d_name;
        if (!name[0] || is_hidden_name(name)) continue;

        // d_type isn't always populated on FATFS; do a stat() to be
        // sure. Path = cur_path_ + "/" + name. The buffer is sized to
        // worst case (MAX_PATH_LEN + NAME_MAX) so GCC doesn't flag a
        // theoretical truncation it cannot prove away.
        char full[MAX_PATH_LEN + 256];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        std::snprintf(full, sizeof(full), "%s/%s", cur_path_, name);
#pragma GCC diagnostic pop
        struct stat st;
        if (::stat(full, &st) != 0) continue;

        EntryKind kind;
        if (S_ISDIR(st.st_mode)) {
            kind = EntryKind::Folder;
        } else if (S_ISREG(st.st_mode) &&
                   (ends_with_icase(name, ".cbz") ||
                    ends_with_icase(name, ".zip"))) {
            // .zip is treated as the same archive format as .cbz —
            // the latter is just a renamed ZIP, and miniz handles
            // both transparently.
            kind = EntryKind::Cbz;
        } else {
            // .epub and other regular files are filtered out for now.
            continue;
        }

        if (total_ >= MAX_RAW_ENTRIES) {
            ++dropped;
            continue;
        }
        entries_[total_].kind = kind;
        std::strncpy(entries_[total_].name, name, MAX_NAME_LEN - 1);
        entries_[total_].name[MAX_NAME_LEN - 1] = '\0';
        ++total_;
    }
    closedir(dir);

    // Folders first (kind value: Folder=0 < Cbz=1), then natural
    // (digit-aware) order inside each group so "1巻" < "10巻".
    std::sort(entries_, entries_ + total_,
              [](const Entry& a, const Entry& b) {
                  if (a.kind != b.kind) {
                      return static_cast<int>(a.kind)
                             < static_cast<int>(b.kind);
                  }
                  return natural_compare(a.name, b.name) < 0;
              });

    if (dropped > 0) {
        ESP_LOGW(TAG, "scanned %s: %d entries (dropped %d over cap %d)",
                 cur_path_, total_, dropped, MAX_RAW_ENTRIES);
    } else {
        ESP_LOGI(TAG, "scanned %s: %d entries", cur_path_, total_);
    }
    return true;
}

int Library::count() const {
    if (total_ == 0) return 0;
    const int start = page_ * MAX_ENTRIES;
    if (start >= total_) return 0;
    const int rem = total_ - start;
    return rem < MAX_ENTRIES ? rem : MAX_ENTRIES;
}

const Entry& Library::at(int idx) const {
    return entries_[page_ * MAX_ENTRIES + idx];
}

int Library::total_pages() const {
    if (total_ == 0) return 0;
    return (total_ + MAX_ENTRIES - 1) / MAX_ENTRIES;
}

bool Library::can_page_next() const {
    return page_ + 1 < total_pages();
}

bool Library::page_prev() {
    if (!can_page_prev()) return false;
    --page_;
    return true;
}

bool Library::page_next() {
    if (!can_page_next()) return false;
    ++page_;
    return true;
}

void Library::go_to_page(int page) {
    const int total = total_pages();
    if (total <= 0) {
        page_ = 0;
        return;
    }
    if (page < 0) page = 0;
    if (page >= total) page = total - 1;
    page_ = page;
}

bool Library::enter(int idx) {
    if (idx < 0 || idx >= count()) return false;
    const Entry& e = at(idx);
    if (e.kind != EntryKind::Folder) return false;

    char child[MAX_PATH_LEN + MAX_NAME_LEN + 2];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    const int n = std::snprintf(child, sizeof(child), "%s/%s",
                                cur_path_, e.name);
#pragma GCC diagnostic pop
    if (n < 0 || static_cast<size_t>(n) >= sizeof(cur_path_)) {
        ESP_LOGE(TAG, "path too long entering '%s'", e.name);
        return false;
    }
    std::strncpy(cur_path_, child, sizeof(cur_path_) - 1);
    cur_path_[sizeof(cur_path_) - 1] = '\0';
    return refresh();
}

bool Library::can_go_up() const {
    return std::strcmp(cur_path_, root_path_) != 0;
}

bool Library::go_up() {
    if (!can_go_up()) return false;
    char* slash = std::strrchr(cur_path_, '/');
    if (!slash || slash == cur_path_) return false;
    *slash = '\0';
    return refresh();
}

bool Library::full_path_of(int idx, char* out, size_t out_size) const {
    if (idx < 0 || idx >= count() || !out || out_size == 0) return false;
    const Entry& e = at(idx);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    const int n = std::snprintf(out, out_size, "%s/%s", cur_path_, e.name);
#pragma GCC diagnostic pop
    return n > 0 && static_cast<size_t>(n) < out_size;
}

}  // namespace ps3::library
