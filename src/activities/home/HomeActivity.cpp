#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Txt.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../reader/BookReadingStats.h"
#include "../reader/BookStatsActivity.h"
#include "BookmarkStore.h"
#include "BookmarksHomeActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t TXT_CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t TXT_CACHE_VERSION = 2;

enum class HomeMenuAction {
  BrowseFiles,
  RecentBooks,
  OpdsBrowser,
  ReadingStats,
  Bookmarks,
  FileTransfer,
  Settings,
};

struct HomeMenuItem {
  const char* label;
  UIIcon icon;
  HomeMenuAction action;
};

float clampProgressPercent(const float progress) { return std::clamp(progress, 0.0f, 100.0f); }

bool hasAnyBookStats(const BookReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 || stats.isCompleted;
}

bool hasAnyGlobalStats(const GlobalReadingStats& stats) {
  return stats.totalSessions > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 ||
         stats.completedBooks > 0;
}

float loadEpubProgressPercent(const RecentBook& book) {
  Epub epub(book.path, "/.crosspoint");
  if (!epub.load(false, true)) {
    return -1.0f;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", epub.getCachePath() + "/progress.bin", file)) {
    return -1.0f;
  }

  uint8_t data[6];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != 6) {
    return -1.0f;
  }

  const int spineIndex = data[0] | (data[1] << 8);
  const int currentPage = data[2] | (data[3] << 8);
  const int pageCount = data[4] | (data[5] << 8);
  if (pageCount <= 0) {
    return 0.0f;
  }

  const float chapterProgress = static_cast<float>(currentPage + 1) / static_cast<float>(pageCount);
  return clampProgressPercent(epub.calculateProgress(spineIndex, chapterProgress) * 100.0f);
}

float loadXtcProgressPercent(const RecentBook& book) {
  Xtc xtc(book.path, "/.crosspoint");
  if (!xtc.load()) {
    return -1.0f;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", xtc.getCachePath() + "/progress.bin", file)) {
    return -1.0f;
  }

  uint8_t data[4];
  const int bytesRead = file.read(data, sizeof(data));
  file.close();
  if (bytesRead != 4) {
    return -1.0f;
  }

  const uint32_t currentPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  return clampProgressPercent(static_cast<float>(xtc.calculateProgress(currentPage)));
}

float loadTxtProgressPercent(const RecentBook& book) {
  Txt txt(book.path, "/.crosspoint");
  if (!txt.load()) {
    return -1.0f;
  }

  FsFile progressFile;
  if (!Storage.openFileForRead("HOME", txt.getCachePath() + "/progress.bin", progressFile)) {
    return -1.0f;
  }

  uint8_t progressData[4];
  const int progressBytes = progressFile.read(progressData, sizeof(progressData));
  progressFile.close();
  if (progressBytes != 4) {
    return -1.0f;
  }

  const uint32_t currentPage = static_cast<uint32_t>(progressData[0]) | (static_cast<uint32_t>(progressData[1]) << 8);

  FsFile indexFile;
  if (!Storage.openFileForRead("HOME", txt.getCachePath() + "/index.bin", indexFile)) {
    return -1.0f;
  }

  uint32_t magic = 0;
  serialization::readPod(indexFile, magic);
  uint8_t version = 0;
  serialization::readPod(indexFile, version);
  uint32_t fileSize = 0;
  serialization::readPod(indexFile, fileSize);
  int32_t cachedWidth = 0;
  serialization::readPod(indexFile, cachedWidth);
  int32_t cachedLines = 0;
  serialization::readPod(indexFile, cachedLines);
  int32_t fontId = 0;
  serialization::readPod(indexFile, fontId);
  int32_t margin = 0;
  serialization::readPod(indexFile, margin);
  uint8_t alignment = 0;
  serialization::readPod(indexFile, alignment);
  uint32_t totalPages = 0;
  serialization::readPod(indexFile, totalPages);
  indexFile.close();
  (void)cachedWidth;
  (void)cachedLines;
  (void)fontId;
  (void)margin;
  (void)alignment;

  if (magic != TXT_CACHE_MAGIC || version != TXT_CACHE_VERSION || fileSize != txt.getFileSize() || totalPages == 0) {
    return -1.0f;
  }

  return clampProgressPercent((static_cast<float>(currentPage + 1) / static_cast<float>(totalPages)) * 100.0f);
}

float loadRecentBookProgressPercent(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return loadEpubProgressPercent(book);
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return loadXtcProgressPercent(book);
  }
  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    return loadTxtProgressPercent(book);
  }
  return -1.0f;
}

BookReadingStats loadRecentBookStats(const RecentBook& book) {
  if (!FsHelpers::hasEpubExtension(book.path)) {
    return BookReadingStats{};
  }

  const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book.path));
  return BookReadingStats::load(cachePath);
}

std::vector<HomeMenuItem> buildHomeMenuItems(bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks) {
  std::vector<HomeMenuItem> items = {
      {tr(STR_BROWSE_FILES), Folder, HomeMenuAction::BrowseFiles},
      {tr(STR_MENU_RECENT_BOOKS), Recent, HomeMenuAction::RecentBooks},
  };

  if (hasOpdsServers) {
    items.push_back({tr(STR_OPDS_BROWSER), Library, HomeMenuAction::OpdsBrowser});
  }
  if (hasReadingStats) {
    items.push_back({tr(STR_READING_STATS), Chart, HomeMenuAction::ReadingStats});
  }
  if (hasBookmarks) {
    items.push_back({tr(STR_BOOKMARKS), BookmarkIcon, HomeMenuAction::Bookmarks});
  }

  items.push_back({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});
  items.push_back({tr(STR_SETTINGS_TITLE), Settings, HomeMenuAction::Settings});
  return items;
}
}  // namespace

// ---------------------------------------------------------------------------
// Static carousel frame cache — survives HomeActivity re-creation so that
// returning to home (e.g. after settings) doesn't re-read covers from SD.
// Freed explicitly in onSelectBook() before entering the reader.
// ---------------------------------------------------------------------------
namespace {
uint8_t* gCachedFrames[HomeActivity::kCarouselFrameCount] = {};
int gCachedFrameBookIdx[HomeActivity::kCarouselFrameCount] = {-1, -1, -1};
int gCachedFrameCount = 0;
std::string gCacheKey;

int findFrameSlot(int bookIdx) {
  for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
    if (gCachedFrameBookIdx[i] == bookIdx && gCachedFrames[i] != nullptr) return i;
  }
  return -1;
}

void invalidateCarouselCache() {
  for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
    if (gCachedFrames[i]) {
      free(gCachedFrames[i]);
      gCachedFrames[i] = nullptr;
    }
    gCachedFrameBookIdx[i] = -1;
  }
  gCachedFrameCount = 0;
  gCacheKey.clear();
}
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  if (hasReadingStats) {
    count++;
  }
  if (hasBookmarks) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  // Tracks which book indices had a thumbnail generated this pass.
  // Sized to LyraCarousel's max (5) since recentBooks is bounded by the active
  // theme's homeRecentBooksCount and LyraCarousel is the maximum across themes.
  static_assert(LyraCarouselMetrics::values.homeRecentBooksCount == 5,
                "bookUpdated array sized to LyraCarousel max; if this metric "
                "changes or another theme exceeds it, resize the array.");
  bool bookUpdated[LyraCarouselMetrics::values.homeRecentBooksCount] = {};
  Rect popupRect;

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!Storage.exists(book.path.c_str())) {
      progress++;
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      if (isCarouselTheme) {
        // For carousel: generate exact-size thumbnails for center and side slots.
        // Load the source image once even when both sizes are missing.
        const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterCoverW,
                                                                  LyraCarouselTheme::kCenterCoverH);
        const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW,
                                                                LyraCarouselTheme::kSideCoverH);
        const bool centerMissing = !Storage.exists(centerPath.c_str());
        const bool sideMissing = !Storage.exists(sidePath.c_str());

        if (centerMissing || sideMissing) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, "/.crosspoint");
            epub.load(false, true);
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = true;
            if (centerMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
            if (sideMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            } else {
              bookUpdated[progress] = true;
            }
            coverRendered = false;
            requestUpdate();
          } else if (FsHelpers::hasXtcExtension(book.path)) {
            Xtc xtc(book.path, "/.crosspoint");
            if (xtc.load()) {
              if (!showingLoading) {
                showingLoading = true;
                popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
              }
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
              bool success = true;
              if (centerMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
              if (sideMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
              if (!success) {
                RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
                book.coverBmpPath = "";
              } else {
                bookUpdated[progress] = true;
              }
              coverRendered = false;
              requestUpdate();
            }
          }
        }
      } else {
        // Non-carousel: generate height-keyed thumbnail
        std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
        if (!Storage.exists(coverPath.c_str())) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, "/.crosspoint");
            epub.load(false, true);
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = epub.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            } else {
              bookUpdated[progress] = true;  // non-carousel path reuses same tracking
            }
            coverRendered = false;
            requestUpdate();
          } else if (FsHelpers::hasXtcExtension(book.path)) {
            Xtc xtc(book.path, "/.crosspoint");
            if (xtc.load()) {
              if (!showingLoading) {
                showingLoading = true;
                popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
              }
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
              bool success = xtc.generateThumbBmp(coverHeight);
              if (!success) {
                RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
                book.coverBmpPath = "";
              } else {
                bookUpdated[progress] = true;
              }
              coverRendered = false;
              requestUpdate();
            }
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;

  // Re-render only the affected slots rather than rebuilding the entire cache.
  if (isCarouselTheme) {
    bool anyUpdated = false;
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (!bookUpdated[i]) continue;
      anyUpdated = true;
      if (carouselFramesReady) {
        // Only re-render the slot holding this book; books outside the window
        // will be picked up by updateSlidingWindowCache on next navigation.
        const int slot = findFrameSlot(i);
        if (slot >= 0) renderCarouselFrame(i, slot);
      }
    }
    if (anyUpdated) {
      if (!carouselFramesReady) {
        // Cache not yet initialised (shouldn't happen) — fall back to full render.
        preRenderCarouselFrames();
      }
      requestUpdate();
    }
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  // Check if any books have bookmarks (directory scan only, no file parsing)
  hasBookmarks = BookmarkStore::hasAnyBookmarks();

  selectorIndex = 0;
  lastCarouselBookIndex = 0;
  carouselFramesReady = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  globalStats = GlobalReadingStats::load();
  updateHighlightedBookContext();

  // Pre-render carousel frames before the first display update so the fast
  // path is active from render #1. Cache hit = instant. Cache miss = SD reads
  // here, but the E-ink is still refreshing from the previous activity anyway.
  if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL) {
    preRenderCarouselFrames();
  }

  requestUpdate();
}

int HomeActivity::getHighlightedBookIndex() const {
  if (recentBooks.empty()) {
    return -1;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int highlightedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  return std::clamp(highlightedBookIdx, 0, bookCount - 1);
}

void HomeActivity::updateHighlightedBookContext() {
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;

  const int highlightedBookIdx = getHighlightedBookIndex();
  if (highlightedBookIdx >= 0) {
    currentBookStats = loadRecentBookStats(recentBooks[highlightedBookIdx]);
    currentBookProgressPercent = loadRecentBookProgressPercent(recentBooks[highlightedBookIdx]);
  }

  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats);
}

void HomeActivity::onExit() {
  Activity::onExit();

  freeCoverBuffer();
  invalidateCarouselCache();
  freeCarouselFrames();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::freeCarouselFrames() {
  // Instance pointers are aliases into the static cache — do not free here.
  for (int i = 0; i < kCarouselFrameCount; ++i) carouselFrames[i] = nullptr;
  carouselFramesReady = false;
}

void HomeActivity::preRenderCarouselFrames() {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount == 0) return;

  // Build cache key from book paths in order
  std::string newKey;
  newKey.reserve(128);
  for (const auto& b : recentBooks) {
    newKey += b.path;
    newKey += '\0';
  }

  // Cache hit: same books in same order — reuse without any SD reads
  if (newKey == gCacheKey && gCachedFrameCount > 0) {
    for (int i = 0; i < gCachedFrameCount; ++i) carouselFrames[i] = gCachedFrames[i];
    carouselFramesReady = true;
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  // Cache miss: free old cache and re-render
  invalidateCarouselCache();

  if (!renderer.getFrameBuffer()) return;

  const size_t bufferSize = renderer.getBufferSize();
  freeCoverBuffer();  // reclaim 48KB before allocating frames

  const int frameCount = std::min(bookCount, kCarouselFrameCount);
  for (int i = 0; i < frameCount; ++i) {
    gCachedFrames[i] = static_cast<uint8_t*>(malloc(bufferSize));
    if (!gCachedFrames[i]) {
      LOG_ERR("HOME", "preRenderCarouselFrames: malloc failed for frame %d", i);
      invalidateCarouselCache();
      return;
    }
  }

  // Render only the currently-selected cover. Adjacent frames are populated
  // lazily by updateSlidingWindowCache() after the first paint completes,
  // so the user sees their book immediately and scroll latency only appears
  // once they actually navigate.
  const int selectedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  const int initialBookIdx = (selectedBookIdx >= 0 && selectedBookIdx < bookCount) ? selectedBookIdx : 0;
  renderCarouselFrame(initialBookIdx, 0);

  gCachedFrameCount = frameCount;
  gCacheKey = newKey;
  carouselFramesReady = true;
  coverRendered = false;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const int previousHighlightedBookIdx = getHighlightedBookIndex();

  if (isCarousel) {
    const int bookCount = static_cast<int>(recentBooks.size());
    const int menuItemCount =
        static_cast<int>(buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks).size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int menuIdx = inCarouselRow ? 0 : (selectorIndex - bookCount);

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inCarouselRow && bookCount > 0)
        selectorIndex = (selectorIndex + 1) % bookCount;
      else if (!inCarouselRow)
        selectorIndex = bookCount + (menuIdx + 1) % menuItemCount;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inCarouselRow && bookCount > 0)
        selectorIndex = (selectorIndex + bookCount - 1) % bookCount;
      else if (!inCarouselRow)
        selectorIndex = bookCount + (menuIdx + menuItemCount - 1) % menuItemCount;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (inCarouselRow) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = bookCount;
      } else {
        selectorIndex = lastCarouselBookIndex;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow) {
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = bookCount;
      } else {
        selectorIndex = lastCarouselBookIndex;
      }
      requestUpdate();
    }
  } else {
    const int menuCount = getMenuItemCount();
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  if (getHighlightedBookIndex() != previousHighlightedBookIdx) {
    updateHighlightedBookContext();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    const auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);
    const int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    if (menuSelectedIndex < 0 || menuSelectedIndex >= static_cast<int>(menuItems.size())) {
      return;
    }

    switch (menuItems[menuSelectedIndex].action) {
      case HomeMenuAction::BrowseFiles:
        onFileBrowserOpen();
        break;
      case HomeMenuAction::RecentBooks:
        onRecentsOpen();
        break;
      case HomeMenuAction::OpdsBrowser:
        onOpdsBrowserOpen();
        break;
      case HomeMenuAction::ReadingStats:
        onReadingStatsOpen();
        break;
      case HomeMenuAction::Bookmarks:
        onBookmarksOpen();
        break;
      case HomeMenuAction::FileTransfer:
        onFileTransferOpen();
        break;
      case HomeMenuAction::Settings:
        onSettingsOpen();
        break;
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Fast path: pre-rendered frames ready — memcpy + border overlay
  if (carouselFramesReady) {
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bookCount = static_cast<int>(recentBooks.size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int centerIdx = inCarouselRow ? selectorIndex : lastCarouselBookIndex;
    const int slotIdx = findFrameSlot(centerIdx);

    if (frameBuffer && slotIdx >= 0 && carouselFrames[slotIdx]) {
      memcpy(frameBuffer, carouselFrames[slotIdx], renderer.getBufferSize());
      LyraCarouselTheme::setPreRenderIndex(centerIdx);

      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             inCarouselRow);

      const auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);

      GUI.drawButtonMenu(
          renderer,
          Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
               pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                             metrics.buttonHintsHeight)},
          static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
          [&menuItems](int index) { return std::string(menuItems[index].label); },
          [&menuItems](int index) { return menuItems[index].icon; });

      const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

      renderer.displayBuffer();
      // E-ink refresh complete — pre-render the missing adjacent frame while idle.
      updateSlidingWindowCache(centerIdx, bookCount);
      // Mirror the slow-path trigger: generate missing thumbnails on the second
      // render so the E-ink is already showing something before the SD work starts.
      if (!firstRenderDone) {
        firstRenderDone = true;
        requestUpdate();
      } else if (!recentsLoaded && !recentsLoading) {
        recentsLoading = true;
        loadRecentCovers(metrics.homeCoverHeight);
      }
      return;
    }
  }

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent);

  auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);

  const int menuStartY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuEndY = pageHeight - metrics.buttonHintsHeight;
  const int menuHeight = std::max(0, menuEndY - menuStartY);

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), {tr(STR_CONTINUE_READING), Book, HomeMenuAction::RecentBooks});
  }

  GUI.drawButtonMenu(
      renderer, Rect{0, menuStartY, pageWidth, menuHeight}, static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index].label); },
      [&menuItems](int index) { return menuItems[index].icon; });

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const auto labels = isCarouselTheme ? mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                                      : mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::renderCarouselFrame(int bookIdx, int slotIdx) {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || !gCachedFrames[slotIdx]) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int bookCount = static_cast<int>(recentBooks.size());
  bool dummy1 = false, dummy2 = false, dummy3 = false;
  BookReadingStats frameStats;
  const BookReadingStats* frameStatsPtr = nullptr;
  float frameProgressPercent = -1.0f;

  if (bookIdx >= 0 && bookIdx < bookCount) {
    frameStats = loadRecentBookStats(recentBooks[bookIdx]);
    if (hasAnyBookStats(frameStats)) {
      frameStatsPtr = &frameStats;
    }
    frameProgressPercent = loadRecentBookProgressPercent(recentBooks[bookIdx]);
  }

  // selectorIndex = bookCount → drawRecentBookCover uses lastCarouselSelectorIndex (set below)
  // and draws no selection border.
  LyraCarouselTheme::setPreRenderIndex(bookIdx);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  GUI.drawRecentBookCover(
      renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}, recentBooks, bookCount, dummy1,
      dummy2, dummy3, []() { return true; }, frameStatsPtr, frameProgressPercent);

  memcpy(gCachedFrames[slotIdx], frameBuffer, renderer.getBufferSize());
  gCachedFrameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCachedFrames[slotIdx];
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  // No sliding needed when all books already fit in the cache.
  if (bookCount <= kCarouselFrameCount || !carouselFramesReady) return;

  const int prevIdx = (centerIdx + bookCount - 1) % bookCount;
  const int nextIdx = (centerIdx + 1) % bookCount;

  const bool hasPrev = findFrameSlot(prevIdx) >= 0;
  const bool hasNext = findFrameSlot(nextIdx) >= 0;
  if (hasPrev && hasNext) return;  // window already complete

  const int missingIdx = !hasPrev ? prevIdx : nextIdx;

  // Evict the slot whose book is furthest from centerIdx (never evict center itself,
  // nor the adjacent that is already cached).
  int evictSlot = -1;
  int maxDist = -1;
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (!gCachedFrames[i]) continue;
    const int bookInSlot = gCachedFrameBookIdx[i];
    if (bookInSlot == centerIdx) continue;
    if (hasPrev && bookInSlot == prevIdx) continue;
    if (hasNext && bookInSlot == nextIdx) continue;
    const int diff = std::abs(bookInSlot - centerIdx);
    const int dist = std::min(diff, bookCount - diff);
    if (dist > maxDist) {
      maxDist = dist;
      evictSlot = i;
    }
  }

  if (evictSlot >= 0) {
    LOG_DBG("HOME", "carousel: evict slot %d (book %d) -> book %d", evictSlot, gCachedFrameBookIdx[evictSlot],
            missingIdx);
    renderCarouselFrame(missingIdx, evictSlot);
  }
}

void HomeActivity::onSelectBook(const std::string& path) {
  invalidateCarouselCache();
  freeCarouselFrames();
  activityManager.goToReader(path);
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onReadingStatsOpen() {
  const int highlightedBookIdx = getHighlightedBookIndex();
  const std::string bookTitle =
      highlightedBookIdx >= 0 ? recentBooks[highlightedBookIdx].title : std::string(tr(STR_READING_STATS));
  startActivityForResult(
      std::make_unique<BookStatsActivity>(renderer, mappedInput, bookTitle, currentBookStats, globalStats),
      [this](const ActivityResult&) { requestUpdate(); });
}

void HomeActivity::onBookmarksOpen() {
  startActivityForResult(std::make_unique<BookmarksHomeActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}
