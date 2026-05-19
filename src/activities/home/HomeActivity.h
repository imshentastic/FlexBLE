#pragma once
#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
 public:
  // Keep one rendered carousel frame in RAM. Additional frames remain available
  // through the SD snapshot cache and are paged in on demand.
  static constexpr int kCarouselFrameCount = 1;
  // Must be >= LyraCarouselMetrics::values.homeRecentBooksCount (asserted in .cpp)
  static constexpr int kMaxCachedBooks = 3;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int lastCarouselBookIndex = 0;  // remembered position when leaving carousel row
  // FlexBLE Collections — leftmost visible spine index on the bookshelf
  // strip; adjusted when the focused spine would otherwise scroll out of view.
  int shelfScrollOffset = 0;
  // True when the cursor is on the collection-name header (the "Favorites"
  // tab) rather than on a book in the shelf row. Header is a separate
  // focus state above the books — L/R cycles the active collection,
  // Down/Confirm enters the books, Up returns to the carousel. Reset on
  // onEnter so navigation always starts on the carousel.
  bool shelfHeaderFocused = false;
  // Set once shelf cover BMPs have been resolved + generated-if-missing for
  // the current home session. Reset on onEnter so a freshly added book picks
  // up its thumbnail on the next return-to-Home.
  bool shelfCoversLoaded = false;
  // Suppresses the short-press Confirm handler when the user just held Confirm
  // long enough to trigger the book action menu — otherwise the matching
  // release would immediately open the book they were just acting on. Same
  // pattern as FileBrowserActivity::longPressConfirmHandled.
  bool longPressConfirmHandled = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasReadingStats = false;
  bool hasBookmarks = false;
  bool hasOpdsServers = false;
  bool minimalMenuOpen = false;
  bool minimalSuppressInitialFrontRelease = false;
  int minimalMenuIndex = 0;
  int minimalHomeNavIndex = -1;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  float currentBookProgressPercent = -1.0f;
  BookReadingStats currentBookStats;
  GlobalReadingStats globalStats;

  // Per-book stats and progress cached at onEnter() to avoid SD reads during navigation.
  std::array<BookReadingStats, kMaxCachedBooks> cachedBookStats{};
  std::array<float, kMaxCachedBooks> cachedBookProgress{};
  bool bookStatsCached = false;

  uint8_t* carouselFrames[kCarouselFrameCount] = {};
  bool carouselFramesReady = false;
  bool carouselWarmupPending = false;

  std::vector<RecentBook> recentBooks;
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onContinueReading();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onReadingStatsOpen();
  void onBookmarksOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  bool preRenderCarouselFrames(bool showProgressPopup = false);
  void freeCarouselFrames();
  bool allocateCarouselFrameSlots(int targetFrameCount);
  bool buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                              bool showProgressPopup = false);
  bool loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx);
  int chooseCarouselEvictionSlot(int centerIdx, int bookCount,
                                 std::optional<int> protectedBookIdx = std::nullopt) const;
  void renderCarouselFrameToCurrentBuffer(int bookIdx, BookReadingStats* outStats, float* outProgressPercent,
                                          bool* outUsedCachedStats);
  void renderCarouselFrame(int bookIdx, int slotIdx);
  void updateSlidingWindowCache(int centerIdx, int bookCount);
  int getHighlightedBookIndex() const;
  void updateHighlightedBookContext();
  void loadRecentBooks(int maxBooks);
  void loadAllBookStats();
  void loadRecentCovers(int coverHeight);
  // FlexBLE Collections — generate BMP thumbnails at the bookshelf's exact
  // cell dimensions for every book in the active collection that doesn't
  // already have one cached on SD. Mirrors loadRecentCovers but uses the
  // shelf cell size instead of the home cover size. Cheap on subsequent
  // calls because Storage.exists() short-circuits once thumbs are written.
  void loadShelfCovers(int cellWidth, int cellHeight);
  // Long-press Confirm helpers (used by both the carousel and the Flow
  // bookshelf). getFocusedBookPath returns the file path of the book under
  // the cursor when the user is on either a carousel slot or a shelf slot.
  // Empty string => not on a book (e.g. cursor is on the menu icon bar).
  std::string getFocusedBookPath() const;
  // Opens the FileBrowserActionActivity picker for `bookPath` with a menu
  // tailored to the home screen (delete, delete cache, mark finished /
  // unfinished, add/remove favorites, remove from recent books). Result
  // handler performs the action, refreshes recents, and requests a redraw.
  void showHomeBookActionMenu(const std::string& bookPath);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
