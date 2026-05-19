#pragma once
#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "./FileBrowserActivity.h"
#include "CollectionsStore.h"  // ShelfEntry
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
  // Cache of the active collection's resolved book paths. Without this,
  // every frame called resolveBookPaths() up to 5 times — for "All Books"
  // with hundreds of entries that meant ~250 KB of string-vector copies
  // and sorts per frame, causing visible home-screen lag. Refresh only
  // when the active collection id changes, when membership mutates
  // (picker, rescan, file delete), or on onEnter. Cache key holds the
  // active id at the time of refresh; an empty key means cold.
  std::string shelfPathsCacheKey;
  std::vector<std::string> shelfPathsCache;
  // Series-collapsed view of the active collection — derived from the
  // path list + SeriesIndex. cachedShelfEntries() is the primary cache
  // and shelfPathsCache is recomputed alongside it (one firstPath per
  // entry). Navigation indexes ShelfEntries 1:1 with cells on the
  // shelf row.
  std::vector<ShelfEntry> shelfEntriesCache;
  // Snapshot of the shelf-relevant state at the moment the cached
  // framebuffer (coverBuffer) was last filled. When this matches the
  // live state AND we successfully restored the framebuffer, the
  // shelf's pixels are already on-screen and we can skip the expensive
  // BMP loads for each visible cell — this is the dominant cost of a
  // home re-render. Reset whenever the shelf could possibly look
  // different (active collection, scroll, focused index/header).
  bool shelfSnapshotValid = false;
  std::string shelfSnapshotActiveId;
  int shelfSnapshotScrollOffset = -1;
  int shelfSnapshotFocusedSpine = -2;  // -1 means "no focus", so use a different sentinel.
  bool shelfSnapshotHeaderFocused = false;
  // True when we have NOT yet run the series-enrichment pass for the
  // currently active collection. Set on onEnter and on cycle; cleared
  // by enrichActiveCollectionForSeries(). Keeps the "Detecting
  // series..." popup from re-firing on every render of the same shelf.
  bool seriesEnrichmentNeededForActive = true;
  // The carousel center hint (coverSelectorIndex) painted into the
  // cached framebuffer. When this matches the value we'd compute for
  // the current render AND the buffer was restored, the carousel's 5
  // covers are still correct on screen — set
  // LyraFlowTheme::skipCarouselCoverLoads so the theme bypasses its
  // SD reads. Sentinel = -1 means "no prior snapshot exists".
  int lastRenderedCoverSelectorIdx = -1;
  bool lastRenderedCoverSelectorValid = false;
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
  // cell dimensions for the books that are currently visible on the
  // shelf. Lazy by design: an active collection like "All Books" can
  // have hundreds of entries and eager generation would freeze the UI
  // for minutes. Only the [scrollOffset, scrollOffset+visibleCount)
  // window pays the cost; the next batch generates when the user
  // scrolls into uncovered territory.
  void loadShelfCovers(int cellWidth, int cellHeight, int scrollOffset, int visibleCount);
  // FlexBLE Series — runs the OPF series-only parse for every EPUB in
  // the active collection that hasn't been checked yet (SeriesIndex
  // doesn't know about it). Shows a loading popup since this can take
  // ~50-200 ms per book. Skipped entirely when the active collection
  // has collapseSeries = false. Records empty entries for books that
  // turn out not to be in a series, so we don't re-parse them.
  void enrichActiveCollectionForSeries();
  // Returns the active collection's resolved book-path list, lazily
  // recomputing only when the active id changes since last access (or
  // when invalidateShelfPathsCache() was called). Critical to home
  // smoothness when "All Books" is active — without this cache, every
  // frame paid 5x resolveBookPaths copies. The returned vector is
  // derived from cachedShelfEntries() — one firstPath per entry.
  const std::vector<std::string>& cachedShelfPaths();
  // Same caching contract as above but returns the rich ShelfEntries
  // (series-collapsed). Navigation indexes these 1:1 with shelf cells.
  // For non-series cells, entries[i].memberPaths == {entries[i].firstPath}.
  const std::vector<ShelfEntry>& cachedShelfEntries();
  // Clear the cache so the next cachedShelfPaths() call re-resolves.
  // Call after any operation that may have changed the active
  // collection's contents (picker toggle, file delete, library rescan).
  void invalidateShelfPathsCache() { shelfPathsCacheKey.clear(); }

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
  // Action menu invoked by long-pressing the shelf header (collection
  // name tab). Currently exposes a single "Rescan library" action that
  // re-walks the SD card and refreshes the LibraryIndex. Carved off as a
  // separate menu (vs. the per-book one) so the items always match the
  // collection-level context.
  void showShelfHeaderActionMenu();
  // FlexBLE series — Confirm on a shelf cell. For single-book cells
  // this is just onSelectBook(firstPath). For series cells, opens the
  // most-recently-read member if any is in RECENT_BOOKS; otherwise
  // opens the SeriesMiniPicker.
  void openShelfEntry(const ShelfEntry& entry);
  // Always opens the SeriesMiniPicker for a series cell (used by the
  // long-press path).
  void openSeriesMiniPicker(const ShelfEntry& entry);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
