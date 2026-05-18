#include "LyraFlowTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/chart.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
constexpr int centerCoverWidth = 220;
constexpr int centerCoverHeight = 320;
constexpr int sideCoverWidth = 66;     // 30% of center
constexpr int sideInnerHeight = 288;   // 90% of center — taller edge (toward middle)
constexpr int sideOuterHeight = 256;   // 80% of center — shorter edge (away from middle)
constexpr int bookCornerRadius = 6;

// Menu visuals — kept in sync with LyraTheme's anonymous-namespace constants
// so the Flow override looks identical to the parent's button menu.
constexpr int menuTileCornerRadius = 6;
constexpr int menuTilePadding = 8;
constexpr int menuIconSize = 32;

// Same lookup as LyraTheme's iconForName(icon, 32). Duplicated here because
// that helper is file-local to LyraTheme.cpp.
const uint8_t* lyraFlowMenuIcon(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Chart:
      return ChartIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    default:
      return nullptr;
  }
}

// Erase pixels outside a rounded-corner mask so a rectangular bitmap blit
// looks like a rounded book cover. Same trick as the reference FlowTheme.
void cutRoundedCorners(GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  const int rSq = r * r;
  for (int dy = 0; dy < r; dy++) {
    for (int dx = 0; dx < r; dx++) {
      const int distSq = (r - dx) * (r - dx) + (r - dy) * (r - dy);
      if (distSq > rSq) {
        renderer.drawPixel(x + dx, y + dy, false);
        renderer.drawPixel(x + w - 1 - dx, y + dy, false);
        renderer.drawPixel(x + w - 1 - dx, y + h - 1 - dy, false);
        renderer.drawPixel(x + dx, y + h - 1 - dy, false);
      }
    }
  }
}
}  // namespace

void LyraFlowTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, const std::function<bool()>& storeCoverBuffer,
                                        const BookReadingStats* stats, float /*progressPercent*/) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int centerY = rect.y + 40;
  const int centerX = pageWidth / 2;
  const int count = static_cast<int>(recentBooks.size());

  // selectorIndex >= count means HomeActivity has navigated past the books and
  // is highlighting a menu item; in that case we keep the carousel visible but
  // drop the selection border. HomeActivity may encode the preferred center as
  // (count + lastBookIndex) so the carousel keeps the user's place when they
  // pop into the menu. Decode if in range, otherwise fall back to book 0.
  const bool hasSelection = (selectorIndex >= 0 && selectorIndex < count);
  int curIdx = 0;
  if (hasSelection) {
    curIdx = selectorIndex;
  } else {
    const int decoded = selectorIndex - count;
    if (decoded >= 0 && decoded < count) curIdx = decoded;
  }

  // The carousel chrome (header date, footer hints, etc.) is drawn by
  // HomeActivity, not by us. We have nothing static to cache here, so just
  // honor the buffer-snapshot protocol the same way the other themes do.
  if (bufferRestored) {
    coverRendered = true;
    coverBufferStored = true;
  } else {
    coverRendered = true;
    coverBufferStored = storeCoverBuffer();
  }

  // --- Side covers (perspective-projected, drawn outside-in so the center
  //     can land cleanly on top of any near-book overlap) ---
  auto drawStackedCover = [&](int idx, bool isLeft, bool isFar) {
    const int hL = isLeft ? sideInnerHeight : sideOuterHeight;
    const int hR = isLeft ? sideOuterHeight : sideInnerHeight;
    const int hMax = std::max(hL, hR);
    const int drawX = isLeft ? (isFar ? 30 : 80) : (isFar ? 385 : 335);
    const int drawY = centerY + (centerCoverHeight / 2) - (hMax / 2);

    const std::string coverPath = UITheme::getCoverThumbPath(recentBooks[idx].coverBmpPath, centerCoverHeight);
    bool drawn = false;
    if (!coverPath.empty()) {
      FsFile file;
      if (Storage.openFileForRead("HOME", coverPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // drawPerspectiveBitmap is OR-style (only writes black), so any
          // white area of the cover would show through to whatever side
          // cover was drawn beneath us. Pre-clear the bbox to opaque white.
          renderer.fillRect(drawX, drawY, sideCoverWidth, hMax, false);
          renderer.drawPerspectiveBitmap(bitmap, drawX, drawY, sideCoverWidth, hL, hR);
          drawn = true;
        }
        file.close();
      }
    }
    if (!drawn) {
      // Solid-black placeholder silhouette so the carousel still has shape.
      renderer.fillRect(drawX, drawY, sideCoverWidth, hMax, true);
      return;  // outline would be invisible against solid black anyway
    }
    // 2px trapezoidal outline matching the perspective shape — keeps every
    // side book visibly framed so the center book reads as part of a row of
    // books, not a single floating cover. The trapezoid is column-centered
    // vertically inside the (sideCoverWidth × hMax) bbox.
    const int topL = (hMax - hL) / 2;
    const int topR = (hMax - hR) / 2;
    const int botL = topL + hL - 1;
    const int botR = topR + hR - 1;
    const int rightX = drawX + sideCoverWidth - 1;
    renderer.drawLine(drawX, drawY + topL, rightX, drawY + topR, 2, true);    // top edge (slanted)
    renderer.drawLine(drawX, drawY + botL, rightX, drawY + botR, 2, true);    // bottom edge (slanted)
    // Verticals use fillRect, not drawLine — drawLine ignores its thickness
    // arg for purely vertical strokes (x1 == x2), so the previous 4 px width
    // was rendering as 1 px regardless. fillRect gives explicit control.
    constexpr int verticalEdgeWidth = 2;
    renderer.fillRect(drawX, drawY + topL, verticalEdgeWidth, hL, true);                    // left edge
    renderer.fillRect(rightX - verticalEdgeWidth + 1, drawY + topR, verticalEdgeWidth, hR,  // right edge
                      true);
    // The bottom slant's perpendicular thickness leaks pixels into the two
    // rows starting just below the bbox bottom (the row at drawY + hMax
    // is part of the visible outline, so we leave it). Wipe rows hMax+1
    // and hMax+2 to catch the hangnail wherever it lands.
    renderer.fillRect(drawX, drawY + hMax + 1, sideCoverWidth, 2, false);
  };

  const int idx2 = (curIdx + count - 1) % count;  // left-near
  const int idx3 = (curIdx + count - 2) % count;  // left-far
  const int idx4 = (curIdx + 1) % count;          // right-near
  const int idx5 = (curIdx + 2) % count;          // right-far

  if (count >= 5) drawStackedCover(idx3, true, true);
  if (count >= 4) drawStackedCover(idx5, false, true);
  if (count >= 2) drawStackedCover(idx2, true, false);
  if (count >= 3) drawStackedCover(idx4, false, false);

  // --- Center cover. Peek the bitmap dimensions first so the slot, outline,
  //     and selection border match the cover's true aspect ratio (otherwise
  //     drawBitmap aspect-fits but our 220×320 chrome leaves a white sliver
  //     for narrower covers, e.g. 1720×2600 which is taller than 220:320). ---
  int actualCoverWidth = centerCoverWidth;
  int actualCoverHeight = centerCoverHeight;
  const std::string cp = UITheme::getCoverThumbPath(recentBooks[curIdx].coverBmpPath, centerCoverHeight);
  FsFile cf;
  const bool centerOpened = !cp.empty() && Storage.openFileForRead("HOME", cp, cf);
  Bitmap centerBitmap(cf);
  bool centerParsed = false;
  if (centerOpened) {
    if (centerBitmap.parseHeaders() == BmpReaderError::Ok && centerBitmap.getWidth() > 0 &&
        centerBitmap.getHeight() > 0) {
      const int srcW = centerBitmap.getWidth();
      const int srcH = centerBitmap.getHeight();
      const float fitScale = std::min(static_cast<float>(centerCoverWidth) / static_cast<float>(srcW),
                                      static_cast<float>(centerCoverHeight) / static_cast<float>(srcH));
      actualCoverWidth = std::min(centerCoverWidth, static_cast<int>(std::round(srcW * fitScale)));
      actualCoverHeight = std::min(centerCoverHeight, static_cast<int>(std::round(srcH * fitScale)));
      centerParsed = true;
    }
  }

  const int cX = centerX - actualCoverWidth / 2;
  // Vertical-center within the original 320-tall slot in case a cover is wider
  // than tall (very rare in practice).
  const int actualY = centerY + (centerCoverHeight - actualCoverHeight) / 2;

  // Clear behind it so any side-cover overlap doesn't bleed through.
  renderer.fillRect(cX, actualY, actualCoverWidth, actualCoverHeight, false);

  if (centerParsed) {
    renderer.drawBitmap(centerBitmap, cX, actualY, actualCoverWidth, actualCoverHeight);
    cutRoundedCorners(renderer, cX, actualY, actualCoverWidth, actualCoverHeight, bookCornerRadius);
  } else {
    // Placeholder: black lower-2/3 with the cover icon, matches reference fallback.
    renderer.fillRoundedRect(cX, actualY + actualCoverHeight / 3, actualCoverWidth, 2 * actualCoverHeight / 3,
                             bookCornerRadius, false, false, true, true, Color::Black);
    renderer.drawIcon(CoverIcon, cX + actualCoverWidth / 2 - 16, actualY + actualCoverHeight / 2 - 16, 32, 32);
  }
  renderer.drawRoundedRect(cX, actualY, actualCoverWidth, actualCoverHeight, 2, bookCornerRadius, true);

  if (hasSelection) {
    renderer.drawRoundedRect(cX - 2, actualY - 2, actualCoverWidth + 4, actualCoverHeight + 4, 4,
                             bookCornerRadius + 2, true);
  }

  if (centerOpened) cf.close();

  // --- Title above the center cover (filename, no extension) ---
  std::string filename = recentBooks[curIdx].title.empty() ? recentBooks[curIdx].path : recentBooks[curIdx].title;
  if (recentBooks[curIdx].title.empty()) {
    const size_t lastSlash = filename.find_last_of('/');
    if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
    const size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos && lastDot > 0) filename = filename.substr(0, lastDot);
  }

  const std::string truncatedTitle =
      renderer.truncatedText(UI_12_FONT_ID, filename.c_str(), pageWidth - 40, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, truncatedTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, centerX - titleWidth / 2, rect.y - 5, truncatedTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  // --- Per-book reading time, centered below the center cover. Mirrors the
  //     reference Lua FlowTheme: "Xh Ym" in SMALL_FONT, 8 px under the cover.
  //     Always rendered (even "0h 0m") so the layout is stable whether the
  //     user has read the book or not. ---
  char timeStr[16];
  const uint32_t bookSeconds = (stats != nullptr) ? stats->totalReadingSeconds : 0;
  const uint32_t hours = bookSeconds / 3600;
  const uint32_t minutes = (bookSeconds % 3600) / 60;
  snprintf(timeStr, sizeof(timeStr), "%uh %um", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
  const int timeWidth = renderer.getTextWidth(SMALL_FONT_ID, timeStr);
  const int timeY = centerY + centerCoverHeight + 8;
  renderer.drawText(SMALL_FONT_ID, centerX - timeWidth / 2, timeY, timeStr, true);
}

void LyraFlowTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  const auto& menuMetrics = UITheme::getInstance().getMetrics();
  const int rowStep = menuMetrics.menuRowHeight + menuMetrics.menuSpacing;
  // Reserve a thin strip at the bottom for page-indicator dots. Reserving
  // unconditionally keeps tile geometry stable whether dots are visible or not.
  constexpr int dotSize = 10;
  constexpr int dotSpacing = 8;
  constexpr int dotStripHeight = 18;
  const int usableHeight = std::max(0, rect.height - dotStripHeight);
  const int pageItems = std::max(1, usableHeight / rowStep);
  const int safeSelectedIndex = std::max(0, selectedIndex);

  // Two-anchor pagination: page 1 = items [0..pageItems), page 2 =
  // last `pageItems` items. The pages overlap; we resolve which one
  // to render via a sticky bit so the cursor "pulls" the visible
  // window with it asymmetrically — page 2 holds while the cursor
  // scrolls up through the overlap, and only flips back to page 1
  // when the cursor crosses page 2's top boundary.
  const bool needsPaging = buttonCount > pageItems;
  const int page2StartIndex = std::max(0, buttonCount - pageItems);
  if (!needsPaging) {
    stickyMenuPage2 = false;
  } else if (safeSelectedIndex >= pageItems) {
    // Cursor is in page 2's exclusive zone — we're definitely on page 2.
    stickyMenuPage2 = true;
  } else if (safeSelectedIndex < page2StartIndex) {
    // Cursor crossed page 2's top boundary going up — back to page 1.
    stickyMenuPage2 = false;
  }
  // Else: cursor in the overlap zone, keep whichever page we were on.
  const bool onPage2 = needsPaging && stickyMenuPage2;
  const int pageStartIndex = onPage2 ? page2StartIndex : 0;
  const int totalPages = needsPaging ? 2 : 1;
  const int currentPage = onPage2 ? 1 : 0;

  for (int i = pageStartIndex; i < buttonCount && i < pageStartIndex + pageItems; ++i) {
    const int displayIndex = i - pageStartIndex;
    const int tileWidth = rect.width - menuMetrics.contentSidePadding * 2;
    const Rect tileRect{rect.x + menuMetrics.contentSidePadding, rect.y + displayIndex * rowStep, tileWidth,
                        menuMetrics.menuRowHeight};

    const bool selected = (i == selectedIndex);
    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, menuTileCornerRadius,
                               Color::LightGray);
    }

    const std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (menuMetrics.menuRowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::BookmarkIcon) {
        // Match the status-bar bookmark ribbon shape (LyraTheme parity).
        const int ribbonWidth = 16;
        const int ribbonHeight = 22;
        const int notchSize = 6;
        const int iconX = textX + (menuIconSize - ribbonWidth) / 2;
        const int iconY = textY + 4;
        const int centerX = iconX + ribbonWidth / 2;
        const int polyX[5] = {iconX, iconX + ribbonWidth, iconX + ribbonWidth, centerX, iconX};
        const int polyY[5] = {iconY, iconY, iconY + ribbonHeight, iconY + ribbonHeight - notchSize,
                              iconY + ribbonHeight};
        renderer.fillPolygon(polyX, polyY, 5, true);
        textX += menuIconSize + menuTilePadding + 2;
      } else {
        const uint8_t* iconBitmap = lyraFlowMenuIcon(icon);
        if (iconBitmap != nullptr) {
          renderer.drawIcon(iconBitmap, textX, textY + 3, menuIconSize, menuIconSize);
          textX += menuIconSize + menuTilePadding + 2;
        }
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }

  // Page-indicator dots — pattern lifted from RecentBooksGridActivity::render.
  // Anchor at the same vertical offset above the button hints as Recent Books
  // does (rect.y + rect.height == pageHeight - buttonHintsHeight for the home
  // menu rect, so this formula resolves to the same Y as Recent Books's
  // pageHeight - buttonHintsHeight - verticalSpacing - 4).
  if (totalPages > 1) {
    const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
    const int dotsStartX = rect.x + (rect.width - totalDotWidth) / 2;
    const int dotY = rect.y + rect.height - menuMetrics.verticalSpacing - 4;
    constexpr int dotRadius = dotSize / 2;  // 5 → fully-circular bullet on 10x10
    for (int p = 0; p < totalPages; ++p) {
      const int dx = dotsStartX + p * (dotSize + dotSpacing);
      if (p == currentPage) {
        renderer.fillRoundedRect(dx, dotY, dotSize, dotSize, dotRadius, Color::Black);
      } else {
        renderer.drawRoundedRect(dx, dotY, dotSize, dotSize, 1, dotRadius, true);
      }
    }
  }
}
