#include "session.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>

namespace ps3::session {

namespace {
constexpr const char* TAG          = "session";
constexpr const char* SESSION_PATH = "/sdcard/temp/session.tsv";
constexpr const char* SESSION_TMP  = "/sdcard/temp/session.tsv.tmp";

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Bookshelf: return "bookshelf";
        case Kind::Reading:   return "reading";
        default:              return "none";
    }
}

Kind kind_from_token(const char* s) {
    if (!s) return Kind::None;
    if (std::strcmp(s, "bookshelf") == 0) return Kind::Bookshelf;
    if (std::strcmp(s, "reading")   == 0) return Kind::Reading;
    return Kind::None;
}

// Carve out a tab-separated field in-place: replaces the next
// '\t', '\n' or '\r' with NUL and advances `*state` past it. Used
// for parsing the single-line TSV format.
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
        *state = p;
    }
    return tok;
}

}  // namespace

bool save(const State& state) {
    if (state.kind == Kind::None) {
        clear();
        return true;
    }

    FILE* fp = std::fopen(SESSION_TMP, "w");
    if (!fp) {
        ESP_LOGE(TAG, "fopen %s for write failed", SESSION_TMP);
        return false;
    }

    if (state.kind == Kind::Bookshelf) {
        // 3rd column = pagination page index, so a deep-sleep on
        // page 2 of a long folder restores back to page 2.
        std::fprintf(fp, "bookshelf\t%s\t%d\n", state.folder, state.page);
    } else {  // Reading
        std::fprintf(fp, "reading\t%s\t%s\n", state.folder, state.book);
    }
    std::fclose(fp);

    std::remove(SESSION_PATH);  // FATFS rename refuses to overwrite
    if (std::rename(SESSION_TMP, SESSION_PATH) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s failed", SESSION_TMP, SESSION_PATH);
        return false;
    }
    ESP_LOGI(TAG, "saved %s: kind=%s folder=%s book=%s page=%d",
             SESSION_PATH, kind_name(state.kind),
             state.folder, state.book, state.page);
    return true;
}

State load() {
    State result;
    FILE* fp = std::fopen(SESSION_PATH, "r");
    if (!fp) {
        ESP_LOGI(TAG, "no session at %s — boot into root shelf",
                 SESSION_PATH);
        return result;
    }

    // 2 × MAX_PATH_LEN + tabs + newline + slack
    char line[ps3::library::MAX_PATH_LEN * 2 + 64];
    if (!std::fgets(line, sizeof(line), fp)) {
        ESP_LOGW(TAG, "empty session file");
        std::fclose(fp);
        return result;
    }
    std::fclose(fp);

    // Strip CR/LF.
    size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }

    char* state    = line;
    char* f_kind   = tsv_next(&state);
    char* f_folder = tsv_next(&state);
    char* f_third  = tsv_next(&state);   // book (Reading) | page (Bookshelf)

    result.kind = kind_from_token(f_kind);
    if (f_folder && f_folder[0]) {
        std::strncpy(result.folder, f_folder, sizeof(result.folder) - 1);
        result.folder[sizeof(result.folder) - 1] = '\0';
    }
    if (f_third && f_third[0]) {
        if (result.kind == Kind::Reading) {
            std::strncpy(result.book, f_third, sizeof(result.book) - 1);
            result.book[sizeof(result.book) - 1] = '\0';
        } else if (result.kind == Kind::Bookshelf) {
            const int p = std::atoi(f_third);
            result.page = (p > 0) ? p : 0;
        }
    }

    ESP_LOGI(TAG, "loaded session: kind=%s folder=%s book=%s page=%d",
             kind_name(result.kind), result.folder, result.book, result.page);
    return result;
}

void clear() {
    if (std::remove(SESSION_PATH) == 0) {
        ESP_LOGI(TAG, "cleared %s", SESSION_PATH);
    }
}

}  // namespace ps3::session
