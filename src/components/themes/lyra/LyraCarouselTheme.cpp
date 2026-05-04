#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/chart.h"
#include "components/icons/cover.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
// Cover layout — keep Lyra Carousel's general geometry, but render the books
// with the same visual treatment as Lyra Flow.
constexpr int kCenterCoverMaxW = LyraCarouselTheme::kCenterCoverW;
constexpr int kCenterCoverMaxH = LyraCarouselTheme::kCenterCoverH;
constexpr int kSideCoverMaxW = LyraCarouselTheme::kSideCoverW;
constexpr int kSideCoverMaxH = LyraCarouselTheme::kSideCoverH;
constexpr int kOverlap = 60;
constexpr int kCoverTopPad = 10;
constexpr int kSidePerspectiveW = (kCenterCoverMaxW * 3) / 10;
constexpr int kSideInnerH = (kCenterCoverMaxH * 9) / 10;
constexpr int kSideOuterH = (kCenterCoverMaxH * 8) / 10;
constexpr int kSideOutlineW = 2;
constexpr int kFlowCenterBaseW = 220;

constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kDotSize = 8;  // px square dot
constexpr int kDotGap = 6;   // px between dots

constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;    // always-visible outline around centre cover
constexpr int kSelectionLineW = 3;  // thicker outline when centre cover is selected
constexpr int kCenterOutlineW = 4;  // white ring around centre cover

// Icon row — icons are 32×32 bitmaps; drawIcon does NOT scale
constexpr int kMenuIconSize = 32;  // must match actual bitmap dimensions
constexpr int kMenuIconPad = 14;   // symmetric vertical padding → tile height = 60
constexpr int kHighlightPad = 12;  // horizontal padding around the icon on each side
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

const uint8_t* iconBitmapFor(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Chart:
      return ChartIcon;
    case UIIcon::Library:
      return LibraryIcon;
    default:
      return nullptr;
  }
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

int scaleFlowWidth(int value) {
  return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * static_cast<float>(kCenterCoverMaxW) /
                                                  static_cast<float>(kFlowCenterBaseW))));
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
    borderRect = Rect{(screenW - kCenterCoverMaxW) / 2, coverRect.y + kCoverTopPad, kCenterCoverMaxW, kCenterCoverMaxH};
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
  (void)stats;
  (void)progressPercent;
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
  const int centerTileY = rect.y + kCoverTopPad;
  const int sideMaxHeight = std::max(kSideInnerH, kSideOuterH);
  const int sideTileY = centerTileY + (kCenterCoverMaxH - sideMaxHeight) / 2;

  const int centerX = (screenW - kCenterCoverMaxW) / 2;
  const int nearOverlap = scaleFlowWidth(15);
  const int farStep = scaleFlowWidth(50);
  const int leftNearX = centerX - kSidePerspectiveW + nearOverlap;
  const int rightNearX = centerX + kCenterCoverMaxW - nearOverlap;
  const int leftFarX = leftNearX - farStep;
  const int rightFarX = rightNearX + farStep;

  auto drawCenterCover = [&](int bookIdx, Rect& outRect) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    outRect = Rect{centerX, centerTileY, kCenterCoverMaxW, kCenterCoverMaxH};

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kCenterCoverMaxW, kCenterCoverMaxH);
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          const int srcW = bitmap.getWidth();
          const int srcH = bitmap.getHeight();
          const float fitScale = std::min(static_cast<float>(kCenterCoverMaxW) / static_cast<float>(srcW),
                                          static_cast<float>(kCenterCoverMaxH) / static_cast<float>(srcH));
          const int drawWidth = std::min(kCenterCoverMaxW, static_cast<int>(std::round(srcW * fitScale)));
          const int drawHeight = std::min(kCenterCoverMaxH, static_cast<int>(std::round(srcH * fitScale)));
          const int drawX = centerX + (kCenterCoverMaxW - drawWidth) / 2;
          const int drawY = centerTileY + (kCenterCoverMaxH - drawHeight) / 2;

          outRect = Rect{drawX, drawY, drawWidth, drawHeight};
          renderer.fillRect(outRect.x - kCenterOutlineW, outRect.y - kCenterOutlineW,
                            outRect.width + 2 * kCenterOutlineW, outRect.height + 2 * kCenterOutlineW, false);
          renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
          renderer.maskRoundedRectOutsideCorners(drawX, drawY, drawWidth, drawHeight, kCornerRadius, Color::White);
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

  auto drawSideCover = [&](int bookIdx, int x, bool isLeft) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    const int leftHeight = isLeft ? kSideInnerH : kSideOuterH;
    const int rightHeight = isLeft ? kSideOuterH : kSideInnerH;

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kSideCoverMaxW, kSideCoverMaxH);
      FsFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.fillRect(x, sideTileY, kSidePerspectiveW, std::max(leftHeight, rightHeight), false);
          renderer.drawPerspectiveBitmap(bitmap, x, sideTileY, kSidePerspectiveW, leftHeight, rightHeight);
          file.close();
          drawPerspectiveOutline(renderer, x, sideTileY, kSidePerspectiveW, leftHeight, rightHeight);
          return true;
        }
        file.close();
      }
    }

    fillPerspectiveSilhouette(renderer, x, sideTileY, kSidePerspectiveW, leftHeight, rightHeight);
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

    if (bookCount >= 5) drawSideCover(leftFarIdx, leftFarX, true);
    if (bookCount >= 4) drawSideCover(rightFarIdx, rightFarX, false);
    if (bookCount >= 2) drawSideCover(leftNearIdx, leftNearX, true);
    if (bookCount >= 3) drawSideCover(rightNearIdx, rightNearX, false);

    Rect centerCoverRect{};
    drawCenterCover(centerIdx, centerCoverRect);
    lastCenterCoverRect = centerCoverRect;
    if (centerIdx >= 0 && centerIdx < LyraCarouselMetrics::values.homeRecentBooksCount) {
      cachedCenterCoverRects[centerIdx] = centerCoverRect;
    }

    // Dots — centred over the cover tile, count = actual book count
    const int dotsY = centerCoverRect.y + centerCoverRect.height + 8;
    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerX + (kCenterCoverMaxW - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx)
        renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      else
        renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      dotX += kDotSize + kDotGap;
    }

    // Author then title below dots
    const int authorY = dotsY + kDotSize + 6;
    const std::string authorTrunc =
        renderer.truncatedText(kTitleFontId, recentBooks[centerIdx].author.c_str(), kCenterCoverMaxW);
    const int authorW = renderer.getTextWidth(kTitleFontId, authorTrunc.c_str());
    renderer.drawText(kTitleFontId, centerX + (kCenterCoverMaxW - authorW) / 2, authorY, authorTrunc.c_str(), true);

    const int titleY = authorY + renderer.getLineHeight(kTitleFontId) + 2;
    const std::string titleTrunc =
        renderer.truncatedText(kTitleFontId, recentBooks[centerIdx].title.c_str(), kCenterCoverMaxW);
    const int titleW = renderer.getTextWidth(kTitleFontId, titleTrunc.c_str());
    renderer.drawText(kTitleFontId, centerX + (kCenterCoverMaxW - titleW) / 2, titleY, titleTrunc.c_str(), true);

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  } else if (lastCenterCoverRect.width <= 0 || lastCenterCoverRect.height <= 0) {
    lastCenterCoverRect = Rect{centerX, centerTileY, kCenterCoverMaxW, kCenterCoverMaxH};
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
  (void)buttonLabel;

  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int tileW = renderer.getScreenWidth() / buttonCount;
  // Anchor row just above button hints, ignoring rect.y which may be off-screen
  // for large cover tiles
  const int rowY = renderer.getScreenHeight() - kButtonHintsH - tileH;

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
        const uint8_t* bmp = iconBitmapFor(icon);
        if (bmp != nullptr) {
          if (selected)
            renderer.drawIconInverted(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          else
            renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
        }
      }
    }
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
  (void)isHeader;
  constexpr int hPad = 8;
  constexpr int listIconSz = 24;
  constexpr int mainMenuIconSz = 32;
  constexpr int maxValWidth = 200;
  constexpr int cornerRadius = 6;

  const int rowHeight = (rowSubtitle != nullptr) ? LyraCarouselMetrics::values.listWithSubtitleRowHeight
                                                 : LyraCarouselMetrics::values.listRowHeight;
  const int pageItems = rect.height / rowHeight;
  if (pageItems <= 0 || itemCount <= 0) return;
  const int totalPages = (itemCount + pageItems - 1) / pageItems;

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

  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraCarouselMetrics::values.scrollBarWidth + LyraCarouselMetrics::values.scrollBarRightOffset)
                      : 1);

  // Solid black highlight bar
  if (selectedIndex >= 0) {
    renderer.fillRoundedRect(
        rect.x + LyraCarouselMetrics::values.contentSidePadding, rect.y + selectedIndex % pageItems * rowHeight,
        contentWidth - LyraCarouselMetrics::values.contentSidePadding * 2, rowHeight, kCornerRadius, Color::Black);
  }

  int textX = rect.x + LyraCarouselMetrics::values.contentSidePadding + hPad;
  int textWidth = contentWidth - LyraCarouselMetrics::values.contentSidePadding * 2 - hPad * 2;
  int iconSize = 0;
  if (rowIcon != nullptr) {
    iconSize = (rowSubtitle != nullptr) ? mainMenuIconSz : listIconSz;
    textX += iconSize + hPad;
    textWidth -= iconSize + hPad;
  }

  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  const int iconY = (rowSubtitle != nullptr) ? 16 : 10;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    const bool sel = (i == selectedIndex);
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
