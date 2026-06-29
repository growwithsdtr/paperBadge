#pragma once

#include "../library/library.hpp"   // for MAX_PATH_LEN

namespace ps3::session {

// What to restore on the next boot. Settings is intentionally not
// representable here: a deep-sleep entered from the settings screen
// records the *underlying* state (Bookshelf or Reading) so the user
// returns to whatever they were doing before opening settings.
enum class Kind {
    None,
    Bookshelf,
    Reading,
};

struct State {
    Kind kind = Kind::None;
    char folder[ps3::library::MAX_PATH_LEN] = {};   // current shelf folder
    char book[ps3::library::MAX_PATH_LEN]   = {};   // Reading only
    int  page = 0;                                  // Bookshelf only —
                                                    // pagination page index.
                                                    // Reading uses books.tsv's
                                                    // last_page instead.
};

// Persist `state` to /sdcard/temp/session.tsv via .tmp + rename.
// Kind::None is treated as a request to delete the file (same as
// clear()) so the next boot starts fresh.
bool save(const State& state);

// Read /sdcard/temp/session.tsv. If the file is missing, empty, or
// malformed the returned State has kind=None.
State load();

// Delete the session file if present. Called right after a
// successful load() so a subsequent ordinary reset (without going
// through deep sleep) doesn't keep restoring the same screen.
void clear();

}  // namespace ps3::session
