#pragma once

namespace ps3::comic {

// Seven-zone tap classification:
//
//   ┌──────────────────────────────────┐  y = 0
//   │           Toolbar                │  fixed-height status strip
//   ├──────────────────────────────────┤  y = TOOLBAR_HEIGHT
//   │             Top                  │  ~25 % of below-toolbar
//   ├──────────────────┬───────────────┤
//   │   MiddleLeft     │  MiddleRight  │  the rest of the screen,
//   │                  │               │  minus the slim Bottom band
//   ├──────────────────┼───────────────┤  y = h - BOTTOM_HEIGHT
//   │   BottomLeft     │  BottomRight  │  page-indicator strip
//   └──────────────────┴───────────────┘  y = h
//
// Zone semantics depend on the top-level app state:
//   Toolbar       : icon-driven — sleep + full-screen toggle wired up,
//                   menu / setting slots reserved for future features.
//   Top           : "close book" while reading; 4-button menu strip on
//                   the bookshelf (GoUp / PagePrev / PageNext /
//                   Thumbnail), each slot 1/4 of the screen width.
//   MiddleLeft/   : page navigation (reading) or entry hit-test
//     MiddleRight   (bookshelf).
//   BottomLeft    : page-jump slider menu (reading); part of the
//                   tile hit-test (bookshelf).
//   BottomRight   : free slot (reading) — the in-page UI toggle that
//                   used to live here moved to the toolbar Full icon;
//                   part of the tile hit-test (bookshelf).

enum class TouchZone {
    None,
    Toolbar,
    Top,
    MiddleLeft,
    MiddleRight,
    BottomLeft,
    BottomRight,
};

constexpr int TOOLBAR_HEIGHT = 60;
// Top band height, sized to fit the bookshelf 4-button menu (or the
// reading-mode "close book" tap target) while leaving most of the
// screen for the tile grid / comic page.
constexpr int TOP_BAND_PX    = 110;
// Bottom strip is sized to fit the page-indicator text exactly
// (~36 px font + a few px of padding) rather than claiming a full
// quarter of the screen.
constexpr int BOTTOM_HEIGHT  = 50;

// `screen_w` and `screen_h` should be the logical (rotated) display
// size. Out-of-range coords return TouchZone::None.
inline TouchZone classify(int x, int y, int screen_w, int screen_h) {
    if (x < 0 || y < 0 || x >= screen_w || y >= screen_h) {
        return TouchZone::None;
    }
    if (y < TOOLBAR_HEIGHT) return TouchZone::Toolbar;

    const int half_w = screen_w / 2;
    if (y >= screen_h - BOTTOM_HEIGHT) {
        return (x < half_w) ? TouchZone::BottomLeft : TouchZone::BottomRight;
    }

    const int below_y = y - TOOLBAR_HEIGHT;
    if (below_y < TOP_BAND_PX) return TouchZone::Top;
    return (x < half_w) ? TouchZone::MiddleLeft : TouchZone::MiddleRight;
}

// --- Toolbar icon layout ---------------------------------------------------
//
// The toolbar arranges square-ish icon slots left-to-right. We resolve
// a Toolbar tap into a specific icon by integer-dividing the offset
// from the left edge by TOOLBAR_ICON_WIDTH.
enum class ToolbarIcon {
    None,
    Sleep,
    Menu,       // open the per-screen Menu overlay (bookshelf /
                // reading-specific quick adjustments — contrast etc.)
    Setting,    // open the system-wide Settings screen (shared across
                // bookshelf / reading)
    Full,       // toggle off the in-page UI (Reading mode only)
    Reload,     // force a GC16Full physical refresh on whatever
                // framebuffer is currently displayed
    Screenshot, // capture the on-screen framebuffer to a PNG file
                // under /sdcard/screenshot/
};

constexpr int TOOLBAR_PADDING_X    = 6;
// Slot width per icon. 64 px was tight even with 5 slots (5 × 64 +
// margin = 326 px out of 540 logical width); 6 slots take 390 px and
// still clear the battery indicator on the right edge.
constexpr int TOOLBAR_ICON_WIDTH   = 64;

// Map a Toolbar tap's logical x to a specific icon, or None if the
// tap doesn't fall on any actionable slot.
inline ToolbarIcon toolbar_icon_at(int x) {
    if (x < TOOLBAR_PADDING_X) return ToolbarIcon::None;
    const int slot = (x - TOOLBAR_PADDING_X) / TOOLBAR_ICON_WIDTH;
    switch (slot) {
        case 0:  return ToolbarIcon::Sleep;
        case 1:  return ToolbarIcon::Menu;
        case 2:  return ToolbarIcon::Setting;
        case 3:  return ToolbarIcon::Full;
        case 4:  return ToolbarIcon::Reload;
        case 5:  return ToolbarIcon::Screenshot;
        default: return ToolbarIcon::None;
    }
}

}  // namespace ps3::comic
