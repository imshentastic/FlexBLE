#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../reader/BookReadingStats.h"
#include "../reader/BookStatsActivity.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/util/ConfirmationActivity.h"
#include "BookmarkStore.h"
#include "BookmarksHomeActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FileBrowserActionActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBookProgress.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "CollectionsStore.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/lyra/LyraFlowTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t CAROUSEL_CACHE_MAGIC = 0x43434152;  // "CCAR"
constexpr uint16_t CAROUSEL_CACHE_VERSION = 2;
constexpr char CAROUSEL_CACHE_PATH[] = "/.crosspoint/home_carousel_cache.bin";
constexpr char CAROUSEL_CACHE_TMP_PATH[] = "/.crosspoint/home_carousel_cache.tmp";

enum class HomeMenuAction {
  BrowseFiles,
  ContinueReading,
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

struct CarouselCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t frameCount;
  uint32_t frameBufferSize;
  uint64_t keyHash;
  uint16_t screenWidth;
  uint16_t screenHeight;
  uint16_t centerCoverW;
  uint16_t centerCoverH;
  uint16_t sideCoverW;
  uint16_t sideCoverH;
};

uint64_t fnvHash64(const std::string& s) {
  uint64_t hash = 14695981039346656037ull;
  for (char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool hasAnyBookStats(const BookReadingStats& stats) {
  return stats.sessionCount > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 || stats.isCompleted;
}

bool hasAnyGlobalStats(const GlobalReadingStats& stats) {
  return stats.totalSessions > 0 || stats.totalReadingSeconds > 0 || stats.totalPagesTurned > 0 ||
         stats.completedBooks > 0;
}

void appendHashedFileStateToKey(std::string& key, const std::string& path) {
  FsFile file;
  if (!Storage.openFileForRead("HOME", path, file)) {
    key += "missing";
    key += '\0';
    return;
  }

  uint64_t hash = 14695981039346656037ull;
  size_t totalBytes = 0;
  uint8_t buffer[64];
  while (true) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;
    totalBytes += static_cast<size_t>(bytesRead);
    for (int i = 0; i < bytesRead; ++i) {
      hash ^= buffer[i];
      hash *= 1099511628211ull;
    }
  }
  file.close();

  char digest[48];
  snprintf(digest, sizeof(digest), "%zu:%" PRIu64, totalBytes, static_cast<uint64_t>(hash));
  key += digest;
  key += '\0';
}

std::string getRecentBookCachePath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  if (FsHelpers::hasTxtExtension(book.path) || FsHelpers::hasMarkdownExtension(book.path)) {
    return "/.crosspoint/txt_" + std::to_string(std::hash<std::string>{}(book.path));
  }
  return "";
}

BookReadingStats loadRecentBookStats(const RecentBook& book) {
  if (!FsHelpers::hasEpubExtension(book.path)) {
    return BookReadingStats{};
  }

  const std::string cachePath = getRecentBookCachePath(book);
  return BookReadingStats::load(cachePath);
}

void updateRecentBookCoverPath(const RecentBook& book, const std::string& coverBmpPath) {
  if (!RECENT_BOOKS.updateBook(book.path, book.title, book.author, coverBmpPath)) {
    LOG_ERR("HOME", "failed to update recent book metadata: %s", book.path.c_str());
  }
}

bool hasThumbnailPlaceholder(const std::string& coverBmpPath) {
  return coverBmpPath.find("[WIDTH]") != std::string::npos || coverBmpPath.find("[HEIGHT]") != std::string::npos;
}

std::string getReusableCoverPath(const RecentBook& book) {
  if (FsHelpers::hasEpubExtension(book.path)) {
    return Epub(book.path, "/.crosspoint").getThumbBmpPath();
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    return Xtc(book.path, "/.crosspoint").getThumbBmpPath();
  }
  return book.coverBmpPath;
}

bool ensureReusableCoverPath(RecentBook& book) {
  if (book.coverBmpPath.empty() || hasThumbnailPlaceholder(book.coverBmpPath)) {
    return false;
  }

  const std::string reusablePath = getReusableCoverPath(book);
  if (reusablePath.empty() || reusablePath == book.coverBmpPath) {
    return false;
  }

  book.coverBmpPath = reusablePath;
  updateRecentBookCoverPath(book, reusablePath);
  return true;
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

std::vector<HomeMenuItem> buildMinimalMenuItems(bool hasOpdsServers, bool hasReadingStats, bool hasBookmarks) {
  std::vector<HomeMenuItem> items = {
      {tr(STR_MENU_RECENT_BOOKS), Recent, HomeMenuAction::RecentBooks},
  };

  if (hasOpdsServers) {
    items.push_back({tr(STR_OPDS_BROWSER), Library, HomeMenuAction::OpdsBrowser});
  }
  if (hasBookmarks) {
    items.push_back({tr(STR_BOOKMARKS), BookmarkIcon, HomeMenuAction::Bookmarks});
  }
  if (hasReadingStats) {
    items.push_back({tr(STR_READING_STATS), Chart, HomeMenuAction::ReadingStats});
  }

  items.push_back({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});
  return items;
}

bool isMinimalTheme() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::MINIMAL;
}

bool isAnyFrontButtonPressed(const MappedInputManager& mappedInput) {
  return mappedInput.isFrontButtonPressed(HalGPIO::BTN_BACK) ||
         mappedInput.isFrontButtonPressed(HalGPIO::BTN_CONFIRM) ||
         mappedInput.isFrontButtonPressed(HalGPIO::BTN_LEFT) || mappedInput.isFrontButtonPressed(HalGPIO::BTN_RIGHT);
}

int minimalHomeNavCount(const bool hasCurrentBook) { return hasCurrentBook ? 4 : 3; }

int minimalHomeCoverWidth(int coverHeight) { return static_cast<int>((static_cast<int64_t>(coverHeight) * 3 + 2) / 5); }

std::string minimalHomeCoverPath(const RecentBook& book, int coverHeight) {
  return UITheme::getCoverThumbPath(book.coverBmpPath, minimalHomeCoverWidth(coverHeight), coverHeight);
}

void appendCarouselCoverStateToKey(std::string& key, const RecentBook& book) {
  key += book.path;
  key += '\0';
  key += book.coverBmpPath;
  key += '\0';

  if (book.coverBmpPath.empty()) {
    key += "0:0";
    key += '\0';
    return;
  }

  const std::string centerPath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH);
  const std::string sidePath =
      UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH);
  key += Storage.exists(centerPath.c_str()) ? '1' : '0';
  key += ':';
  key += Storage.exists(sidePath.c_str()) ? '1' : '0';
  key += '\0';

  const std::string cachePath = getRecentBookCachePath(book);
  if (!cachePath.empty()) {
    appendHashedFileStateToKey(key, cachePath + "/progress.bin");
    if (FsHelpers::hasEpubExtension(book.path)) {
      appendHashedFileStateToKey(key, cachePath + "/stats.bin");
    }
  } else {
    key += "no-cache-path";
    key += '\0';
  }
}

void buildCarouselCacheKey(const std::vector<RecentBook>& recentBooks, std::string& key, uint64_t& keyHash) {
  key.clear();
  key.reserve(512);
  for (const auto& book : recentBooks) {
    appendCarouselCoverStateToKey(key, book);
  }
  appendHashedFileStateToKey(key, "/.crosspoint/global_stats.bin");
  keyHash = fnvHash64(key);
}

bool isCarouselCacheHeaderValid(const CarouselCacheHeader& header, uint64_t cacheKeyHash, int bookCount,
                                const GfxRenderer& renderer) {
  return header.magic == CAROUSEL_CACHE_MAGIC && header.version == CAROUSEL_CACHE_VERSION &&
         header.keyHash == cacheKeyHash && header.frameCount == bookCount &&
         header.frameBufferSize == renderer.getBufferSize() && header.screenWidth == renderer.getScreenWidth() &&
         header.screenHeight == renderer.getScreenHeight() && header.centerCoverW == LyraCarouselTheme::kCenterCoverW &&
         header.centerCoverH == LyraCarouselTheme::kCenterCoverH &&
         header.sideCoverW == LyraCarouselTheme::kSideCoverW && header.sideCoverH == LyraCarouselTheme::kSideCoverH;
}

bool readCarouselCacheHeader(FsFile& file, CarouselCacheHeader& header) {
  CarouselCacheHeader readHeader{};
  if (!serialization::tryReadPod(file, readHeader)) {
    return false;
  }
  header = readHeader;
  return true;
}

bool hasValidCarouselDiskCache(const std::vector<RecentBook>& recentBooks, const GfxRenderer& renderer) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount <= 0) return false;

  std::string cacheKey;
  uint64_t cacheKeyHash = 0;
  buildCarouselCacheKey(recentBooks, cacheKey, cacheKeyHash);

  FsFile cacheFile;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) {
    return false;
  }

  CarouselCacheHeader header{};
  const bool readOk = readCarouselCacheHeader(cacheFile, header);
  cacheFile.close();
  return readOk && isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer);
}

int getHomeMenuSelectionOffset(const std::vector<RecentBook>& recentBooks) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
}

// Small centered toast — mirrors the helper in FileBrowserActivity.cpp.
// Local to this TU because the file-browser version is also file-local.
void drawHomeToast(const GfxRenderer& renderer, const char* msg) {
  constexpr int toastPadX = 20;
  constexpr int toastPadY = 12;
  const int msgW = renderer.getTextWidth(UI_10_FONT_ID, msg);
  const int msgH = renderer.getLineHeight(UI_10_FONT_ID);
  const int toastW = msgW + toastPadX * 2;
  const int toastH = msgH + toastPadY * 2;
  const int toastX = (renderer.getScreenWidth() - toastW) / 2;
  const int toastY = (renderer.getScreenHeight() - toastH) / 2;
  renderer.fillRect(toastX, toastY, toastW, toastH, true);
  renderer.drawText(UI_10_FONT_ID, toastX + toastPadX, toastY + toastPadY, msg, false);
  renderer.displayBuffer();
}
}  // namespace

// ---------------------------------------------------------------------------
// Static carousel frame cache — survives HomeActivity re-creation so that
// returning to home (e.g. after settings) doesn't re-read covers from SD.
// Freed explicitly in onSelectBook() before entering the reader.
// ---------------------------------------------------------------------------
namespace {
class CarouselCache {
 public:
  uint8_t* frames[HomeActivity::kCarouselFrameCount] = {};
  int frameBookIdx[HomeActivity::kCarouselFrameCount] = {-1};
  int frameCount = 0;
  int lastCenterIdx = -1;
  std::string key;
  uint64_t keyHash = 0;

  int findFrameSlot(int bookIdx) const {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frameBookIdx[i] == bookIdx && frames[i] != nullptr) return i;
    }
    return -1;
  }

  void invalidate() {
    for (int i = 0; i < HomeActivity::kCarouselFrameCount; ++i) {
      if (frames[i]) {
        free(frames[i]);
        frames[i] = nullptr;
      }
      frameBookIdx[i] = -1;
    }
    frameCount = 0;
    lastCenterIdx = -1;
    key.clear();
    keyHash = 0;
  }
};

CarouselCache gCarouselCache;
}  // namespace

static_assert(HomeActivity::kMaxCachedBooks >= LyraCarouselMetrics::values.homeRecentBooksCount,
              "kMaxCachedBooks must cover all carousel slots");

int HomeActivity::getMenuItemCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    count += recentBooks.size();
  } else if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    count++;  // Continue Reading menu item
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

  for (const RecentBook& storedBook : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    RecentBook book = storedBook;
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    ensureReusableCoverPath(book);
    recentBooks.push_back(book);
  }
}

void HomeActivity::loadAllBookStats() {
  const auto start = millis();
  const int count = std::min(static_cast<int>(recentBooks.size()), kMaxCachedBooks);
  for (int i = 0; i < count; ++i) {
    cachedBookStats[i] = loadRecentBookStats(recentBooks[i]);
    cachedBookProgress[i] = RecentBookProgress::loadPercent(recentBooks[i]);
  }
  bookStatsCached = true;
  LOG_DBG("HOME", "carousel: cached stats/progress for %d book(s) in %lums", count, millis() - start);
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const bool isMinimal = isMinimalTheme();
  const size_t recentBookCount = recentBooks.size();
  // Tracks which book indices had a thumbnail generated this pass.
  std::vector<char> bookUpdated(recentBookCount, false);
  const int progressIncrement = 90 / static_cast<int>(std::max<size_t>(1, recentBookCount));

  int progress = 0;
  for (size_t bookIdx = 0; bookIdx < recentBooks.size(); ++bookIdx) {
    RecentBook& book = recentBooks[bookIdx];
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
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            if (!epub.load(false, true)) {
              LOG_ERR("HOME", "carousel: failed to load EPUB cache for thumb generation: %s", book.path.c_str());
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
              coverRendered = false;
              requestUpdate();
              progress++;
              continue;
            }
            bool success = true;
            if (centerMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
            if (sideMissing)
              success =
                  epub.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
            if (!success) {
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
            } else {
              bookUpdated[bookIdx] = true;
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
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
              bool success = true;
              if (centerMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH) && success;
              if (sideMissing)
                success =
                    xtc.generateThumbBmp(LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH) && success;
              if (!success) {
                updateRecentBookCoverPath(book, "");
                book.coverBmpPath = "";
              } else {
                bookUpdated[bookIdx] = true;
              }
              coverRendered = false;
              requestUpdate();
            }
          }
        }
      } else {
        // Non-carousel: generate height-keyed thumbnail
        const bool useMinimalThumb = isMinimal && FsHelpers::hasEpubExtension(book.path);
        const std::string coverPath = useMinimalThumb ? minimalHomeCoverPath(book, coverHeight)
                                                      : UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
        if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
          if (FsHelpers::hasEpubExtension(book.path)) {
            Epub epub(book.path, "/.crosspoint");
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            if (!epub.load(false, true)) {
              LOG_ERR("HOME", "failed to load EPUB cache for thumb generation: %s", book.path.c_str());
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
              coverRendered = false;
              requestUpdate();
              progress++;
              continue;
            }
            const bool success = useMinimalThumb
                                     ? epub.generateThumbBmp(minimalHomeCoverWidth(coverHeight), coverHeight)
                                     : epub.generateThumbBmp(0, coverHeight);
            if (!success) {
              updateRecentBookCoverPath(book, "");
              book.coverBmpPath = "";
            } else {
              bookUpdated[bookIdx] = true;  // non-carousel path reuses same tracking
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
              GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
              bool success = xtc.generateThumbBmp(coverHeight);
              if (!success) {
                updateRecentBookCoverPath(book, "");
                book.coverBmpPath = "";
              } else {
                bookUpdated[bookIdx] = true;
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
      if (static_cast<size_t>(i) >= bookUpdated.size() || !bookUpdated[i]) continue;
      anyUpdated = true;
      if (carouselFramesReady) {
        // Only re-render the slot holding this book; books outside the window
        // will be picked up by updateSlidingWindowCache on next navigation.
        const int slot = gCarouselCache.findFrameSlot(i);
        if (slot >= 0) renderCarouselFrame(i, slot);
      }
    }
    if (anyUpdated) {
      if (!carouselFramesReady) {
        // Cover assets changed before the carousel cache was initialised, so
        // any existing SD snapshot may still contain placeholder frames.
        // Force a rebuild from the fresh thumbs instead of reusing stale
        // `home_carousel_cache.bin` content keyed only by book order/layout.
        if (Storage.exists(CAROUSEL_CACHE_PATH)) {
          Storage.remove(CAROUSEL_CACHE_PATH);
        }
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
          Storage.remove(CAROUSEL_CACHE_TMP_PATH);
        }
        preRenderCarouselFrames();
      } else {
        // The live carousel frames are already updated above. Keep Home
        // responsive by invalidating any stale SD snapshot instead of
        // rewriting all 5 frames synchronously on this return-to-Home path.
        if (Storage.exists(CAROUSEL_CACHE_PATH)) {
          Storage.remove(CAROUSEL_CACHE_PATH);
        }
        if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
          Storage.remove(CAROUSEL_CACHE_TMP_PATH);
        }
      }
      requestUpdate();
    }
  }
}

void HomeActivity::loadShelfCovers(int cellWidth, int cellHeight) {
  // No-op for themes other than LYRA_FLOW (only theme that has a shelf).
  if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) != CrossPointSettings::UI_THEME::LYRA_FLOW) {
    shelfCoversLoaded = true;
    return;
  }
  const Collection* activeCollection = CollectionsStore::getInstance().getActiveCollection();
  if (activeCollection == nullptr || activeCollection->bookPaths.empty()) {
    shelfCoversLoaded = true;
    return;
  }
  if (cellWidth <= 0 || cellHeight <= 0) {
    shelfCoversLoaded = true;
    return;
  }

  bool showingLoading = false;
  Rect popupRect;
  const int total = static_cast<int>(activeCollection->bookPaths.size());
  const int progressIncrement = 90 / std::max(1, total);
  int processed = 0;

  for (const auto& bookPath : activeCollection->bookPaths) {
    if (!Storage.exists(bookPath.c_str())) {
      processed++;
      continue;
    }
    // Build the dimension-specific resolved thumb path. If it already exists
    // on SD, this book is done — skip the expensive EPUB/XTC load.
    std::string templatePath;
    if (FsHelpers::hasEpubExtension(bookPath)) {
      templatePath = Epub(bookPath, "/.crosspoint").getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(bookPath)) {
      templatePath = Xtc(bookPath, "/.crosspoint").getThumbBmpPath();
    } else {
      processed++;
      continue;
    }
    const std::string resolved = UITheme::getCoverThumbPath(templatePath, cellWidth, cellHeight);
    if (!resolved.empty() && Storage.exists(resolved.c_str())) {
      processed++;
      continue;
    }

    // Need to generate. Show a loading popup if this is the first book in
    // this pass that's missing — matches the loadRecentCovers UX.
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + processed * progressIncrement);

    if (FsHelpers::hasEpubExtension(bookPath)) {
      Epub epub(bookPath, "/.crosspoint");
      // buildIfMissing=true so books the user toggled into Favorites without
      // ever having opened them still get their thumbnail extracted (same
      // reasoning as RecentBooksGridActivity::loadPageCovers).
      if (epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true)) {
        if (!epub.generateThumbBmp(cellWidth, cellHeight)) {
          LOG_ERR("HOME", "shelf: failed to generate thumb for %s", bookPath.c_str());
        }
      } else {
        LOG_ERR("HOME", "shelf: failed to load EPUB for thumb gen: %s", bookPath.c_str());
      }
    } else if (FsHelpers::hasXtcExtension(bookPath)) {
      Xtc xtc(bookPath, "/.crosspoint");
      if (xtc.load()) {
        if (!xtc.generateThumbBmp(cellWidth, cellHeight)) {
          LOG_ERR("HOME", "shelf: failed to generate xtc thumb for %s", bookPath.c_str());
        }
      }
    }
    processed++;
  }

  shelfCoversLoaded = true;
  if (showingLoading) {
    requestUpdate();
  }
}

std::string HomeActivity::getFocusedBookPath() const {
  // Header focus is a separate row that isn't a "book" — long-press there
  // should NOT open an action menu (there's no book to act on).
  if (shelfHeaderFocused) {
    return {};
  }
  // Carousel range: selectorIndex < recentBooks.size().
  if (selectorIndex < recentBooks.size()) {
    return recentBooks[selectorIndex].path;
  }
  // Shelf range: only meaningful on the Flow theme. Indices sit between
  // the carousel and the menu icon bar.
  if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_FLOW) {
    const Collection* active = CollectionsStore::getInstance().getActiveCollection();
    if (active != nullptr) {
      const int shelfStart = static_cast<int>(recentBooks.size());
      const int shelfCount = static_cast<int>(active->bookPaths.size());
      if (static_cast<int>(selectorIndex) >= shelfStart &&
          static_cast<int>(selectorIndex) < shelfStart + shelfCount) {
        return active->bookPaths[selectorIndex - shelfStart];
      }
    }
  }
  return {};  // empty => focus is somewhere that doesn't represent a book (e.g. menu row).
}

void HomeActivity::showHomeBookActionMenu(const std::string& bookPath) {
  // Build a menu tailored to the home screen. Compared to the file
  // browser's version we drop PinFavorite (sleep-image-only, irrelevant
  // here) and add RemoveFromRecentBooks when the book is currently in the
  // recents list.
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(6);

  // Delete (file-level) is destructive — keep it as the first item to
  // match the file browser's ordering so user muscle memory transfers.
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  const bool isEpub = FsHelpers::hasEpubExtension(bookPath);
  const bool isXtc = FsHelpers::hasXtcExtension(bookPath);
  if (isEpub || isXtc) {
    items.push_back({FileBrowserAction::DeleteCache, StrId::STR_DELETE_CACHE});
  }
  if (isEpub) {
    const Epub epub(bookPath, "/.crosspoint");
    const bool completed = BookReadingStats::load(epub.getCachePath()).isCompleted;
    items.push_back({FileBrowserAction::ToggleCompleted,
                     completed ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  }

  const bool isBookFile = isEpub || isXtc || FsHelpers::hasTxtExtension(bookPath) ||
                          FsHelpers::hasMarkdownExtension(bookPath);
  if (isBookFile) {
    const bool inFavorites =
        CollectionsStore::getInstance().isBookInCollection(CollectionsStore::FAVORITES_ID, bookPath);
    items.push_back({inFavorites ? FileBrowserAction::RemoveFromFavorites : FileBrowserAction::AddToFavorites,
                     inFavorites ? StrId::STR_REMOVE_FROM_FAVORITES : StrId::STR_ADD_TO_FAVORITES});
  }

  // Only show "Remove from Recent Books" if the book is actually in the
  // recents — otherwise the option is meaningless (e.g. a Favorites-only
  // book that was never opened).
  const auto& recents = RECENT_BOOKS.getBooks();
  const bool inRecents =
      std::find_if(recents.begin(), recents.end(), [&](const RecentBook& r) { return r.path == bookPath; }) !=
      recents.end();
  if (inRecents) {
    items.push_back({FileBrowserAction::RemoveFromRecentBooks, StrId::STR_REMOVE_FROM_RECENT_BOOKS});
  }

  // Title for the picker = the book's filename (matches file browser UX).
  const size_t lastSlash = bookPath.find_last_of('/');
  const std::string title = (lastSlash != std::string::npos) ? bookPath.substr(lastSlash + 1) : bookPath;

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, title, std::move(items),
                                                  /*ignoreInitialConfirmRelease=*/true),
      [this, bookPath](const ActivityResult& result) {
        longPressConfirmHandled = false;
        if (result.isCancelled) {
          return;
        }
        const auto action = static_cast<FileBrowserAction>(std::get<FileBrowserActionResult>(result.data).action);
        switch (action) {
          case FileBrowserAction::Delete: {
            // Confirmation prompt mirrors FileBrowser. On confirm we wipe
            // the cache, drop the book from recents + every collection,
            // and finally remove the file itself. Failures are logged but
            // we still refresh the home view so stale entries don't
            // linger.
            const size_t ls = bookPath.find_last_of('/');
            const std::string entry = (ls != std::string::npos) ? bookPath.substr(ls + 1) : bookPath;
            const std::string heading = tr(STR_DELETE) + std::string("? ");
            startActivityForResult(
                std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry),
                [this, bookPath](const ActivityResult& confirm) {
                  if (confirm.isCancelled) {
                    return;
                  }
                  if (FsHelpers::hasEpubExtension(bookPath)) {
                    Epub(bookPath, "/.crosspoint").clearCache();
                    BookmarkStore::deleteForFilePath(bookPath, "epub");
                  } else if (FsHelpers::hasXtcExtension(bookPath)) {
                    Xtc(bookPath, "/.crosspoint").clearCache();
                    BookmarkStore::deleteForFilePath(bookPath, "xtc");
                  } else if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
                    BookmarkStore::deleteForFilePath(bookPath, "txt");
                  }
                  RECENT_BOOKS.removeBook(bookPath);
                  CollectionsStore::getInstance().removeBookFromAllCollections(bookPath);
                  if (!Storage.remove(bookPath.c_str())) {
                    LOG_ERR("HOME", "Failed to delete file: %s", bookPath.c_str());
                  }
                  // Recents shrank — reload from the store so the
                  // carousel/shelf indices stay valid.
                  loadRecentBooks(UITheme::getInstance().getMetrics().homeRecentBooksCount);
                  if (selectorIndex >= recentBooks.size() + 1) {
                    selectorIndex = recentBooks.empty() ? 0 : static_cast<int>(recentBooks.size()) - 1;
                  }
                  shelfCoversLoaded = false;
                  requestUpdate(true);
                });
            return;
          }
          case FileBrowserAction::DeleteCache: {
            bool ok = false;
            if (FsHelpers::hasEpubExtension(bookPath)) {
              ok = Epub(bookPath, "/.crosspoint").clearCache();
            } else if (FsHelpers::hasXtcExtension(bookPath)) {
              ok = Xtc(bookPath, "/.crosspoint").clearCache();
            }
            if (!ok) {
              LOG_ERR("HOME", "Failed to clear book cache for: %s", bookPath.c_str());
            } else {
              drawHomeToast(renderer, tr(STR_BOOK_CACHE_DELETED));
              delay(800);
            }
            shelfCoversLoaded = false;  // thumbs in cache may have been wiped.
            requestUpdate();
            return;
          }
          case FileBrowserAction::ToggleCompleted: {
            // Simplified vs. FileBrowser: just flip the flag and update
            // GlobalReadingStats. We deliberately skip the
            // "auto-move-finished-to-/Read folder" dance because the home
            // screen doesn't have the surrounding redraw machinery for
            // the moved-file alert path. Users who want that should
            // mark from the file browser.
            Epub epub(bookPath, "/.crosspoint");
            epub.setupCacheDir();
            BookReadingStats stats = BookReadingStats::load(epub.getCachePath());
            const bool nowCompleted = !stats.isCompleted;
            stats.isCompleted = nowCompleted;
            GlobalReadingStats gs = GlobalReadingStats::load();
            if (nowCompleted) {
              gs.completedBooks++;
            } else if (gs.completedBooks > 0) {
              gs.completedBooks--;
            }
            stats.save(epub.getCachePath());
            gs.save();
            drawHomeToast(renderer, nowCompleted ? tr(STR_BOOK_FINISHED) : tr(STR_BOOK_UNFINISHED));
            delay(800);
            requestUpdate();
            return;
          }
          case FileBrowserAction::AddToFavorites:
          case FileBrowserAction::RemoveFromFavorites: {
            const bool nowIn = CollectionsStore::getInstance().toggleBookInCollection(
                CollectionsStore::FAVORITES_ID, bookPath);
            drawHomeToast(renderer, nowIn ? tr(STR_ADDED_TO_FAVORITES) : tr(STR_REMOVED_FROM_FAVORITES));
            delay(800);
            // Adding a brand-new book to Favorites means we need to
            // generate its shelf thumb on next render.
            shelfCoversLoaded = false;
            requestUpdate();
            return;
          }
          case FileBrowserAction::RemoveFromRecentBooks: {
            if (RECENT_BOOKS.removeBook(bookPath)) {
              drawHomeToast(renderer, tr(STR_REMOVED_FROM_RECENT_BOOKS));
              delay(800);
              loadRecentBooks(UITheme::getInstance().getMetrics().homeRecentBooksCount);
              if (selectorIndex >= recentBooks.size() + 1) {
                selectorIndex = recentBooks.empty() ? 0 : static_cast<int>(recentBooks.size()) - 1;
              }
            }
            requestUpdate();
            return;
          }
          case FileBrowserAction::PinFavorite:
          case FileBrowserAction::UnpinFavorite:
            // Not exposed in the home menu — sleep-image-only actions.
            return;
        }
      });
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();
  const bool isCarouselTheme =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  // Check if any books have bookmarks (directory scan only, no file parsing)
  hasBookmarks = BookmarkStore::hasAnyBookmarks();

  selectorIndex = 0;
  lastCarouselBookIndex = 0;
  minimalMenuOpen = false;
  minimalSuppressInitialFrontRelease = isMinimalTheme();
  minimalMenuIndex = 0;
  minimalHomeNavIndex = -1;
  carouselFramesReady = false;
  carouselWarmupPending = isCarouselTheme;
  // Force a re-check of shelf thumbnails on every onEnter so books that
  // were just toggled into a collection (e.g. via the file browser long-
  // press) get their cover generated on the next return to Home.
  shelfCoversLoaded = false;
  shelfHeaderFocused = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  if (!APP_STATE.openEpubPath.empty()) {
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (recentBooks[i].path == APP_STATE.openEpubPath) {
        selectorIndex = i;
        lastCarouselBookIndex = i;
        break;
      }
    }
  }

  globalStats = GlobalReadingStats::load();
  if (isCarouselTheme) {
    loadAllBookStats();
  }
  updateHighlightedBookContext();

  if (isCarouselTheme && hasValidCarouselDiskCache(recentBooks, renderer)) {
    preRenderCarouselFrames(false);
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
  const auto start = millis();
  currentBookStats = BookReadingStats{};
  currentBookProgressPercent = -1.0f;

  const int idx = getHighlightedBookIndex();
  const bool useCachedStats = idx >= 0 && bookStatsCached && idx < kMaxCachedBooks;
  if (idx >= 0) {
    if (useCachedStats) {
      currentBookStats = cachedBookStats[idx];
      currentBookProgressPercent = cachedBookProgress[idx];
    } else {
      currentBookStats = loadRecentBookStats(recentBooks[idx]);
      currentBookProgressPercent = RecentBookProgress::loadPercent(recentBooks[idx]);
    }
  }

  hasReadingStats = hasAnyBookStats(currentBookStats) || hasAnyGlobalStats(globalStats);
  LOG_DBG("HOME", "carousel: updateHighlightedBookContext idx=%d cached=%s took %lums", idx,
          useCachedStats ? "yes" : "no", millis() - start);
}

void HomeActivity::onExit() {
  Activity::onExit();

  freeCoverBuffer();
  gCarouselCache.invalidate();
  freeCarouselFrames();
  carouselWarmupPending = false;
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

bool HomeActivity::allocateCarouselFrameSlots(int targetFrameCount) {
  const size_t bufferSize = renderer.getBufferSize();
  int frameCount = 0;
  for (int attemptFrameCount = targetFrameCount; attemptFrameCount >= 1; --attemptFrameCount) {
    bool allocFailed = false;
    for (int i = 0; i < attemptFrameCount; ++i) {
      gCarouselCache.frames[i] = static_cast<uint8_t*>(malloc(bufferSize));
      if (!gCarouselCache.frames[i]) {
        LOG_ERR("HOME", "preRenderCarouselFrames: malloc failed for frame %d while allocating %d frame(s)", i,
                attemptFrameCount);
        allocFailed = true;
        break;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }

    if (!allocFailed) {
      frameCount = attemptFrameCount;
      break;
    }

    for (int i = 0; i < attemptFrameCount; ++i) {
      if (gCarouselCache.frames[i]) {
        free(gCarouselCache.frames[i]);
        gCarouselCache.frames[i] = nullptr;
      }
      gCarouselCache.frameBookIdx[i] = -1;
    }
  }

  if (frameCount == 0) {
    gCarouselCache.invalidate();
    return false;
  }

  gCarouselCache.frameCount = frameCount;
  LOG_INF("HOME", "carousel: frame cache capacity %d/%d", frameCount, targetFrameCount);
  return true;
}

void HomeActivity::renderCarouselFrameToCurrentBuffer(int bookIdx, BookReadingStats* outStats,
                                                      float* outProgressPercent, bool* outUsedCachedStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(recentBooks.size());
  bool dummy1 = false, dummy2 = false, dummy3 = false;
  BookReadingStats frameStats;
  const BookReadingStats* frameStatsPtr = nullptr;
  float frameProgressPercent = -1.0f;
  bool usedCachedStats = false;

  if (bookIdx >= 0 && bookIdx < bookCount) {
    if (bookStatsCached && bookIdx < kMaxCachedBooks) {
      usedCachedStats = true;
      frameStats = cachedBookStats[bookIdx];
      frameProgressPercent = cachedBookProgress[bookIdx];
    } else {
      frameStats = loadRecentBookStats(recentBooks[bookIdx]);
      frameProgressPercent = RecentBookProgress::loadPercent(recentBooks[bookIdx]);
    }
    if (hasAnyBookStats(frameStats)) frameStatsPtr = &frameStats;
  }

  LyraCarouselTheme::setPreRenderIndex(bookIdx);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  GUI.drawRecentBookCover(
      renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}, recentBooks, bookCount, dummy1,
      dummy2, dummy3, []() { return true; }, frameStatsPtr, frameProgressPercent);

  const bool frameHasReadingStats = hasAnyBookStats(frameStats) || hasAnyGlobalStats(globalStats);
  const auto menuItems = buildHomeMenuItems(hasOpdsServers, frameHasReadingStats, hasBookmarks);
  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing * 2 +
                         metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()), -1, [&menuItems](int index) { return std::string(menuItems[index].label); },
      [&menuItems](int index) { return menuItems[index].icon; });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (outStats) *outStats = frameStats;
  if (outProgressPercent) *outProgressPercent = frameProgressPercent;
  if (outUsedCachedStats) *outUsedCachedStats = usedCachedStats;
}

bool HomeActivity::buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                                          bool showProgressPopup) {
  (void)cacheKey;
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || bookCount <= 0) return false;

  Storage.mkdir("/.crosspoint");
  if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
  }

  FsFile file;
  if (!Storage.openFileForWrite("HOME", CAROUSEL_CACHE_TMP_PATH, file)) {
    return false;
  }

  const CarouselCacheHeader header = {
      CAROUSEL_CACHE_MAGIC,
      CAROUSEL_CACHE_VERSION,
      static_cast<uint16_t>(bookCount),
      static_cast<uint32_t>(renderer.getBufferSize()),
      cacheKeyHash,
      static_cast<uint16_t>(renderer.getScreenWidth()),
      static_cast<uint16_t>(renderer.getScreenHeight()),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterCoverW),
      static_cast<uint16_t>(LyraCarouselTheme::kCenterCoverH),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverW),
      static_cast<uint16_t>(LyraCarouselTheme::kSideCoverH),
  };
  if (!serialization::tryWritePod(file, header)) {
    file.close();
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to write SD cache header");
    return false;
  }

  const auto start = millis();
  Rect popupRect{};
  uint8_t* progressFrameBuffer = nullptr;
  const size_t bufferSize = renderer.getBufferSize();
  if (showProgressPopup) {
    progressFrameBuffer = static_cast<uint8_t*>(malloc(bufferSize));
    if (!progressFrameBuffer) {
      LOG_ERR("HOME", "carousel: failed to allocate progress overlay buffer");
      showProgressPopup = false;
    } else {
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
      memcpy(progressFrameBuffer, frameBuffer, bufferSize);
    }
  }
  bool writeFailed = false;
  for (int i = 0; i < bookCount; ++i) {
    const int cachedSlot = gCarouselCache.findFrameSlot(i);
    if (cachedSlot >= 0 && carouselFrames[cachedSlot]) {
      memcpy(frameBuffer, carouselFrames[cachedSlot], renderer.getBufferSize());
    } else {
      renderCarouselFrameToCurrentBuffer(i, nullptr, nullptr, nullptr);
    }
    if (file.write(frameBuffer, renderer.getBufferSize()) != renderer.getBufferSize()) {
      writeFailed = true;
      break;
    }
    if (showProgressPopup) {
      memcpy(frameBuffer, progressFrameBuffer, bufferSize);
      GUI.fillPopupProgress(renderer, popupRect, ((i + 1) * 100) / bookCount);
    }
  }

  const bool syncOk = file.sync();
  file.close();

  if (writeFailed || !syncOk) {
    free(progressFrameBuffer);
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to write SD cache snapshot");
    return false;
  }

  if (Storage.exists(CAROUSEL_CACHE_PATH)) {
    Storage.remove(CAROUSEL_CACHE_PATH);
  }
  if (!Storage.rename(CAROUSEL_CACHE_TMP_PATH, CAROUSEL_CACHE_PATH)) {
    free(progressFrameBuffer);
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
    LOG_ERR("HOME", "carousel: failed to promote SD cache snapshot");
    return false;
  }

  free(progressFrameBuffer);
  LOG_DBG("HOME", "carousel: built SD cache for %d book(s) in %lums", bookCount, millis() - start);
  return true;
}

bool HomeActivity::loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx) {
  if (slotIdx < 0 || slotIdx >= kCarouselFrameCount || !gCarouselCache.frames[slotIdx] || bookIdx < 0 ||
      bookIdx >= bookCount) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, file)) {
    return false;
  }

  CarouselCacheHeader header{};
  if (!readCarouselCacheHeader(file, header) ||
      !isCarouselCacheHeaderValid(header, cacheKeyHash, bookCount, renderer)) {
    file.close();
    return false;
  }

  const size_t frameOffset = sizeof(CarouselCacheHeader) + static_cast<size_t>(bookIdx) * renderer.getBufferSize();
  if (!file.seek(frameOffset)) {
    file.close();
    return false;
  }
  const size_t expectedBytes = renderer.getBufferSize();
  size_t totalBytesRead = 0;
  while (totalBytesRead < expectedBytes) {
    const int bytesRead = file.read(gCarouselCache.frames[slotIdx] + totalBytesRead, expectedBytes - totalBytesRead);
    if (bytesRead <= 0) {
      break;
    }
    totalBytesRead += static_cast<size_t>(bytesRead);
  }
  file.close();
  if (totalBytesRead != expectedBytes) {
    LOG_ERR("HOME", "carousel: short read for slot %d (%zu/%zu bytes)", slotIdx, totalBytesRead, expectedBytes);
    return false;
  }

  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  return true;
}

int HomeActivity::chooseCarouselEvictionSlot(int centerIdx, int bookCount, std::optional<int> protectedBookIdx) const {
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (gCarouselCache.frames[i] && gCarouselCache.frameBookIdx[i] < 0) {
      return i;
    }
  }

  int evictSlot = -1;
  int maxDist = -1;
  for (int i = 0; i < kCarouselFrameCount; ++i) {
    if (!gCarouselCache.frames[i]) continue;
    const int cachedBookIdx = gCarouselCache.frameBookIdx[i];
    if (protectedBookIdx.has_value() && cachedBookIdx == protectedBookIdx.value()) continue;
    const int diff = std::abs(cachedBookIdx - centerIdx);
    const int dist = std::min(diff, bookCount - diff);
    if (dist > maxDist) {
      maxDist = dist;
      evictSlot = i;
    }
  }
  return evictSlot;
}

bool HomeActivity::preRenderCarouselFrames(bool showProgressPopup) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount == 0) return false;
  bool showedProgressPopup = false;

  // Build cache key from book paths plus thumb-asset availability so we don't
  // reuse a stale snapshot built before carousel-sized thumbs existed.
  std::string newKey;
  uint64_t newKeyHash = 0;
  buildCarouselCacheKey(recentBooks, newKey, newKeyHash);

  // Cache hit: same books in same order — reuse without any SD reads
  if (newKey == gCarouselCache.key && gCarouselCache.frameCount > 0) {
    for (int i = 0; i < gCarouselCache.frameCount; ++i) carouselFrames[i] = gCarouselCache.frames[i];
    carouselFramesReady = true;
    coverRendered = false;
    coverBufferStored = false;
    return false;
  }

  // Cache miss: free old cache and re-render
  if (!renderer.getFrameBuffer()) return false;
  freeCoverBuffer();  // reclaim 48KB before allocating frames
  gCarouselCache.invalidate();

  const int targetFrameCount = std::min(bookCount, kCarouselFrameCount);
  bool diskCacheValid = false;
  FsFile cacheFile;
  if (Storage.openFileForRead("HOME", CAROUSEL_CACHE_PATH, cacheFile)) {
    CarouselCacheHeader header{};
    const bool readOk = readCarouselCacheHeader(cacheFile, header);
    cacheFile.close();
    diskCacheValid = readOk && isCarouselCacheHeaderValid(header, newKeyHash, bookCount, renderer);
  }

  if (!allocateCarouselFrameSlots(targetFrameCount)) {
    return showedProgressPopup;
  }

  // Keep only the current frame in RAM; adjacent frames come from the SD
  // snapshot on demand instead of occupying another framebuffer-sized slot.
  const int selectedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  const int initialBookIdx = (selectedBookIdx >= 0 && selectedBookIdx < bookCount) ? selectedBookIdx : 0;
  auto loadOrRender = [&](int bookIdx, int slot) {
    if (!diskCacheValid || !loadCarouselFrameFromDisk(newKeyHash, bookCount, bookIdx, slot)) {
      renderCarouselFrame(bookIdx, slot);
    }
  };
  loadOrRender(initialBookIdx, 0);
  gCarouselCache.lastCenterIdx = initialBookIdx;

  if (gCarouselCache.frameCount >= 2 && bookCount >= 2) {
    const int nextIdx = (initialBookIdx + 1) % bookCount;
    loadOrRender(nextIdx, 1);
  }

  if (gCarouselCache.frameCount >= 3 && bookCount >= 3) {
    const int prevIdx = (initialBookIdx + bookCount - 1) % bookCount;
    loadOrRender(prevIdx, 2);
  }

  const bool hasFullFrameCache = gCarouselCache.frameCount >= targetFrameCount;
  gCarouselCache.key = newKey;
  gCarouselCache.keyHash = diskCacheValid ? newKeyHash : 0;
  carouselFramesReady = true;
  coverRendered = false;
  coverBufferStored = false;

  // Persist the freshly-rendered carousel snapshot back to SD after Home is
  // already visible so later reader->Home returns and carousel navigation can
  // bootstrap from disk instead of live-rendering covers again.
  if (!diskCacheValid && gCarouselCache.frameCount > 0) {
    if (hasFullFrameCache) {
      const bool cacheBuilt = buildCarouselCacheFile(newKey, newKeyHash, bookCount, showProgressPopup);
      if (cacheBuilt) {
        gCarouselCache.keyHash = newKeyHash;
        showedProgressPopup = true;
      }
    } else {
      LOG_INF("HOME", "carousel: skipping SD cache build in degraded frame cache mode");
    }
  }
  return showedProgressPopup;
}

void HomeActivity::loop() {
  if (isMinimalTheme()) {
    const int pressedFrontButton = mappedInput.getPressedFrontButton();
    const int releasedFrontButton = mappedInput.getReleasedFrontButton();

    if (minimalSuppressInitialFrontRelease) {
      if (releasedFrontButton >= 0) {
        minimalSuppressInitialFrontRelease = false;
        return;
      }
      if (!isAnyFrontButtonPressed(mappedInput)) {
        minimalSuppressInitialFrontRelease = false;
      }
    }

    if (minimalMenuOpen) {
      const auto menuItems = buildMinimalMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);
      const int menuCount = static_cast<int>(menuItems.size());
      if (menuCount <= 0) {
        minimalMenuOpen = false;
        minimalHomeNavIndex = -1;
        requestUpdate();
        return;
      }

      if (minimalMenuIndex >= menuCount) {
        minimalMenuIndex = menuCount - 1;
      }

      buttonNavigator.onPreviousPress([this, menuCount] {
        minimalMenuIndex = ButtonNavigator::previousIndex(minimalMenuIndex, menuCount);
        requestUpdate();
      });
      buttonNavigator.onNextPress([this, menuCount] {
        minimalMenuIndex = ButtonNavigator::nextIndex(minimalMenuIndex, menuCount);
        requestUpdate();
      });
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        minimalMenuOpen = false;
        minimalHomeNavIndex = -1;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        switch (menuItems[minimalMenuIndex].action) {
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
          case HomeMenuAction::ContinueReading:
          case HomeMenuAction::Settings:
            break;
        }
      }
      return;
    }

    const int homeNavCount = minimalHomeNavCount(!recentBooks.empty());
    if (minimalHomeNavIndex >= homeNavCount) {
      minimalHomeNavIndex = homeNavCount - 1;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      minimalHomeNavIndex = minimalHomeNavIndex < 0 ? homeNavCount - 1
                                                    : ButtonNavigator::previousIndex(minimalHomeNavIndex, homeNavCount);
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      minimalHomeNavIndex = minimalHomeNavIndex < 0 ? 0 : ButtonNavigator::nextIndex(minimalHomeNavIndex, homeNavCount);
      requestUpdate();
      return;
    }

    auto activateMinimalHomeNav = [this](int index) {
      switch (index) {
        case 0:
          minimalMenuOpen = true;
          minimalMenuIndex = 0;
          requestUpdate();
          break;
        case 1:
          onFileBrowserOpen();
          break;
        case 2:
          onSettingsOpen();
          break;
        case 3:
          onContinueReading();
          break;
      }
    };

    if (releasedFrontButton == HalGPIO::BTN_BACK) {
      minimalHomeNavIndex = 0;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_CONFIRM) {
      minimalHomeNavIndex = 1;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_LEFT) {
      minimalHomeNavIndex = 2;
      activateMinimalHomeNav(minimalHomeNavIndex);
      return;
    }
    if (releasedFrontButton == HalGPIO::BTN_RIGHT) {
      if (!recentBooks.empty()) {
        minimalHomeNavIndex = 3;
        activateMinimalHomeNav(minimalHomeNavIndex);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (minimalHomeNavIndex >= 0) {
        activateMinimalHomeNav(minimalHomeNavIndex);
      }
      return;
    }
    return;
  }

  const auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  const bool isLyraCarousel = themeType == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  const bool isLyraFlow = themeType == CrossPointSettings::UI_THEME::LYRA_FLOW;
  const int previousHighlightedBookIdx = getHighlightedBookIndex();

  if (isLyraCarousel || isLyraFlow) {
    // Carousel + Flow share the same navigation grammar now that Flow also
    // renders its menu as a horizontal icon bar:
    //   - L/R iterates within the current row
    //   - U/D toggles between rows (carousel ↕ shelf header ↕ shelf books
    //     ↕ icon bar). The shelf rows only exist in Flow (phase 1/2);
    //     Carousel still does the two-row carousel/menu toggle.
    const int bookCount = static_cast<int>(recentBooks.size());
    const int menuItemCount =
        static_cast<int>(buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks).size());

    // The shelf header is its own focus row (between carousel and books)
    // that appears whenever the user has at least one collection. The
    // header is what the user lands on when they press Down from the
    // carousel — from there L/R cycles the active collection and Down
    // enters its books. Carousel theme never shows the shelf, so it
    // ignores all of this and stays in the two-row carousel/menu model.
    const auto& collections = CollectionsStore::getInstance().getCollections();
    const bool shelfHeaderExists = isLyraFlow && !collections.empty();
    if (!isLyraFlow) {
      shelfHeaderFocused = false;  // safety: defensive reset off-Flow
    }
    const Collection* activeCollection =
        isLyraFlow ? CollectionsStore::getInstance().getActiveCollection() : nullptr;
    const int shelfCount = activeCollection ? static_cast<int>(activeCollection->bookPaths.size()) : 0;
    const int shelfStart = bookCount;
    const int shelfEnd = shelfStart + shelfCount;
    const int menuStart = shelfEnd;
    const int menuEnd = menuStart + menuItemCount;

    const bool inHeaderRow = shelfHeaderFocused;
    const bool inCarouselRow = !inHeaderRow && selectorIndex < bookCount;
    const bool inShelfRow =
        !inHeaderRow && (shelfCount > 0) && (selectorIndex >= shelfStart) && (selectorIndex < shelfEnd);
    const bool inMenuRow = !inHeaderRow && selectorIndex >= menuStart && selectorIndex < menuEnd;

    // Cycles the active collection. direction is +1 (next) or -1 (prev).
    // Wraps. Resets shelf-side render state so the new collection's
    // thumbs regenerate on next render. Persists the new activeId via
    // CollectionsStore::setActiveId which writes to SD.
    auto cycleActiveCollection = [this, &collections](int direction) {
      if (collections.size() <= 1) return;
      const std::string& currentActive = CollectionsStore::getInstance().getActiveId();
      int idx = 0;
      for (size_t i = 0; i < collections.size(); ++i) {
        if (collections[i].id == currentActive) {
          idx = static_cast<int>(i);
          break;
        }
      }
      const int n = static_cast<int>(collections.size());
      idx = (idx + direction + n) % n;
      CollectionsStore::getInstance().setActiveId(collections[idx].id);
      shelfScrollOffset = 0;
      shelfCoversLoaded = false;  // new collection probably has missing thumbs.
    };

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (inHeaderRow) {
        cycleActiveCollection(+1);
      } else if (inCarouselRow && bookCount > 0) {
        selectorIndex = (selectorIndex + 1) % bookCount;
        lastCarouselBookIndex = selectorIndex;
      } else if (inShelfRow && shelfCount > 0) {
        const int shelfIdx = selectorIndex - shelfStart;
        selectorIndex = shelfStart + (shelfIdx + 1) % shelfCount;
      } else if (inMenuRow && menuItemCount > 0) {
        const int menuIdx = selectorIndex - menuStart;
        selectorIndex = menuStart + (menuIdx + 1) % menuItemCount;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (inHeaderRow) {
        cycleActiveCollection(-1);
      } else if (inCarouselRow && bookCount > 0) {
        selectorIndex = (selectorIndex + bookCount - 1) % bookCount;
        lastCarouselBookIndex = selectorIndex;
      } else if (inShelfRow && shelfCount > 0) {
        const int shelfIdx = selectorIndex - shelfStart;
        selectorIndex = shelfStart + (shelfIdx + shelfCount - 1) % shelfCount;
      } else if (inMenuRow && menuItemCount > 0) {
        const int menuIdx = selectorIndex - menuStart;
        selectorIndex = menuStart + (menuIdx + menuItemCount - 1) % menuItemCount;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (inCarouselRow) {
        lastCarouselBookIndex = selectorIndex;
        if (shelfHeaderExists) {
          shelfHeaderFocused = true;
        } else if (shelfCount > 0) {
          selectorIndex = shelfStart;
        } else {
          selectorIndex = menuStart;
        }
      } else if (inHeaderRow) {
        shelfHeaderFocused = false;
        // Enter books if any; otherwise skip the empty row straight to
        // the menu so Down still does something useful.
        selectorIndex = (shelfCount > 0) ? shelfStart : menuStart;
      } else if (inShelfRow) {
        selectorIndex = menuStart;
      } else /* inMenuRow */ {
        selectorIndex = lastCarouselBookIndex;
      }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (inCarouselRow) {
        // Wrap to the bottom of the screen (menu row) for symmetry.
        lastCarouselBookIndex = selectorIndex;
        selectorIndex = menuStart;
      } else if (inHeaderRow) {
        shelfHeaderFocused = false;
        selectorIndex = lastCarouselBookIndex;
      } else if (inShelfRow) {
        if (shelfHeaderExists) {
          shelfHeaderFocused = true;
        } else {
          selectorIndex = lastCarouselBookIndex;
        }
      } else /* inMenuRow */ {
        if (shelfCount > 0) {
          selectorIndex = shelfStart;
        } else if (shelfHeaderExists) {
          shelfHeaderFocused = true;
        } else {
          selectorIndex = lastCarouselBookIndex;
        }
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

  // FlexBLE Collections — keep the shelf's selected spine visible. Recompute
  // from the live collection size each iteration; cheap and avoids stale
  // offsets if the user added/removed books from another activity.
  if (isLyraFlow) {
    const Collection* activeCollection = CollectionsStore::getInstance().getActiveCollection();
    if (activeCollection != nullptr) {
      const int shelfCount = static_cast<int>(activeCollection->bookPaths.size());
      const int shelfStart = static_cast<int>(recentBooks.size());
      // Mirror the visible-cell math used inside drawBookshelfStrip so the
      // scroll window matches the renderer's view exactly. Keep these in
      // lockstep with the constants at the top of drawBookshelfStrip.
      constexpr int kSidePad = 16;
      constexpr int kCellWidth = 100;
      constexpr int kCellGap = 16;
      const int availW = renderer.getScreenWidth() - 2 * kSidePad;
      const int cellTotalW = kCellWidth + kCellGap;
      const int visibleSpines = std::max(1, (availW + kCellGap) / cellTotalW);
      if (selectorIndex >= shelfStart && selectorIndex < shelfStart + shelfCount) {
        const int focused = selectorIndex - shelfStart;
        if (focused < shelfScrollOffset) shelfScrollOffset = focused;
        if (focused >= shelfScrollOffset + visibleSpines) shelfScrollOffset = focused - visibleSpines + 1;
      }
      if (shelfScrollOffset > std::max(0, shelfCount - visibleSpines)) {
        shelfScrollOffset = std::max(0, shelfCount - visibleSpines);
      }
      if (shelfScrollOffset < 0) shelfScrollOffset = 0;
    } else {
      shelfScrollOffset = 0;
    }
  }

  // Long-press Confirm on a focused book (carousel or shelf) → open the
  // file-action menu instead of opening the book. Mirrors the same gesture
  // in FileBrowserActivity. Threshold matches FileBrowser's GO_HOME_MS so
  // the muscle memory carries over. We require a non-empty book path so
  // long-pressing on the menu icon bar still falls through to the menu's
  // existing short-press handler.
  if (!longPressConfirmHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= 1000) {
    const std::string focusedPath = getFocusedBookPath();
    if (!focusedPath.empty()) {
      longPressConfirmHandled = true;
      showHomeBookActionMenu(focusedPath);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      // Swallow the release that ended the long-press so the short-press
      // open-book / menu-activation handlers below don't also fire.
      longPressConfirmHandled = false;
      return;
    }
    // Confirm while focus is on the shelf header behaves like Down: dive
    // into the active collection's books. If the collection is empty,
    // skip straight to the menu so Confirm still does something useful.
    if (shelfHeaderFocused) {
      const Collection* active = CollectionsStore::getInstance().getActiveCollection();
      const int shelfStart = static_cast<int>(recentBooks.size());
      const int shelfCount = (active != nullptr) ? static_cast<int>(active->bookPaths.size()) : 0;
      shelfHeaderFocused = false;
      selectorIndex = (shelfCount > 0) ? shelfStart : shelfStart;  // shelfStart == menuStart when shelfCount==0
      requestUpdate();
      return;
    }
    const auto& metrics = UITheme::getInstance().getMetrics();
    if (!metrics.homeContinueReadingInMenu && selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    // FlexBLE Collections — Flow theme's bookshelf row. Selection indices
    // sit between the carousel and the menu icon bar; open the matching
    // book path directly.
    const auto activeThemeForConfirm = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
    int shelfBookCount = 0;
    if (activeThemeForConfirm == CrossPointSettings::UI_THEME::LYRA_FLOW) {
      const Collection* activeCollection = CollectionsStore::getInstance().getActiveCollection();
      if (activeCollection != nullptr) {
        const int shelfStart = static_cast<int>(recentBooks.size());
        shelfBookCount = static_cast<int>(activeCollection->bookPaths.size());
        if (selectorIndex >= shelfStart && selectorIndex < shelfStart + shelfBookCount) {
          onSelectBook(activeCollection->bookPaths[selectorIndex - shelfStart]);
          return;
        }
      }
    }

    auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);
    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      menuItems.insert(menuItems.begin(), {tr(STR_CONTINUE_READING), Book, HomeMenuAction::ContinueReading});
    }
    const int menuSelectedIndex = selectorIndex - getHomeMenuSelectionOffset(recentBooks) - shelfBookCount;
    if (menuSelectedIndex < 0 || menuSelectedIndex >= static_cast<int>(menuItems.size())) {
      return;
    }

    switch (menuItems[menuSelectedIndex].action) {
      case HomeMenuAction::BrowseFiles:
        onFileBrowserOpen();
        break;
      case HomeMenuAction::ContinueReading:
        onContinueReading();
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

  if (isMinimalTheme()) {
    renderer.clearScreen();

    if (minimalMenuOpen) {
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
      const auto menuItems = buildMinimalMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);
      GUI.drawButtonMenu(
          renderer, Rect{0, metrics.homeTopPadding, pageWidth, pageHeight - metrics.homeTopPadding},
          static_cast<int>(menuItems.size()), minimalMenuIndex,
          [&menuItems](int index) { return std::string(menuItems[index].label); },
          [&menuItems](int index) { return menuItems[index].icon; });
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer();
      return;
    }

    bool bufferRestored = coverBufferStored && restoreCoverBuffer();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

    GUI.drawRecentBookCover(
        renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}, recentBooks, selectorIndex,
        coverRendered, coverBufferStored, bufferRestored, std::bind(&HomeActivity::storeCoverBuffer, this),
        hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent);

    const int homeNavCount = minimalHomeNavCount(!recentBooks.empty());
    if (minimalHomeNavIndex >= homeNavCount) {
      minimalHomeNavIndex = homeNavCount - 1;
    }
    MinimalTheme::setHomeButtonHintSelection(minimalHomeNavIndex);
    GUI.drawButtonHints(renderer, tr(STR_MENU), tr(STR_BROWSE), tr(STR_SETTINGS_TITLE),
                        recentBooks.empty() ? "" : tr(STR_READ));

    renderer.displayBuffer();

    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
      return;
    }

    if (!recentsLoaded && !recentsLoading) {
      recentsLoading = true;
      loadRecentCovers(metrics.homeCoverHeight);
    }
    return;
  }

  // Fast path: pre-rendered frames ready — memcpy + border overlay
  if (carouselFramesReady) {
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bookCount = static_cast<int>(recentBooks.size());
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int centerIdx = inCarouselRow ? selectorIndex : lastCarouselBookIndex;
    int slotIdx = gCarouselCache.findFrameSlot(centerIdx);

    if (frameBuffer && slotIdx < 0 && gCarouselCache.keyHash != 0 && bookCount > 0) {
      const int evictSlot = chooseCarouselEvictionSlot(centerIdx, bookCount);
      if (evictSlot >= 0 && loadCarouselFrameFromDisk(gCarouselCache.keyHash, bookCount, centerIdx, evictSlot)) {
        slotIdx = evictSlot;
      }
    }

    if (frameBuffer && slotIdx >= 0 && carouselFrames[slotIdx]) {
      memcpy(frameBuffer, carouselFrames[slotIdx], renderer.getBufferSize());
      LyraCarouselTheme::setPreRenderIndex(centerIdx);

      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             recentBooks, centerIdx, inCarouselRow);
      if (!inCarouselRow) {
        const auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);
        if (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) ==
            CrossPointSettings::UI_THEME::LYRA_CAROUSEL) {
          static_cast<const LyraCarouselTheme&>(GUI).drawButtonMenuSelectionOverlay(
              renderer, static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
              [&menuItems](int index) { return std::string(menuItems[index].label); },
              [&menuItems](int index) { return menuItems[index].icon; });
        }
      }

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

  // Flow theme decodes (selectorIndex - bookCount) as a "carousel center hint"
  // when no carousel slot is selected. Without this encoding the carousel
  // would drift forward by one as the user iterates through menu items.
  // Pinning the encoded hint to lastCarouselBookIndex keeps the carousel
  // visually stationary while the menu cursor moves.
  const int bookCountForRender = static_cast<int>(recentBooks.size());
  const auto activeThemeForRender = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  // Hold the carousel still whenever focus is NOT on a carousel book. That
  // includes focus on a menu item (selectorIndex >= bookCount) AND focus
  // on the shelf header — in the header case selectorIndex still sits in
  // carousel range from the cursor's last carousel position, but the
  // carousel itself shouldn't appear to be the active row.
  const bool flowCarouselHold = activeThemeForRender == CrossPointSettings::UI_THEME::LYRA_FLOW &&
                                (selectorIndex >= bookCountForRender || shelfHeaderFocused);
  const int coverSelectorIndex =
      flowCarouselHold ? (bookCountForRender +
                          (lastCarouselBookIndex >= 0 && lastCarouselBookIndex < bookCountForRender
                               ? lastCarouselBookIndex
                               : 0))
                       : selectorIndex;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, coverSelectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this),
                          hasAnyBookStats(currentBookStats) ? &currentBookStats : nullptr, currentBookProgressPercent);

  auto menuItems = buildHomeMenuItems(hasOpdsServers, hasReadingStats, hasBookmarks);

  const int menuStartY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuEndY = pageHeight - metrics.buttonHintsHeight;
  const int menuHeight = std::max(0, menuEndY - menuStartY);

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), {tr(STR_CONTINUE_READING), Book, HomeMenuAction::ContinueReading});
  }

  // FlexBLE Flow bookshelf — render strip between cover footer and icon bar,
  // and offset the menu's selected-index calculation so the icon-bar
  // selection-highlight tracks the right item when the cursor moves past
  // the shelf.
  const auto activeThemeForShelf = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  int shelfBookCount = 0;
  int shelfSelectedSpine = -1;
  if (activeThemeForShelf == CrossPointSettings::UI_THEME::LYRA_FLOW) {
    const Collection* activeCollection = CollectionsStore::getInstance().getActiveCollection();
    if (activeCollection != nullptr) {
      shelfBookCount = static_cast<int>(activeCollection->bookPaths.size());
      const int shelfStart = static_cast<int>(recentBooks.size());
      if (selectorIndex >= shelfStart && selectorIndex < shelfStart + shelfBookCount) {
        shelfSelectedSpine = selectorIndex - shelfStart;
      }
    }
    // Strip sits in the dead space between the carousel footer and the icon
    // bar. Center it vertically in that empty band so neither edge crowds.
    // Empirical anchors on Flow @ 480x800 portrait:
    //   carousel footer ends ~y=460 (cover + reading-progress bar)
    //   icon-bar label top  ~y=689
    //   midpoint            ~y=575
    constexpr int kShelfTabHeight = 18;          // matches drawBookshelfStrip
    constexpr int kShelfTabBottomGap = 12;       // matches drawBookshelfStrip
    constexpr int kShelfCellWidth = 100;         // matches drawBookshelfStrip
    constexpr int kShelfCellHeight = 150;        // matches drawBookshelfStrip
    constexpr int kShelfStripHeight = kShelfTabHeight + kShelfTabBottomGap + kShelfCellHeight;

    // Generate any missing cover thumbnails for this collection. First call
    // per onEnter pays the cost (load EPUB + write BMP per missing book);
    // subsequent calls short-circuit because Storage.exists() is true.
    if (!shelfCoversLoaded) {
      loadShelfCovers(kShelfCellWidth, kShelfCellHeight);
    }

    // Position the strip slightly below the geometric midpoint of the empty
    // band between cover tile bottom (~y=401) and icon-bar label top (~y=686).
    // User feedback iter 3: cells now show real cover art, so the strip can
    // sit closer to the icon bar without crowding — earlier high anchor was
    // a defensive choice when placeholders looked like UI chrome.
    // Re-tuned in iter 4 after cells grew to 100x150 (strip is now 180 tall):
    // 240 keeps the gap to the icon bar around 36 px so the row doesn't run
    // into the menu labels.
    constexpr int kEmptySpaceMidpointFromBottom = 240;
    const int shelfStripY = pageHeight - kEmptySpaceMidpointFromBottom - (kShelfStripHeight / 2);
    const Rect shelfRect{0, shelfStripY, pageWidth, kShelfStripHeight};

    const Collection* activeCollection2 = CollectionsStore::getInstance().getActiveCollection();
    const char* collectionName = (activeCollection2 != nullptr) ? activeCollection2->name.c_str() : "";
    // Header focus + cycle-hint flags forwarded to the theme so it can draw
    // the "◀ Collection ▶" affordance only when both apply. Otherwise the
    // arrows would be misleading (single-collection case can't cycle).
    const bool hasMultipleCollections = CollectionsStore::getInstance().getCollections().size() > 1;

    // Resolve a concrete (dimension-substituted) cover thumbnail path for
    // each book. getThumbBmpPath() returns a template like
    //   /.crosspoint/<book>/thumb_[WIDTH]x[HEIGHT].bmp
    // which is not a real file — UITheme::getCoverThumbPath fills in the
    // placeholders. Books whose thumb wasn't generated (or whose generation
    // failed in loadShelfCovers) get an empty string so the renderer draws
    // the placeholder card instead of trying to open a non-existent file.
    std::vector<std::string> shelfCoverPaths;
    if (activeCollection2 != nullptr) {
      shelfCoverPaths.reserve(activeCollection2->bookPaths.size());
      for (const auto& path : activeCollection2->bookPaths) {
        std::string templatePath;
        if (FsHelpers::hasEpubExtension(path)) {
          templatePath = Epub(path, "/.crosspoint").getThumbBmpPath();
        } else if (FsHelpers::hasXtcExtension(path)) {
          templatePath = Xtc(path, "/.crosspoint").getThumbBmpPath();
        }
        std::string resolved;
        if (!templatePath.empty()) {
          resolved = UITheme::getCoverThumbPath(templatePath, kShelfCellWidth, kShelfCellHeight);
          if (!resolved.empty() && !Storage.exists(resolved.c_str())) {
            resolved.clear();
          }
        }
        shelfCoverPaths.push_back(std::move(resolved));
      }
    }
    static_cast<const LyraFlowTheme&>(GUI).drawBookshelfStrip(renderer, shelfRect, collectionName, shelfCoverPaths,
                                                              shelfSelectedSpine, shelfScrollOffset,
                                                              shelfHeaderFocused, hasMultipleCollections);
  }

  // While the shelf header is focused, force "no menu selection" so the
  // icon bar doesn't show a misleading highlight from the carousel/menu's
  // shared selectorIndex value.
  const int menuSelectedIndex = shelfHeaderFocused
                                    ? -1
                                    : selectorIndex - getHomeMenuSelectionOffset(recentBooks) - shelfBookCount;
  GUI.drawButtonMenu(
      renderer, Rect{0, menuStartY, pageWidth, menuHeight}, static_cast<int>(menuItems.size()), menuSelectedIndex,
      [&menuItems](int index) { return std::string(menuItems[index].label); },
      [&menuItems](int index) { return menuItems[index].icon; });

  const auto activeTheme = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  // LYRA_CAROUSEL and LYRA_FLOW share a row-and-column grammar: L/R iterates
  // within the active row, U/D toggles rows — label as Left/Right.
  // Everything else (non-carousel themes) hints "Up/Down" since their L/R is
  // a thin wrapper over the menu's vertical buttonNavigator.
  MappedInputManager::Labels labels;
  if (activeTheme == CrossPointSettings::UI_THEME::LYRA_CAROUSEL ||
      activeTheme == CrossPointSettings::UI_THEME::LYRA_FLOW) {
    labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  } else {
    labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
    return;
  }

  if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }

  if (carouselWarmupPending && !carouselFramesReady) {
    // Resolve any missing cover thumbs first, then warm the carousel snapshot.
    // Cover generation needs more contiguous heap than the frame cache path.
    carouselWarmupPending = false;
    const bool showedWarmupProgress = preRenderCarouselFrames(true);
    if (carouselFramesReady || showedWarmupProgress) {
      requestUpdate();
    }
  }
}

void HomeActivity::renderCarouselFrame(int bookIdx, int slotIdx) {
  const auto start = millis();
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || !gCarouselCache.frames[slotIdx]) return;
  BookReadingStats frameStats;
  float frameProgressPercent = -1.0f;
  bool usedCachedStats = false;
  renderCarouselFrameToCurrentBuffer(bookIdx, &frameStats, &frameProgressPercent, &usedCachedStats);

  memcpy(gCarouselCache.frames[slotIdx], frameBuffer, renderer.getBufferSize());
  gCarouselCache.frameBookIdx[slotIdx] = bookIdx;
  carouselFrames[slotIdx] = gCarouselCache.frames[slotIdx];
  LOG_DBG("HOME", "carousel: renderCarouselFrame book=%d slot=%d cached=%s took %lums", bookIdx, slotIdx,
          usedCachedStats ? "yes" : "no", millis() - start);
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  (void)centerIdx;
  (void)bookCount;
  // The current carousel cache keeps one frame in RAM; other frames are paged
  // from the SD snapshot cache on demand in render().
}

void HomeActivity::onSelectBook(const std::string& path) {
  gCarouselCache.invalidate();
  freeCarouselFrames();
  if (Storage.exists(CAROUSEL_CACHE_TMP_PATH)) {
    Storage.remove(CAROUSEL_CACHE_TMP_PATH);
  }
  activityManager.goToReader(path);
}

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onContinueReading() {
  if (!recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
  }
}

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onReadingStatsOpen() {
  const int highlightedBookIdx = getHighlightedBookIndex();
  const std::string bookTitle =
      highlightedBookIdx >= 0 ? recentBooks[highlightedBookIdx].title : std::string(tr(STR_READING_STATS));
  const std::string bookPath = highlightedBookIdx >= 0 ? recentBooks[highlightedBookIdx].path : std::string();
  const std::string coverBmpPath =
      highlightedBookIdx >= 0 ? recentBooks[highlightedBookIdx].coverBmpPath : std::string();
  // FlexBLE inherits chintanvajariya's richer BookStatsActivity (recent-books
  // navigation + cover image). The richer constructor needs path/cover and a
  // backToHome flag; launched from home, so back returns here.
  startActivityForResult(
      std::make_unique<BookStatsActivity>(renderer, mappedInput, bookPath, bookTitle, coverBmpPath, currentBookStats,
                                          globalStats, /*backToHome=*/true),
      [this](const ActivityResult&) { requestUpdate(); });
}

void HomeActivity::onBookmarksOpen() {
  startActivityForResult(std::make_unique<BookmarksHomeActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}
