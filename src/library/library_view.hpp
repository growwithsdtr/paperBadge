#pragma once

#include "library.hpp"

namespace ps3::font { class XTEinkFont; }
namespace ps3::library { class BookDb; }

namespace ps3::library_view {

// Layout (logical screen coords). Public so main.cpp can hit-test.
//
//   y = 60..170    : Top zone is now a 4-slot button strip
//                    (GoUp / PagePrev / PageNext / LastBook). Each
//                    slot is screen_w / 4 wide. Hidden buttons leave
//                    their slot empty.
//   y = 170 .. 960 : 3x3 portrait tile grid spanning the Middle and
//                    Bottom zones (Bookshelf claims the bottom strip
//                    when there's no book open). Tile aspect is ~2:3
//                    once the menu strip was halved — closer to a
//                    real comic cover.

constexpr int MENU_Y_TOP        = 60;     // toolbar bottom
constexpr int MENU_Y_BOTTOM     = 170;    // top zone bottom (= TILE_AREA_Y)
constexpr int MENU_BUTTON_COUNT = 4;

enum class MenuButton {
    GoUp,
    PagePrev,
    PageNext,
    LastBook,   // jump straight back into the most recently read
                // CBZ (handy after a hardware reset wakes the device
                // from deep sleep — session restore only catches
                // wake from sleep through the firmware, not a cold
                // reset boot)
    None,
};

// Whether a given button slot is shown given the current library
// state. Hidden buttons are skipped both visually and from the tap
// hit-test.
bool menu_button_visible(MenuButton btn, const ps3::library::Library& lib);

// Hit-test a Top-zone tap (Top zone caller already verified). Returns
// the visible button at that x, or MenuButton::None if the slot is
// hidden / x is out of range.
MenuButton menu_button_hit_test(int tap_x,
                                const ps3::library::Library& lib);

constexpr int TILE_COLS         = 3;
constexpr int TILE_ROWS         = 3;
constexpr int TILE_AREA_X       = 0;
constexpr int TILE_AREA_Y       = 170;    // = MENU_Y_BOTTOM
constexpr int TILE_AREA_W       = 540;
constexpr int TILE_AREA_H       = 790;    // 960 - TILE_AREA_Y
constexpr int TILE_OUTER_MARGIN = 10;
constexpr int TILE_INNER_GAP    = 10;
constexpr int TILE_W = (TILE_AREA_W - 2 * TILE_OUTER_MARGIN
                        - (TILE_COLS - 1) * TILE_INNER_GAP) / TILE_COLS;
constexpr int TILE_H = (TILE_AREA_H - 2 * TILE_OUTER_MARGIN
                        - (TILE_ROWS - 1) * TILE_INNER_GAP) / TILE_ROWS;

struct TileRect { int x, y, w, h; };
TileRect tile_rect(int idx);

// Render the library view: 4-button menu in the Top zone + a 3x3
// portrait tile grid spanning the Middle + Bottom zones. Each
// populated tile gets a 1 px border and either a thumbnail (if the
// book has one in `db`) or the entry name word-wrapped to fill the
// tile (vertically centered).
void render(const ps3::library::Library& lib,
            const ps3::library::BookDb& db,
            const ps3::font::XTEinkFont& font);

// Hit-test a tap (logical coords). Returns the entry index (0 ..
// count-1) if the tap lands inside a populated tile, or -1 otherwise.
int hit_test(int tap_x, int tap_y, const ps3::library::Library& lib);

}  // namespace ps3::library_view
