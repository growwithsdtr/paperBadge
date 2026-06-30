#pragma once

#include <cstdint>

#include "../library/library.hpp"   // for MAX_NAME_LEN

namespace ps3::font { class XTEinkFont; }

namespace ps3::settings {

// Persistent system-wide settings shared across Bookshelf / Reading
// (and future EPUB) views. Held in memory only for now — disk
// persistence is a follow-up task. Default values match boot state:
// no auto sleep / power-off, panel orientation matches the firmware
// default (EPD_ROT_INVERTED_PORTRAIT, i.e. rotation_inverted=false).
struct State {
    int  sleep_minutes      = 0;     // 0 = none, otherwise 5/10/15
    int  power_off_minutes  = 0;     // 0 = none, otherwise 20/30/60
    bool rotation_inverted  = false; // true = panel flipped 180°
    // Sleep / power-off splash images. Empty string means "use the
    // built-in OFF text overlay" (default behaviour). When set, the
    // value is the filename of a JPEG inside /sdcard/images/.
    char sleep_image[ps3::library::MAX_NAME_LEN]     = {};
    char power_off_image[ps3::library::MAX_NAME_LEN] = {};
    // System font filename inside /sdcard/fonts/. Empty string means
    // "use the firmware-embedded font.bin" (default). The picker
    // and the loader expect XTEink-format binaries matching the
    // build's PS3_FONT_WIDTH / PS3_FONT_HEIGHT.
    char system_font[ps3::library::MAX_NAME_LEN]     = {};
    // Black-point lift levels for image display. 0 = pass-through;
    // higher levels (currently up to 6) raise the black point so
    // faded scans look denser. Bookshelf and Reading hold separate
    // values so a high lift on faded comics doesn't bleed onto a
    // shelf full of clean cover thumbnails (and vice versa). The
    // toolbar Menu opens a per-screen overlay where these get
    // adjusted.
    int  bookshelf_contrast  = 0;
    int  reading_contrast    = 0;
    // How many GL16 page turns happen between the periodic GC16Full
    // cleanups during reading. 1 = every page is a full refresh
    // (slower but no residual ghosting); higher values save flush
    // time at the cost of more lingering ghost. Allowed values come
    // from kFullRefreshOptions in src/system/menu.cpp's cycle.
    int  full_refresh_pages  = 10;
    // 0 = Fast, 1 = Balanced, 2 = Clean. Higher quality profiles
    // trade speed for lower ghosting on reveal/error/page transitions.
    int  refresh_profile     = 1;
    int  reader_font_level   = 1;  // 0=S, 1=M, 2=L, 3=XL
    int  interview_font_level = 1;
    int  japanese_font_level = 1;
    int  japanese_font_face  = 1;  // 0=IPAex Gothic, 1=BIZ UDGothic
    int  western_font_profile = 0; // 0=firmware UI sans, FontLab candidates are preview-only
    // Page-flip direction in Reading mode.
    //   false = 左綴じ (Western / current default): MiddleRight tap
    //           advances to the next page, MiddleLeft goes back.
    //   true  = 右綴じ (Japanese manga binding):    MiddleLeft tap
    //           advances to the next page, MiddleRight goes back.
    // Only affects the MiddleLeft / MiddleRight reading handler; the
    // page-jump menu, toolbar, and everything else stay the same.
    bool right_binding       = false;
};

// Where a contrast lookup is happening. The decode path picks the
// matching State field via contrast_for() and the eventual LUT
// builder follows. `Off` skips the curve entirely (used by sleep /
// power-off splash images and the loading dialog so a curated
// picture isn't bent by the user's reading-page setting).
enum class ContrastContext {
    Off,
    Bookshelf,
    Reading,
};

State& state();

// What happened on a settings tap, so the caller (main loop) knows
// whether to repaint or hand control back to the previous screen.
enum class TapResult {
    None,              // tap fell on a row that has no action yet
                       // (system font), or in the header gap above
                       // the list — caller does nothing
    ValueChanged,      // a settings value was toggled; redraw the
                       // settings screen so the new value shows up
    RotationChanged,   // panel rotation was toggled; display + touch
                       // sides have already been notified, the
                       // caller should redraw under the new
                       // orientation
    OutsideList,       // tap fell in the empty area below the row
                       // list — caller should close the settings UI
    PickSleepImage,    // sleep splash image row tapped — caller
                       // should run the image picker and write the
                       // result back into state().sleep_image
    PickPowerOffImage, // power-off splash image row tapped — same
                       // dance for state().power_off_image
    PickSystemFont,    // system-font row tapped — caller should
                       // run the font picker (/sdcard/fonts/*.bin),
                       // load the chosen file via the system-font
                       // loader, then write state().system_font and
                       // call save()
};

// Render the settings list onto the active framebuffer (toolbar
// area y=0..TOOLBAR_HEIGHT is left untouched — caller paints the
// toolbar separately). Uses the supplied font for both labels and
// values.
void render(const ps3::font::XTEinkFont& font);

// Hit-test a tap (logical coords) against the settings list and run
// the matching action. The Top zone is handled by the caller and
// closes the settings screen, so this function only sees taps that
// fall in the body region. dispatch_tap calls save() automatically
// whenever a value mutates, so the on-disk copy stays in sync
// across reset / deep-sleep without further caller involvement.
TapResult dispatch_tap(int x, int y);

// Persist state() to /sdcard/temp/settings.kv via .tmp + rename.
// Called from dispatch_tap after each toggle. Returns false on I/O
// failure but the in-memory state is unaffected.
bool save();

// Read /sdcard/temp/settings.kv into state(). Missing or unreadable
// files are treated as "use defaults" (returns true). Unknown keys
// are skipped, so adding new settings later remains forward-/
// backward-compatible.
bool load();

// Build a 256-entry black-point lift LUT for the level matching
// `ctx`. `lut[v]` returns the corrected 8-bit value for an 8-bit
// input. Callers that already have 4-bit grayscale (e.g. thumbnail
// blit) can use build_contrast_lut4() instead. ContrastContext::Off
// always produces an identity LUT.
void build_contrast_lut(uint8_t lut[256], ContrastContext ctx);

// Build a 16-entry LUT for callers that operate on 4-bit grayscale.
// `lut4[g]` (g in 0..15) returns the corrected 4-bit value. Same
// ContrastContext semantics as build_contrast_lut().
void build_contrast_lut4(uint8_t lut4[16], ContrastContext ctx);

// Read the current black-point level for `ctx` (0 if Off). Mostly
// used by the toolbar Menu handler to decide what to cycle to next.
int contrast_for(ContrastContext ctx);

// Total number of selectable levels (cycle range is 0 .. levels-1).
// Used by callers that want to render a "level N of M" indicator or
// implement a custom cycling scheme.
int contrast_levels();

// Cycle the level for `ctx` (level = (level + 1) % contrast_levels())
// and persist via save(). No-op when ctx == Off. Callers should
// follow with a redraw of the affected screen and, if Reading was
// the one that changed, page_loader::invalidate() so cached pages
// are re-decoded under the new curve.
void cycle_contrast(ContrastContext ctx);

}  // namespace ps3::settings
