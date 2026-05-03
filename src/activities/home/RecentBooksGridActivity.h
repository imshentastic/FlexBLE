#pragma once
#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

// Lua-fork-style grid view of recent books. 3x3 covers per page, paged when
// there are more than 9 books, on-demand thumbnail generation behind a
// loading popup. Replaces the plain-list RecentBooksActivity at the dispatch
// site (ActivityManager::goToRecentBooks); the original activity file is
// kept untouched for clean upstream merges.
class RecentBooksGridActivity final : public Activity {
 public:
  static constexpr int BOOKS_PER_PAGE = 9;   // 3 cols x 3 rows
  static constexpr int MAX_BOOKS = BOOKS_PER_PAGE * 2;
  static constexpr int COVER_HEIGHT = 180;
  static constexpr int COVER_WIDTH = 123;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  std::vector<RecentBook> recentBooks;
  // Track which page's covers we've already attempted to generate, so we
  // only run the heavy Epub/Xtc thumb path when the user pages.
  int loadedPageStart = -1;

  void loadRecentBooks();
  void loadPageCovers(int pageStart);

 public:
  explicit RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooksGrid", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
