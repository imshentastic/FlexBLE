#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Flow ("iPod-style") carousel theme. Inherits LyraTheme wholesale and only
// overrides the home-screen recent-book carousel. Ported from the Lua-fork
// FlowTheme; date / today-clock / per-book reading-time chrome have been
// removed because their stats subsystems do not exist in this firmware.
namespace LyraFlowMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverHeight = 320;       // 25-kai book ratio (~0.7) — center cover
  v.homeCoverTileHeight = 360;   // hugs the bottom of the cover so the menu sits close
  v.homeRecentBooksCount = 5;    // matches the 5 carousel slots visible at once
                                 // (center + 2 sides each direction). Capped at 5 to
                                 // avoid first-boot OOM during sequential thumb gen on
                                 // ESP32-C3 — see HomeActivity::loadRecentCovers.
  v.homeTopPadding = 41;         // tighter than Lyra's 56: Flow's home header has no
                                 // title/subtitle, only the battery icon (rendered at
                                 // y+5 inside the rect), so the rest of the rect was
                                 // dead space. Shrinking it shifts the carousel title
                                 // and covers up ~15 px closer to the battery row.
  v.homeMenuTopOffset = 28;      // 41 + 360 + 28 = 429, ~3 px above where the
                                 // menu sat at Lyra's original 56 padding. The 28 px
                                 // gap between cover bottom and menu top houses the
                                 // per-book reading-time indicator drawn under the
                                 // center cover.
  return v;
}();
}  // namespace LyraFlowMetrics

class LyraFlowTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;
  // Flow-only override of the home menu. Two-anchor pagination with a
  // sticky bit so the second page stays in view as the cursor scrolls
  // back up through the overlap zone — switches back to page 1 only
  // when the cursor crosses page 2's top boundary.
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;

  // FlexBLE Collections — bookshelf strip between the carousel and the icon
  // bar. Renders the collection name above a row of cover thumbnails. Each
  // entry in `coverPaths` is the absolute path to a BMP thumb on SD (empty
  // string => render a placeholder card for that book).
  //
  // Selection model has THREE focus states on this row (driven by the
  // caller, encoded across the two flags):
  //   • `headerFocused = true` and `selectedSpineIndex = -1`: the user is
  //     on the collection tab itself; tab is drawn bold and flanked with
  //     left/right arrows when `hasMultipleCollections` is true to hint at
  //     L/R cycling.
  //   • `headerFocused = false` and `selectedSpineIndex >= 0`: the user is
  //     on one of the books; the matching cell gets a focus ring.
  //   • `headerFocused = false` and `selectedSpineIndex = -1`: nothing on
  //     this row is focused (cursor is on the carousel or icon bar).
  //
  // `scrollOffset` is the leftmost visible book index when the collection
  // overflows the viewport — pass 0 if no scroll.
  //
  // `focusedBookTitle` is rendered centered BELOW the cell row when a
  // book is focused (`selectedSpineIndex >= 0`). Pass nullptr or empty
  // when nothing is focused or when the title shouldn't render. Pattern
  // mirrors the Lyra carousel's title affordance under the cover.
  void drawBookshelfStrip(GfxRenderer& renderer, Rect rect, const char* collectionName,
                          const std::vector<std::string>& coverPaths, int selectedSpineIndex, int scrollOffset,
                          bool headerFocused, bool hasMultipleCollections,
                          const char* focusedBookTitle = nullptr) const;

 public:
  // Set by HomeActivity right before invoking drawRecentBookCover. When
  // true, the theme bypasses the 5 BMP loads (4 side covers + 1 center
  // cover) and trusts that the framebuffer was restored from a previous
  // render where the same covers were painted at the same positions.
  // Title and footer text still render because drawHeader's fillRect
  // partially wipes the title strip on every frame. The flag is one-
  // shot: drawRecentBookCover resets it before returning, so a stale
  // value can't leak into the next paint.
  //
  // Lives on the theme rather than a function parameter because the
  // drawRecentBookCover signature is part of the BaseTheme virtual
  // contract — overloading it would touch every other theme. mutable
  // because drawRecentBookCover is const.
  mutable bool skipCarouselCoverLoads = false;

 private:
  // Tracks "is page 2 currently shown" across renders. mutable because
  // drawButtonMenu is a const method (theme contract). State is purely
  // a UX hint; resets itself whenever the cursor lands unambiguously
  // inside page 1's exclusive zone.
  mutable bool stickyMenuPage2 = false;
};
