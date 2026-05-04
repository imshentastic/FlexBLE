#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
// Cover layout — keep Lyra Carousel's general geometry, but render the books
// with the same visual treatment as Lyra Flow.
constexpr int kCenterCoverMaxW = LyraCarouselTheme::kCenterCoverW;
constexpr int kCenterCoverMaxH = LyraCarouselTheme::kCenterCoverH;
constexpr int kSideCoverMaxW = LyraCarouselTheme::kSideCoverW;
constexpr int kSideCoverMaxH = LyraCarouselTheme::kSideCoverH;
constexpr int kCoverTopPad = 18;
constexpr int kBaseDisplayCenterW = (kCenterCoverMaxW * 86) / 100;
constexpr int kBaseDisplayCenterH = (kCenterCoverMaxH * 86) / 100;
constexpr int kDisplayCenterW = std::min(kCenterCoverMaxW, kBaseDisplayCenterW + 24);
constexpr int kDisplayCenterH = std::min(kCenterCoverMaxH, kBaseDisplayCenterH + 24);
constexpr int kNearSideW = (kBaseDisplayCenterW * 26) / 100;
constexpr int kFarSideW = (kBaseDisplayCenterW * 21) / 100;
constexpr int kNearSideInnerH = (kBaseDisplayCenterH * 90) / 100;
constexpr int kNearSideOuterH = (kBaseDisplayCenterH * 82) / 100;
constexpr int kFarSideInnerH = (kBaseDisplayCenterH * 84) / 100;
constexpr int kFarSideOuterH = (kBaseDisplayCenterH * 74) / 100;
constexpr int kSideOutlineW = 2;
constexpr int kSideCornerRadius = 5;
constexpr int kCoverStackLift = 15;
constexpr int kCenterCoverTopInset = (((kCenterCoverMaxH - kDisplayCenterH) / 2) > kCoverStackLift)
                                         ? ((kCenterCoverMaxH - kDisplayCenterH) / 2) - kCoverStackLift
                                         : 0;

constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kMenuLabelFontId = SMALL_FONT_ID;
constexpr int kDotSize = 8;  // px square dot
constexpr int kDotGap = 6;   // px between dots
constexpr int kTitleTopClearance = 4;
constexpr int kTitleDrawOffset = 5;
constexpr int kTitleBottomGap = 8;
constexpr int kMenuLabelTopGap = 3;
constexpr int kMenuLabelBottomGap = 4;
constexpr int kMenuRowDrop = 31;

constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;    // always-visible outline around centre cover
constexpr int kSelectionLineW = 3;  // thicker outline when centre cover is selected
constexpr int kCenterOutlineW = 4;  // white ring around centre cover

// Icon row — icons are 32×32 bitmaps; drawIcon does NOT scale
constexpr int kMenuIconSize = 32;  // must match actual bitmap dimensions
constexpr int kMenuIconPad = 14;   // symmetric vertical padding → tile height = 60
constexpr int kHighlightPad = 7;   // highlight padding around the selected icon
// Row is anchored to the bottom of the screen, just above button hints
constexpr int kButtonHintsH = LyraCarouselMetrics::values.buttonHintsHeight;

int lastCarouselSelectorIndex = -1;
Rect lastCenterCoverRect{0, 0, 0, 0};
Rect cachedCenterCoverRects[LyraCarouselMetrics::values.homeRecentBooksCount];

void drawMenuBookmarkIcon(const GfxRenderer& renderer, int x, int y, bool selected) {
  constexpr int ribbonWidth = 16;
  constexpr int ribbonHeight = 22;
  constexpr int notchSize = 6;
  const int iconX = x + (kMenuIconSize - ribbonWidth) / 2;
  const int iconY = y + 4;
  const int centerX = iconX + ribbonWidth / 2;

  const int polyX[5] = {iconX, iconX + ribbonWidth, iconX + ribbonWidth, centerX, iconX};
  const int polyY[5] = {iconY, iconY, iconY + ribbonHeight, iconY + ribbonHeight - notchSize, iconY + ribbonHeight};
  renderer.fillPolygon(polyX, polyY, 5, !selected);
}

void drawPerspectiveOutline(GfxRenderer& renderer, int x, int y, int width, int leftHeight, int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  const int topLeft = (maxHeight - leftHeight) / 2;
  const int topRight = (maxHeight - rightHeight) / 2;
  const int bottomLeft = topLeft + leftHeight - 1;
  const int bottomRight = topRight + rightHeight - 1;
  const int rightX = x + width - 1;

  renderer.drawLine(x, y + topLeft, rightX, y + topRight, kSideOutlineW, true);
  renderer.drawLine(x, y + bottomLeft, rightX, y + bottomRight, kSideOutlineW, true);
  renderer.fillRect(x, y + topLeft, kSideOutlineW, leftHeight, true);
  renderer.fillRect(rightX - kSideOutlineW + 1, y + topRight, kSideOutlineW, rightHeight, true);
  renderer.fillRect(x, y + maxHeight + 1, width, 2, false);
}

void fillPerspectiveSilhouette(GfxRenderer& renderer, int x, int y, int width, int leftHeight, int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  renderer.fillRect(x, y, width, maxHeight, false);
  for (int dx = 0; dx < width; ++dx) {
    const int columnHeight = (width <= 1) ? leftHeight : (leftHeight + ((rightHeight - leftHeight) * dx) / (width - 1));
    const int top = y + (maxHeight - columnHeight) / 2;
    renderer.fillRect(x + dx, top, 1, columnHeight, true);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------
void LyraCarouselTheme::setPreRenderIndex(int idx) {
  lastCarouselSelectorIndex = idx;
  if (idx >= 0 && idx < LyraCarouselMetrics::values.homeRecentBooksCount) {
    const Rect cachedRect = cachedCenterCoverRects[idx];
    if (cachedRect.width > 0 && cachedRect.height > 0) lastCenterCoverRect = cachedRect;
  }
}

void LyraCarouselTheme::drawCarouselBorder(GfxRenderer& renderer, Rect coverRect, bool inCarouselRow) const {
  if (!inCarouselRow) return;
  Rect borderRect = lastCenterCoverRect;
  if (borderRect.width <= 0 || borderRect.height <= 0) {
    const int screenW = renderer.getScreenWidth();
    const int fallbackX = (screenW - kDisplayCenterW) / 2;
    const int fallbackY = coverRect.y + kCoverTopPad + kCenterCoverTopInset;
    borderRect = Rect{fallbackX, fallbackY, kDisplayCenterW, kDisplayCenterH};
  }
  renderer.drawRoundedRect(borderRect.x, borderRect.y, borderRect.width, borderRect.height, kSelectionLineW,
                           kCornerRadius, true);
}

// ---------------------------------------------------------------------------
// Carousel cover strip
// ---------------------------------------------------------------------------
void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                            std::function<bool()> storeCoverBuffer, const BookReadingStats* stats,
                                            float progressPercent) const {
  (void)bufferRestored;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  // When navigating the icon row, keep showing the last carousel position —
  // falling back to 0 on first use (lastCarouselSelectorIndex == -1).
  const bool inCarouselRow = (selectorIndex < bookCount);
  int centerIdx = inCarouselRow ? selectorIndex : (lastCarouselSelectorIndex >= 0 ? lastCarouselSelectorIndex : 0);

  if (centerIdx >= bookCount) {
    centerIdx = bookCount - 1;
    coverRendered = false;
    coverBufferStored = false;
  }

  // cppcheck-suppress knownConditionTrueFalse
  // Reachable as false when navigating the icon row with a previously-set
  // lastCarouselSelectorIndex; cppcheck only models the inCarouselRow=true path.
  if (centerIdx != lastCarouselSelectorIndex) {
    coverRendered = false;
    coverBufferStored = false;
  }

  const int screenW = renderer.getScreenWidth();
  const int textMaxWidth = std::min(screenW - 40, kCenterCoverMaxW + 40);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, recentBooks[centerIdx].title.c_str(), textMaxWidth, 2, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
  const int reservedTitleBlockHeight = titleLineHeight * 2;
  const int titleY = rect.y + kTitleTopClearance;
  const int centerTileY = std::max(rect.y + kCoverTopPad, titleY + reservedTitleBlockHeight + kTitleBottomGap);
  const int sideMaxHeight = std::max(kNearSideInnerH, kNearSideOuterH);
  const int centerDrawY = centerTileY + kCenterCoverTopInset;
  const int sideTileY = centerDrawY + (kDisplayCenterH - sideMaxHeight) / 2;

  const int centerX = (screenW - kDisplayCenterW) / 2;
  const int nearOverlap = 4;
  const int farOverlap = 2;
  constexpr int nearCoverInset = 10;
  const int baseLeftNearX = centerX - kNearSideW + nearOverlap;
  const int baseRightNearX = centerX + kDisplayCenterW - nearOverlap;
  const int leftNearX = baseLeftNearX + nearCoverInset;
  const int rightNearX = baseRightNearX - nearCoverInset;
  const int leftFarX = std::max(0, baseLeftNearX - kFarSideW + farOverlap);
  const int rightFarX = std::min(screenW - kFarSideW, baseRightNearX + kNearSideW - farOverlap);

  auto drawCenterCover = [&](int bookIdx, Rect& outRect) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    outRect = Rect{centerX, centerDrawY, kDisplayCenterW, kDisplayCenterH};

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCenterCoverMaxW, kCenterCoverMaxH);
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          const float srcW = static_cast<float>(bitmap.getWidth());
          const float srcH = static_cast<float>(bitmap.getHeight());
          const float srcRatio = srcW / srcH;
          const float targetRatio = static_cast<float>(kDisplayCenterW) / static_cast<float>(kDisplayCenterH);
          float cropX = 0.0f;
          float cropY = 0.0f;

          if (srcRatio > targetRatio) {
            cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
          } else if (srcRatio < targetRatio) {
            cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
          }

          renderer.fillRect(outRect.x - kCenterOutlineW, outRect.y - kCenterOutlineW,
                            outRect.width + 2 * kCenterOutlineW, outRect.height + 2 * kCenterOutlineW, false);
          renderer.drawBitmap(bitmap, outRect.x, outRect.y, outRect.width, outRect.height, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(outRect.x, outRect.y, outRect.width, outRect.height, kCornerRadius,
                                                 Color::White);
          file.close();
          return true;
        }
        file.close();
      }
    }

    renderer.fillRect(outRect.x - kCenterOutlineW, outRect.y - kCenterOutlineW, outRect.width + 2 * kCenterOutlineW,
                      outRect.height + 2 * kCenterOutlineW, false);
    renderer.drawRoundedRect(outRect.x, outRect.y, outRect.width, outRect.height, 1, kCornerRadius, true);
    renderer.fillRoundedRect(outRect.x, outRect.y + outRect.height / 3, outRect.width, 2 * outRect.height / 3,
                             kCornerRadius, /*roundTopLeft=*/false, /*roundTopRight=*/false,
                             /*roundBottomLeft=*/true, /*roundBottomRight=*/true, Color::Black);
    renderer.drawIcon(CoverIcon, outRect.x + outRect.width / 2 - 16, outRect.y + outRect.height / 2 - 16, 32, 32);
    return false;
  };

  auto drawSideCover = [&](int bookIdx, int x, int width, int leftHeight, int rightHeight) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kSideCoverMaxW, kSideCoverMaxH);
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const int sideHeight = std::max(leftHeight, rightHeight);
          renderer.fillRect(x, sideTileY, width, sideHeight, false);
          renderer.drawPerspectiveBitmap(bitmap, x, sideTileY, width, leftHeight, rightHeight);
          renderer.maskRoundedRectOutsideCorners(x, sideTileY, width, sideHeight, kSideCornerRadius, Color::White);
          file.close();
          drawPerspectiveOutline(renderer, x, sideTileY, width, leftHeight, rightHeight);
          return true;
        }
        file.close();
      }
    }

    fillPerspectiveSilhouette(renderer, x, sideTileY, width, leftHeight, rightHeight);
    renderer.maskRoundedRectOutsideCorners(x, sideTileY, width, std::max(leftHeight, rightHeight), kSideCornerRadius,
                                           Color::White);
    return false;
  };

  if (!coverRendered) {
    lastCarouselSelectorIndex = centerIdx;

    // Clear the entire cover tile to white so stale pixels from old positions
    // don't persist (drawBitmap only sets black pixels, never clears).
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

    // More literal Lyra Flow layout: two visible books per side when available.
    const int leftNearIdx = (centerIdx + bookCount - 1) % bookCount;
    const int leftFarIdx = (centerIdx + bookCount - 2) % bookCount;
    const int rightNearIdx = (centerIdx + 1) % bookCount;
    const int rightFarIdx = (centerIdx + 2) % bookCount;

    if (bookCount >= 5) drawSideCover(leftFarIdx, leftFarX, kFarSideW, kFarSideInnerH, kFarSideOuterH);
    if (bookCount >= 4) drawSideCover(rightFarIdx, rightFarX, kFarSideW, kFarSideOuterH, kFarSideInnerH);
    if (bookCount >= 2) drawSideCover(leftNearIdx, leftNearX, kNearSideW, kNearSideInnerH, kNearSideOuterH);
    if (bookCount >= 3) drawSideCover(rightNearIdx, rightNearX, kNearSideW, kNearSideOuterH, kNearSideInnerH);

    Rect centerCoverRect{};
    drawCenterCover(centerIdx, centerCoverRect);
    lastCenterCoverRect = centerCoverRect;
    if (centerIdx >= 0 && centerIdx < LyraCarouselMetrics::values.homeRecentBooksCount) {
      cachedCenterCoverRects[centerIdx] = centerCoverRect;
    }

    // Title sits above the center cover; wrap to 2 lines and ellipsize on line 2 if needed.
    const int textCenterX = centerCoverRect.x + centerCoverRect.width / 2;
    const int titleVerticalInset = (reservedTitleBlockHeight - titleBlockHeight) / 2;
    int currentTitleY = titleY + titleVerticalInset + kTitleDrawOffset;
    for (const auto& titleLine : titleLines) {
      const int titleW = renderer.getTextWidth(kTitleFontId, titleLine.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(kTitleFontId, textCenterX - titleW / 2, currentTitleY, titleLine.c_str(), true,
                        EpdFontFamily::BOLD);
      currentTitleY += titleLineHeight;
    }

    // Dots — centred under the displayed centre cover, count = actual book count
    const int dotsY = centerCoverRect.y + centerCoverRect.height + 8;
    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerCoverRect.x + (centerCoverRect.width - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx)
        renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      else
        renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      dotX += kDotSize + kDotGap;
    }

    // Lyra-style per-book stats, centered below the cover.
    const int statsLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int progressLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const bool hasStats = (stats != nullptr && stats->sessionCount > 0);
    const bool hasProgress = progressPercent >= 0.0f;
    constexpr int footerTopPad = 2;
    int infoY = dotsY + kDotSize + 8 + footerTopPad;

    if (hasStats) {
      char buf[48];
      char statLine[64];
      BookReadingStats::formatDuration(stats->totalReadingSeconds, buf, sizeof(buf));
      snprintf(statLine, sizeof(statLine), "%s%s", tr(STR_STATS_TOTAL_TIME), buf);
      const int totalTimeW = renderer.getTextWidth(SMALL_FONT_ID, statLine);
      renderer.drawText(SMALL_FONT_ID, textCenterX - totalTimeW / 2, infoY, statLine, true);
      infoY += statsLineHeight + 3;
    }

    if (hasProgress) {
      constexpr int progressBarHeight = 4;
      constexpr int progressTopPad = 2;
      const int progressBarWidth = centerCoverRect.width;
      const int filledWidth =
          std::clamp(static_cast<int>((progressPercent / 100.0f) * progressBarWidth), 0, progressBarWidth);
      char progressLabel[16];
      snprintf(progressLabel, sizeof(progressLabel), "%.0f%%", progressPercent);
      const int progressLabelW = renderer.getTextWidth(UI_10_FONT_ID, progressLabel, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, textCenterX - progressLabelW / 2, infoY + progressTopPad, progressLabel, true,
                        EpdFontFamily::BOLD);
      const int progressBarX = textCenterX - progressBarWidth / 2;
      const int progressBarY = infoY + progressTopPad + progressLineHeight + 2;
      renderer.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, true);
      if (filledWidth > 0) {
        renderer.fillRect(progressBarX, progressBarY, filledWidth, progressBarHeight, true);
      }
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  } else if (lastCenterCoverRect.width <= 0 || lastCenterCoverRect.height <= 0) {
    lastCenterCoverRect = Rect{centerX, centerDrawY, kDisplayCenterW, kDisplayCenterH};
  }

  // Always outline the centre cover at its own edge (white ring sits outside the black line);
  // thicker when the carousel row is active
  const int outlineW = inCarouselRow ? kSelectionLineW : kThinOutlineW;
  renderer.drawRoundedRect(lastCenterCoverRect.x, lastCenterCoverRect.y, lastCenterCoverRect.width,
                           lastCenterCoverRect.height, outlineW, kCornerRadius, true);
}

// ---------------------------------------------------------------------------
// Horizontal icon-only menu row — anchored to bottom of screen
// ---------------------------------------------------------------------------
void LyraCarouselTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;
  (void)rect;

  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int tileW = renderer.getScreenWidth() / buttonCount;
  const int labelLineHeight = renderer.getLineHeight(kMenuLabelFontId);
  // Anchor row just above button hints, ignoring rect.y which may be off-screen
  // for large cover tiles
  const int rowY = renderer.getScreenHeight() - kButtonHintsH - tileH - kMenuLabelTopGap - labelLineHeight -
                   kMenuLabelBottomGap + kMenuRowDrop;
  const int labelY = rowY - kMenuLabelTopGap - labelLineHeight;

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = i * tileW;
    const int iconX = tileX + (tileW - kMenuIconSize) / 2;
    const int iconY = rowY + kMenuIconPad;

    const bool selected = (selectedIndex == i);
    if (selected) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = rowY + (tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kCornerRadius,
                               Color::Black);
    }

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::BookmarkIcon) {
        drawMenuBookmarkIcon(renderer, iconX, iconY, selected);
      } else {
        const uint8_t* bmp = iconForName(icon, kMenuIconSize);
        if (bmp != nullptr) {
          if (selected)
            renderer.drawIconInverted(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          else
            renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
        }
      }
    }
  }

  renderer.fillRect(0, labelY, renderer.getScreenWidth(), labelLineHeight, false);
  if (selectedIndex >= 0 && selectedIndex < buttonCount && buttonLabel != nullptr) {
    const std::string labelStr = buttonLabel(selectedIndex);
    const auto centeredLabel =
        renderer.truncatedText(kMenuLabelFontId, labelStr.c_str(), renderer.getScreenWidth() - 40);
    const int labelWidth = renderer.getTextWidth(kMenuLabelFontId, centeredLabel.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(kMenuLabelFontId, (renderer.getScreenWidth() - labelWidth) / 2, labelY + 2, centeredLabel.c_str(),
                      true, EpdFontFamily::REGULAR);
  }
}

// ---------------------------------------------------------------------------
// List — solid black highlight, inverted text and icons on selected row
// ---------------------------------------------------------------------------
void LyraCarouselTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                 const std::function<std::string(int index)>& rowTitle,
                                 const std::function<std::string(int index)>& rowSubtitle,
                                 const std::function<UIIcon(int index)>& rowIcon,
                                 const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                 const std::function<bool(int index)>& isHeader) const {
  constexpr int hPad = 8;
  constexpr int listIconSz = 24;
  constexpr int mainMenuIconSz = 32;
  constexpr int maxValWidth = 200;
  constexpr int cornerRadius = 6;
  constexpr int sectionHeaderTopPadding = 20;

  const int rowHeight = (rowSubtitle != nullptr) ? LyraCarouselMetrics::values.listWithSubtitleRowHeight
                                                 : LyraCarouselMetrics::values.listRowHeight;
  const int pageItems = rect.height / rowHeight;
  if (pageItems <= 0 || itemCount <= 0) return;
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  const auto isHeaderRow = [&isHeader](int index) { return isHeader != nullptr && isHeader(index); };

  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraCarouselMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraCarouselMetrics::values.scrollBarWidth, scrollBarY,
                      LyraCarouselMetrics::values.scrollBarWidth, scrollBarHeight, true);
  }

  const int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraCarouselMetrics::values.scrollBarWidth + LyraCarouselMetrics::values.scrollBarRightOffset)
                      : 1);
  const int pageStartIndex = std::max(0, selectedIndex / pageItems) * pageItems;

  // Solid black highlight bar, skipping section headers and accounting for
  // the extra top padding inserted before each header row.
  if (selectedIndex >= 0 && !isHeaderRow(selectedIndex)) {
    int selectedY = rect.y;
    for (int rowIndex = pageStartIndex; rowIndex < selectedIndex; ++rowIndex) {
      selectedY += rowHeight;
      if (isHeaderRow(rowIndex + 1)) selectedY += sectionHeaderTopPadding;
    }
    renderer.fillRoundedRect(rect.x + LyraCarouselMetrics::values.contentSidePadding, selectedY,
                             contentWidth - LyraCarouselMetrics::values.contentSidePadding * 2, rowHeight,
                             kCornerRadius, Color::Black);
  }

  int textX = rect.x + LyraCarouselMetrics::values.contentSidePadding + hPad;
  int textWidth = contentWidth - LyraCarouselMetrics::values.contentSidePadding * 2 - hPad * 2;
  int iconSize = 0;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSz : listIconSz;
    textX += iconSize + hPad;
    textWidth -= iconSize + hPad;
  }

  const int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  int currentY = rect.y;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    if (i > pageStartIndex && isHeaderRow(i)) currentY += sectionHeaderTopPadding;
    const int itemY = currentY;
    currentY += rowHeight;
    const bool sel = (i == selectedIndex);

    if (isHeaderRow(i)) {
      std::string label = rowTitle(i);
      std::transform(label.begin(), label.end(), label.begin(),
                     [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      const auto headerLabel = renderer.truncatedText(UI_10_FONT_ID, label.c_str(),
                                                      contentWidth - LyraCarouselMetrics::values.contentSidePadding * 2,
                                                      EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rect.x + LyraCarouselMetrics::values.contentSidePadding, itemY + 7,
                        headerLabel.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawLine(rect.x, itemY + rowHeight - 1, rect.x + contentWidth, itemY + rowHeight - 1, true);
      continue;
    }

    int rowTextWidth = textWidth;

    int valueWidth = 0;
    std::string valueText;
    if (rowValue != nullptr) {
      valueText = rowValue(i);
      valueText = renderer.truncatedText(UI_10_FONT_ID, valueText.c_str(), maxValWidth);
      valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + hPad;
      rowTextWidth -= valueWidth;
    }

    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), rowTextWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 7, item.c_str(), !sel);

    if (rowIcon != nullptr) {
      const uint8_t* iconBitmap = iconForName(rowIcon(i), iconSize);
      if (iconBitmap != nullptr) {
        const int ix = rect.x + LyraCarouselMetrics::values.contentSidePadding + hPad;
        if (sel)
          renderer.drawIconInverted(iconBitmap, ix, itemY + iconY, iconSize, iconSize);
        else
          renderer.drawIcon(iconBitmap, ix, itemY + iconY, iconSize, iconSize);
      }
    }

    if (rowSubtitle != nullptr) {
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), rowTextWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), !sel);
    }

    if (!valueText.empty()) {
      if (sel && highlightValue) {
        renderer.fillRoundedRect(
            rect.x + contentWidth - LyraCarouselMetrics::values.contentSidePadding - hPad - valueWidth, itemY,
            valueWidth + hPad, rowHeight, cornerRadius, Color::Black);
      }
      renderer.drawText(UI_10_FONT_ID,
                        rect.x + contentWidth - LyraCarouselMetrics::values.contentSidePadding - valueWidth, itemY + 6,
                        valueText.c_str(), !sel);
    }
  }
}

// ---------------------------------------------------------------------------
// Tab bar — solid black background + solid black active tab, inverted text
// ---------------------------------------------------------------------------
void LyraCarouselTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                                   bool selected) const {
  constexpr int hPad = 8;
  int currentX = rect.x + LyraCarouselMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, EpdFontFamily::REGULAR);

    if (tab.selected) {
      if (selected) {
        renderer.fillRoundedRect(currentX, rect.y + 1, textWidth + 2 * hPad, rect.height - 4, kCornerRadius,
                                 Color::Black);
      } else {
        renderer.drawRoundedRect(currentX, rect.y, textWidth + 2 * hPad, rect.height - 3, 1, kCornerRadius, true);
      }
    }

    renderer.drawText(UI_10_FONT_ID, currentX + hPad, rect.y + 6, tab.label, !(tab.selected && selected),
                      EpdFontFamily::REGULAR);

    currentX += textWidth + LyraCarouselMetrics::values.tabSpacing + 2 * hPad;
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}
