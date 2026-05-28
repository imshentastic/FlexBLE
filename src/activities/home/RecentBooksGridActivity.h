#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class RecentBooksGridActivity final : public Activity {
 public:
  static constexpr int BOOKS_PER_PAGE = 9;  // 3 cols x 3 rows
  static constexpr int MAX_GRID_BOOKS = BOOKS_PER_PAGE * 2;
  static constexpr int COVER_HEIGHT = 180;
  static constexpr int COVER_WIDTH = 123;

  // CrumBLE #81: dual-mode constructor. Default form preserves the legacy
  // RECENT_BOOKS view (max MAX_GRID_BOOKS books, ordered by last-opened).
  // The collectionId overload sources books from CollectionsStore --
  // typically the currently-active collection -- so the icon-bar entry
  // becomes a "Bookshelf" grid over the user's collections.
  explicit RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooksGrid", renderer, mappedInput) {}
  RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string collectionId)
      : Activity("RecentBooksGrid", renderer, mappedInput), collectionId_(std::move(collectionId)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct BookState {
    RecentBook book;
    float progress = -1.0f;
    bool progressLoaded = false;
  };
  static constexpr int NO_PAGE_LOADED = -1;

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool longPressFired = false;
  std::vector<BookState> recentBooks;
  int loadedPageStart = NO_PAGE_LOADED;
  // CrumBLE: when non-empty, loadRecentBooks() pulls from the given
  // CollectionsStore collection instead of RECENT_BOOKS. Title gracefully
  // falls back to filename for books without cached metadata. Empty
  // string = legacy RECENT_BOOKS mode.
  std::string collectionId_;

  bool isCollectionMode() const { return !collectionId_.empty(); }
  void loadRecentBooks();
  void loadPageCovers(int pageStart);
  void ensureProgressLoaded(int index);
  void reloadAfterBookAction();
  void promptDeleteBook(const RecentBook& book);
  void promptRemoveBook(const std::string& path, const std::string& title);
  void showBookActionMenu(int bookIndex, bool ignoreInitialConfirmRelease = false);
};
