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
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int centerCoverWidth = 220;
constexpr int centerCoverHeight = 320;
constexpr int sideCoverWidth = 66;     // 30% of center
constexpr int sideInnerHeight = 288;   // 90% of center — taller edge (toward middle)
constexpr int sideOuterHeight = 256;   // 80% of center — shorter edge (away from middle)
constexpr int bookCornerRadius = 6;

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
                                        const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                        const BookReadingStats* /*stats*/, float /*progressPercent*/) const {
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
  // drop the selection border and pin the center to book 0.
  const bool hasSelection = (selectorIndex >= 0 && selectorIndex < count);
  const int curIdx = hasSelection ? selectorIndex : 0;

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
    // Verticals are thicker than the 2px slants so the junctions read as
    // properly-balanced corners — slanted drawLine stair-steps thinner than
    // its nominal width, so a heavier vertical compensates.
    renderer.drawLine(drawX, drawY + topL, drawX, drawY + botL, 4, true);     // left edge (vertical, height hL)
    renderer.drawLine(rightX, drawY + topR, rightX, drawY + botR, 4, true);   // right edge (vertical, height hR)
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
}
