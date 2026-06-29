#pragma once

namespace ps3::font { class XTEinkFont; }

namespace ps3::menu {

// Which screen the menu is currently overlaying. Menu rows differ
// between the two contexts: Bookshelf shows contrast + thumbnail
// generation, Reading shows reading-side contrast, the page-turn
// full-refresh cadence, and the page-flip binding direction.
enum class Context {
    Bookshelf,
    Reading,
};

// What happened on a menu tap, so the caller (main loop) knows
// whether to also drop page_loader caches or repaint behind the
// overlay.
enum class TapResult {
    None,
    ContrastChanged,       // bookshelf_contrast or reading_contrast,
                           // depending on the active context — caller
                           // re-renders the menu and (in Reading)
                           // drops the pre-decoded page ring
    FullRefreshChanged,    // full_refresh_pages was cycled — caller
                           // re-renders the menu; the next page turn
                           // picks up the new period naturally so no
                           // cache work is needed
    PageDirectionChanged,  // right_binding was toggled (Reading only)
                           // — caller re-renders the menu so the new
                           // 左綴じ/右綴じ label shows up. The actual
                           // tap mapping is read fresh every tap from
                           // settings::state().right_binding so no
                           // cache invalidation is needed
    ThumbnailRegenAll,     // Bookshelf only: user tapped the サムネ
                           // イル作成 row's 全更新 button — caller
                           // closes the menu and runs the thumbnail
                           // generation with force=true (regenerate
                           // every CBZ thumb)
    ThumbnailRegenMissing, // Bookshelf only: 差分のみ button — same
                           // path but force=false (skip cached
                           // thumbs, only generate missing ones)
    OutsideList,           // tap fell below the visible rows —
                           // caller closes the menu (same affordance
                           // as settings)
};

// Paint the menu rows onto the active framebuffer. Toolbar (y=0..
// TOOLBAR_HEIGHT) is left to the caller, mirroring settings::render.
void render(const ps3::font::XTEinkFont& font, Context ctx);

// Hit-test a tap (logical coords) against the menu rows. The Top
// zone is handled by the caller (closes the menu), so this function
// only sees taps that fall in the body region. dispatch_tap calls
// settings::save() automatically whenever a row mutates. `font` is
// needed because the Bookshelf サムネイル作成 row splits the right
// half into two text buttons whose hit-test x-bounds depend on the
// font metrics shared with render().
TapResult dispatch_tap(int x, int y, Context ctx,
                       const ps3::font::XTEinkFont& font);

}  // namespace ps3::menu
