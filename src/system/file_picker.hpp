#pragma once

#include <cstddef>

namespace ps3::font { class XTEinkFont; }

namespace ps3::file_picker {

// Pickable directory configuration. `dir` is an absolute SD path
// (e.g. "/sdcard/images") and `exts` is a list of lowercase
// extensions including the dot (e.g. {".jpg", ".png"}). The match
// is case-insensitive. Both pointers must outlive the run() call —
// string literals are fine.
//
// `include_default` controls whether the picker shows a leading
// "default" sentinel tile that resolves to an empty string. Useful
// for image pickers (default = "no image, fall back to OFF text")
// but redundant for callers that always need to bind to *some*
// real file (e.g. the system font, where "default" would just
// alias the first real file in the list).
struct Config {
    const char* dir;
    const char* const* exts;
    int                ext_count;
    bool               include_default = true;
};

// Modal screen that lets the user pick one file under
// `config.dir` whose name ends in one of `config.exts`. The first
// tile is a "default" sentinel (empty string in the result) for
// "no file" / "use the built-in default" semantics. Top-zone
// buttons cancel / page-prev / page-next; tapping a tile commits
// the selection.
//
// `current` is the previously chosen filename (empty = "default").
// On confirm: returns true and writes the chosen filename (or empty
// for "default") into `out`. On cancel — Top "go up", toolbar tap,
// area outside the list — returns false and `out` is left
// untouched.
//
// The picker takes over the screen for the duration of the call
// and blocks until the user makes a decision; callers are expected
// to repaint their own screen afterwards.
bool run(const ps3::font::XTEinkFont& font,
         const Config& config,
         const char* current,
         char* out, std::size_t out_size);

}  // namespace ps3::file_picker
