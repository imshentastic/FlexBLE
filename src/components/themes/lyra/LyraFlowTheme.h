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
  return v;
}();
}  // namespace LyraFlowMetrics

class LyraFlowTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f) const override;
};
