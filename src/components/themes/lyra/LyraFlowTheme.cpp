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
                                        const BookReadingStats* stats, float progressPercent) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  // CrumBLE: cover starts 58 px below the carousel rect's top. Half-way
  // between the original 52 (before the title + author block went above)
  // and the previous 64 (which left too much air between author and
  // cover). Author bottom sits ~rect.y + 22; cover top at rect.y + 58
  // gives ~36 px of breathing room.
  const int centerY = rect.y + 58;
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
    // CrumBLE: tightened from (30/80 left, 385/335 right) -- pulls the
    // side covers ~16 px closer toward the center book on each side so
    // the adjacent books read as a clustered "row of books" rather than
    // floating off near the screen edges. Near covers move inward more
    // than far so the perspective stagger stays balanced.
    const int drawX = isLeft ? (isFar ? 46 : 96) : (isFar ? 368 : 318);
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

  // Variables the footer (progress bar) and cover chrome depend on. We
  // peek the CENTER cover's true dimensions on EVERY render — even in the
  // skipCarouselCoverLoads fast path — so the footer width stays stable.
  // If we left these at the default 220 while skipping but recomputed them
  // narrower on the next full render, the progress bar would visibly start
  // wider then shrink when navigating (it tracks actualCoverWidth below).
  // parseHeaders() only reads the small BMP header; the expensive work
  // (scaling + drawing all 5 covers) stays gated behind the skip flag.
  int actualCoverWidth = centerCoverWidth;
  int actualCoverHeight = centerCoverHeight;

  // --- Center cover. Peek the bitmap dimensions first so the slot, outline,
  //     selection border, AND footer/progress-bar width match the cover's
  //     true aspect ratio (otherwise drawBitmap aspect-fits but our 220×320
  //     chrome leaves a white sliver for narrower covers, e.g. 1720×2600
  //     which is taller than 220:320). ---
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

  int cX = centerX - actualCoverWidth / 2;
  int actualY = centerY + (centerCoverHeight - actualCoverHeight) / 2;

  // Skip the expensive 5-BMP cover paint when HomeActivity tells us the
  // framebuffer was just restored with these same covers. Saves ~80% of
  // drawRecentBookCover's cost on every "L/R on shelf/menu" type input
  // where the carousel doesn't visually change. The center-cover header
  // peek above still runs so the footer geometry stays consistent.
  if (!skipCarouselCoverLoads) {
    if (count >= 5) drawStackedCover(idx3, true, true);
    if (count >= 4) drawStackedCover(idx5, false, true);
    if (count >= 2) drawStackedCover(idx2, true, false);
    if (count >= 3) drawStackedCover(idx4, false, false);

    // Clear the MAX possible center-cover slot, not just the current
    // cover's bbox. When the previous render was a wider book, its
    // selection border (+1 px outside the cover) extended past the
    // current cover's edges; the narrow per-cover clear left those
    // outer pixels intact, producing the "ghost hooks" effect at
    // each corner. A fixed-size slot clear sized to the maximum
    // possible cover + 3 px of selection-border safety wipes every
    // previous frame's border pixels regardless of dimensions.
    constexpr int kSelBorderPad = 3;
    const int slotW = centerCoverWidth + 2 * kSelBorderPad;
    const int slotH = centerCoverHeight + 2 * kSelBorderPad;
    const int slotX = centerX - slotW / 2;
    const int slotY = centerY - kSelBorderPad;
    renderer.fillRect(slotX, slotY, slotW, slotH, false);

    if (centerParsed) {
      renderer.drawBitmap(centerBitmap, cX, actualY, actualCoverWidth, actualCoverHeight);
      cutRoundedCorners(renderer, cX, actualY, actualCoverWidth, actualCoverHeight, bookCornerRadius);
    } else {
    // Placeholder: black lower-2/3 with the cover icon centered just below
    // the divider, then the wrapped book title in white below the icon.
    // Mirrors LyraCarouselTheme's fallback so un-thumbnailed books still
    // look like intentional cards instead of half-blank slots.
    renderer.fillRoundedRect(cX, actualY + actualCoverHeight / 3, actualCoverWidth, 2 * actualCoverHeight / 3,
                             bookCornerRadius, false, false, true, true, Color::Black);

    constexpr int kFallbackTitlePadX = 14;
    constexpr int kFallbackTitlePadBottom = 14;
    constexpr int kFallbackIconGap = 10;
    const int iconX = cX + actualCoverWidth / 2 - 16;
    const int iconY = actualY + actualCoverHeight / 3 + 14;
    renderer.drawIcon(CoverIcon, iconX, iconY, 32, 32);

    const int titleY = iconY + 32 + kFallbackIconGap;
    const int titleW = actualCoverWidth - kFallbackTitlePadX * 2;
    const int titleH = actualY + actualCoverHeight - kFallbackTitlePadBottom - titleY;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int maxLines = std::clamp(titleH / std::max(1, lineHeight), 1, 4);
    // Fall back to the filename (sans extension) when the EPUB had no title
    // metadata — same logic the Flow theme uses for the heading above.
    std::string title = recentBooks[curIdx].title;
    if (title.empty()) {
      title = recentBooks[curIdx].path;
      const size_t lastSlash = title.find_last_of('/');
      if (lastSlash != std::string::npos) title = title.substr(lastSlash + 1);
      const size_t lastDot = title.find_last_of('.');
      if (lastDot != std::string::npos && lastDot > 0) title = title.substr(0, lastDot);
    }
    const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, title.c_str(), titleW, maxLines, EpdFontFamily::BOLD);
    const int blockH = lineHeight * static_cast<int>(titleLines.size());
    int lineY = titleY + std::max(0, (titleH - blockH) / 2);
    for (const auto& line : titleLines) {
      const int lineW = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      // White text on the black backdrop — drawText's `black` param is the
      // pixel state to write, so `false` puts down white pixels.
      renderer.drawText(UI_10_FONT_ID, cX + (actualCoverWidth - lineW) / 2, lineY, line.c_str(), false,
                        EpdFontFamily::BOLD);
      lineY += lineHeight;
    }
  }
    // Inner cover outline removed — cutRoundedCorners has already
    // shaped the bitmap's corners to white, and the selection border
    // (below, when hasSelection) is sufficient framing for the
    // focused book. Dropping the always-on stroke eliminates the
    // last visible source of corner-hook artifacts.

    if (hasSelection) {
      // Selection border: thinned from 4 px to 2 px and pulled in
      // (offset 1 px instead of 2 px). The 4 px-stroke + 8 px radius
      // combo was the dominant source of the "corner hook" artifact —
      // each corner's arc occupied ~6 px of stroke width which read
      // as a bracket at panel resolution. A 2 px outline at radius 7
      // looks like a single intentional frame.
      renderer.drawRoundedRect(cX - 1, actualY - 1, actualCoverWidth + 2, actualCoverHeight + 2, 2,
                               bookCornerRadius + 1, true);
    }

  }  // end of if (!skipCarouselCoverLoads)

  // Close the center-cover file opened above (outside the skip block now,
  // since the header peek runs on every render).
  if (centerOpened) cf.close();

  // One-shot reset so the next render starts from a known default.
  skipCarouselCoverLoads = false;

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
  // Pre-clear the title + author strip. HomeActivity now snapshots the
  // framebuffer at end-of-render and restores it at the start of the next
  // render (lets the shelf paint be skipped when its state is unchanged).
  // The restored buffer has the PRIOR render's title/author in it — if the
  // new title is shorter or differently-positioned, the leftover characters
  // would show through.
  constexpr int kAuthorFontId = UI_10_FONT_ID;
  constexpr int kTitleAuthorGap = 2;
  // Title sits at rect.y - 12 (was -5): pulls the title + author block UP
  // ~7 px so there's a clearer visual gap between the author line bottom
  // and the cover's top edge. Cover correspondingly drops to rect.y + 64
  // (above), keeping both ends of the gap comfortable.
  constexpr int kTitleTopY = -12;
  const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int authorLineH = renderer.getLineHeight(kAuthorFontId);
  const int titleAuthorStripHeight = titleLineH + kTitleAuthorGap + authorLineH + 4;
  renderer.fillRect(0, rect.y + kTitleTopY, pageWidth, titleAuthorStripHeight, false);
  renderer.drawText(UI_12_FONT_ID, centerX - titleWidth / 2, rect.y + kTitleTopY, truncatedTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  // --- Author line directly under the title (above the cover). ---
  const std::string& authorRaw = recentBooks[curIdx].author;
  if (!authorRaw.empty()) {
    const std::string truncatedAuthor =
        renderer.truncatedText(kAuthorFontId, authorRaw.c_str(), pageWidth - 40, EpdFontFamily::REGULAR);
    const int aw = renderer.getTextWidth(kAuthorFontId, truncatedAuthor.c_str(), EpdFontFamily::REGULAR);
    const int authorY = rect.y + kTitleTopY + titleLineH + kTitleAuthorGap;
    renderer.drawText(kAuthorFontId, centerX - aw / 2, authorY, truncatedAuthor.c_str(), true,
                      EpdFontFamily::REGULAR);
  }

  // --- Reading-progress footer below the center cover. Modelled on the
  //     LyraCarousel footer: compact elapsed-time label on the top-left,
  //     a 5-px dithered-track progress bar across the cover width, and a
  //     right-aligned percentage below the bar. ---
  constexpr int kFooterFontId = UI_10_FONT_ID;
  constexpr int kFooterTopGap = 8;
  constexpr int kFooterProgressBarHeight = 5;
  constexpr int kFooterBarToLabelGap = 2;      // gap between bar and time/percent row

  const int footerWidth = actualCoverWidth;
  const int footerX = cX;
  int infoY = centerY + centerCoverHeight + kFooterTopGap;

  const bool hasStats = (stats != nullptr && stats->sessionCount > 0);
  const bool hasProgress = progressPercent >= 0.0f;

  // Pre-clear the footer region (bar + label row beneath it). Clears
  // the FULL page width — not just the current cover's width — because
  // adjacent books in the carousel can have different aspect ratios.
  // If the previous render's cover was wider, the previous footer's
  // right-aligned percent text extended past the current footerWidth's
  // right edge, leaving trailing glyphs (e.g. "0%‰" instead of "0%"
  // when prior was "37%"). Clearing pageWidth costs ~5KB more pixels
  // per render but eliminates the class of bug entirely.
  const int footerStripHeight = kFooterProgressBarHeight + kFooterBarToLabelGap + renderer.getLineHeight(kFooterFontId) + 4;
  renderer.fillRect(0, infoY, renderer.getScreenWidth(), footerStripHeight, false);

  // New stacking order (per UX feedback): bar on top, then a single row with
  // elapsed-time on the left and percentage on the right beneath it. This
  // keeps the numeric data clustered visually rather than splitting time
  // above the bar and percent below.
  if (hasProgress) {
    const float clampedProgress = std::clamp(progressPercent, 0.0f, 100.0f);
    const int filledWidth = std::clamp(static_cast<int>((clampedProgress / 100.0f) * footerWidth), 0, footerWidth);
    renderer.fillRectDither(footerX, infoY, footerWidth, kFooterProgressBarHeight, Color::LightGray);
    if (filledWidth > 0) {
      renderer.fillRect(footerX, infoY, filledWidth, kFooterProgressBarHeight, true);
    }

    const int labelRowY = infoY + kFooterProgressBarHeight + kFooterBarToLabelGap;
    if (hasStats) {
      char buf[24];
      const uint32_t seconds = stats->totalReadingSeconds;
      if (seconds < 60) {
        snprintf(buf, sizeof(buf), "<1m");
      } else if (seconds < 3600) {
        snprintf(buf, sizeof(buf), "%um", static_cast<unsigned>(seconds / 60));
      } else {
        snprintf(buf, sizeof(buf), "%uh %um", static_cast<unsigned>(seconds / 3600),
                 static_cast<unsigned>((seconds % 3600) / 60));
      }
      renderer.drawText(kFooterFontId, footerX, labelRowY, buf, true, EpdFontFamily::REGULAR);
    }

    char percentBuf[8];
    snprintf(percentBuf, sizeof(percentBuf), "%.0f%%", clampedProgress);
    const int percentW = renderer.getTextWidth(kFooterFontId, percentBuf, EpdFontFamily::REGULAR);
    renderer.drawText(kFooterFontId, footerX + footerWidth - percentW, labelRowY, percentBuf, true,
                      EpdFontFamily::REGULAR);
  } else if (hasStats) {
    // Edge case: stats exist but progress doesn't (e.g. book just opened
    // for the first time, no last-read marker yet). Fall back to time-only
    // on a single line, where the bar would have sat.
    char buf[24];
    const uint32_t seconds = stats->totalReadingSeconds;
    if (seconds < 60) {
      snprintf(buf, sizeof(buf), "<1m");
    } else if (seconds < 3600) {
      snprintf(buf, sizeof(buf), "%um", static_cast<unsigned>(seconds / 60));
    } else {
      snprintf(buf, sizeof(buf), "%uh %um", static_cast<unsigned>(seconds / 3600),
               static_cast<unsigned>((seconds % 3600) / 60));
    }
    renderer.drawText(kFooterFontId, footerX, infoY, buf, true, EpdFontFamily::REGULAR);
  }
}

void LyraFlowTheme::drawBookshelfStrip(GfxRenderer& renderer, Rect rect, const char* collectionName,
                                       const std::vector<std::string>& coverPaths, int selectedSpineIndex,
                                       int scrollOffset, bool headerFocused, bool hasMultipleCollections,
                                       const char* focusedBookTitle,
                                       const std::vector<int>* seriesMemberCounts, const char* focusedBookAuthor) const {
  // Vertical layout (top → bottom):
  //   [focused book title]  — drawn ABOVE rect.y so the rest of the layout
  //                           doesn't shift when focused vs unfocused
  //   [collection tab]      — collection name centered, bold when focused
  //   [thumbnail row]       — fixed-cell-size cover thumbnails, aspect-fill
  //                           with crop (same pattern as Recent Books grid)
  //
  // Cell aspect is ~2:3 (book-cover proportion) so a row of thumbnails
  // reads as a row of actual books standing up rather than abstract bars.
  // Tab is the "Favorites" / collection-name heading rendered above the
  // book row. kTabBottomGap is the vertical padding between the heading
  // baseline and the top of the cover row — user feedback iter 4 asked
  // for more breathing room there so the heading doesn't crowd the row.
  constexpr int kTabHeight = 18;
  constexpr int kTabBottomGap = 12;
  // 4 thumbs across at 480px wide: 2*16 (side pad) + 4*100 (cells) + 3*16
  // (gaps) = 480 exactly. Dropped from 5 cells @ 84x126 to 4 cells @
  // 100x150 to fill the empty band below the carousel that the smaller
  // cells left behind. Aspect still ~2:3 book-cover; renderer can blit
  // 1:1 because loadShelfCovers caches BMPs at exactly this size.
  constexpr int kCellWidth = 100;
  constexpr int kCellHeight = 150;
  constexpr int kSidePad = 16;
  constexpr int kCellGap = 16;

  const int bookCount = static_cast<int>(coverPaths.size());

  // Pre-clear the entire shelf zone (tab + cell row + shadow strip below
  // + focused-title strip below that). HomeActivity's end-of-render
  // snapshot means the prior frame's pixels (different focus ring,
  // different title text, wider arrows) would otherwise show through.
  // Extends `rect.height` by 40 px to wipe the shadow band (6 px) PLUS
  // the focused-title strip below it (one full UI_10 line height) with
  // generous padding so descenders/ascenders never leak past.
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height + 56, false);

  const int tabY = rect.y;
  // Tab font is one step up from the row's prior 8px (SMALL_FONT_ID) to
  // 10px (UI_10_FONT_ID) so the collection name is readable from a glance
  // without dominating the strip vs. the cover thumbs.
  constexpr int kTabFontId = UI_10_FONT_ID;
  if (collectionName != nullptr && *collectionName != '\0') {
    // Bold + flanking left/right arrow glyphs when the header itself is
    // the focus, so the user has an obvious visual cue that L/R cycles
    // collections (and not, say, books within a row).
    const auto style = headerFocused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int nw = renderer.getTextWidth(kTabFontId, collectionName, style);
    const int nameX = rect.x + (rect.width - nw) / 2;
    renderer.drawText(kTabFontId, nameX, tabY, collectionName, true, style);

    if (headerFocused && hasMultipleCollections) {
      // Triangle hints flanking the name. Drawn as filled polygons so
      // they read as clear arrowheads at 10px font scale. Vertically
      // centered on the cap-height of the name.
      const int lineH = renderer.getLineHeight(kTabFontId);
      const int cy = tabY + lineH / 2;
      // Bumped from 4 → 7 px. The 4-px triangles read as decoration
      // more than navigation hints; 7 px is large enough that the
      // user reads them as "press L/R" at a glance.
      constexpr int kArrowSize = 7;
      constexpr int kArrowGap = 8;
      // Left ◀
      const int lTip = nameX - kArrowGap - kArrowSize;
      const int lBase = nameX - kArrowGap;
      const int lXs[3] = {lTip, lBase, lBase};
      const int lYs[3] = {cy, cy - kArrowSize, cy + kArrowSize};
      renderer.fillPolygon(lXs, lYs, 3, true);
      // Right ▶
      const int rTip = nameX + nw + kArrowGap + kArrowSize;
      const int rBase = nameX + nw + kArrowGap;
      const int rXs[3] = {rTip, rBase, rBase};
      const int rYs[3] = {cy, cy - kArrowSize, cy + kArrowSize};
      renderer.fillPolygon(rXs, rYs, 3, true);
    }
  }

  if (bookCount <= 0) return;

  const int availW = rect.width - 2 * kSidePad;
  const int cellTotalW = kCellWidth + kCellGap;
  const int visibleCells = std::max(1, (availW + kCellGap) / cellTotalW);
  const int shelfRowY = tabY + kTabHeight + kTabBottomGap;
  const int actualDrawn = std::min(visibleCells, bookCount - scrollOffset);
  const int rowDrawW = actualDrawn * kCellWidth + (actualDrawn - 1) * kCellGap;
  const int rowStartX = rect.x + (rect.width - rowDrawW) / 2;

  // Page-stack depth cue: draw two dithered light-gray strips only on the
  // right and bottom of each cell (not behind the cover — that part was
  // wasted ink in the previous L-shape implementation). kShadowInset
  // pulls the strips in from the top-right and bottom-left corners so
  // the protrusion fades at the corners — same as how page edges aren't
  // visible at the corners of a real bound book.
  constexpr int kShadowDepth = 6;   // how far the page stack protrudes (px)
  constexpr int kShadowInset = 3;   // corner inset along the cover edge

  // Width of the dark spine glyph drawn LEFT of a series cell's cover.
  // The cover itself stays kCellWidth wide; the spine adds visual mass
  // on the left to signal "this is multiple books bound together".
  constexpr int kSeriesSpineWidth = 6;

  for (int i = 0; i < actualDrawn; ++i) {
    const int spineIdx = scrollOffset + i;
    const int x = rowStartX + i * cellTotalW;
    const int y = shelfRowY;
    const bool isSeries = seriesMemberCounts != nullptr && spineIdx < static_cast<int>(seriesMemberCounts->size()) &&
                          (*seriesMemberCounts)[spineIdx] >= 2;

    // Series spine: a solid black bar to the left of the cover. Reads
    // as a stack of books leaning against the front cover.
    if (isSeries) {
      renderer.fillRect(x - kSeriesSpineWidth - 1, y, kSeriesSpineWidth, kCellHeight, true);
    }

    // Right edge: pages stacked along the fore-edge of the book.
    renderer.fillRectDither(x + kCellWidth, y + kShadowInset, kShadowDepth, kCellHeight - kShadowInset, Color::LightGray);
    // Bottom edge: pages along the tail of the book.
    renderer.fillRectDither(x + kShadowInset, y + kCellHeight, kCellWidth - kShadowInset, kShadowDepth, Color::LightGray);
    // Corner block where the two strips meet — fills the gap so the L
    // looks contiguous rather than missing its outer corner.
    renderer.fillRectDither(x + kCellWidth, y + kCellHeight, kShadowDepth, kShadowDepth, Color::LightGray);

    bool drewThumb = false;
    if (spineIdx < static_cast<int>(coverPaths.size()) && !coverPaths[spineIdx].empty()) {
      FsFile file;
      if (Storage.openFileForRead("LFT", coverPaths[spineIdx], file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          // Aspect-fill into the cell, cropping the longer axis. Same math
          // as RecentBooksGridActivity::loadPageCovers.
          const float srcW = static_cast<float>(bitmap.getWidth());
          const float srcH = static_cast<float>(bitmap.getHeight());
          const float srcRatio = srcW / srcH;
          const float targetRatio = static_cast<float>(kCellWidth) / static_cast<float>(kCellHeight);
          float cropX = 0.0f;
          float cropY = 0.0f;
          if (srcRatio > targetRatio) {
            cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
          } else if (srcRatio < targetRatio) {
            cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
          }
          renderer.drawBitmap(bitmap, x, y, kCellWidth, kCellHeight, cropX, cropY);
          drewThumb = true;
        }
        file.close();
      }
    }
    if (!drewThumb) {
      // Placeholder cell: thin outline + small book icon centered.
      renderer.drawRoundedRect(x, y, kCellWidth, kCellHeight, 1, 3, true);
      constexpr int kIconSize = 24;
      const int iconX = x + (kCellWidth - kIconSize) / 2;
      const int iconY = y + (kCellHeight - kIconSize) / 2;
      renderer.drawIcon(CoverIcon, iconX, iconY, kIconSize, kIconSize);
    }

    if (spineIdx == selectedSpineIndex) {
      // Focus ring outside the cell so the cover itself isn't covered.
      renderer.drawRoundedRect(x - 3, y - 3, kCellWidth + 6, kCellHeight + 6, 2, 5, true);
    }
  }

  if (bookCount > visibleCells) {
    const int triY = shelfRowY + kCellHeight / 2;
    constexpr int triSize = 4;
    if (scrollOffset > 0) {
      const int tx = rect.x + kSidePad / 2;
      const int triXs[3] = {tx + triSize, tx - 1, tx + triSize};
      const int triYs[3] = {triY - triSize, triY, triY + triSize};
      renderer.fillPolygon(triXs, triYs, 3, true);
    }
    if (scrollOffset + visibleCells < bookCount) {
      const int tx = rect.x + rect.width - kSidePad / 2;
      const int triXs[3] = {tx - triSize, tx + 1, tx - triSize};
      const int triYs[3] = {triY - triSize, triY, triY + triSize};
      renderer.fillPolygon(triXs, triYs, 3, true);
    }
  }

  // Focused book title, centered below the cell row. Same affordance as
  // the carousel showing the active book name under its cover. Anchored
  // a few px below the shadow's extent so it doesn't crowd the cells.
  // The strip rect is sized to JUST the tab + cells, so this drops into
  // the small breathing-room band between the strip and the icon bar.
  //
  // CrumBLE: dropped the author second-line from this function. The author
  // (when a book is focused) is now rendered into the icon bar's label
  // band by drawButtonMenu via focusedBookAuthorForLabel -- HomeActivity
  // sets that pointer when a shelf book is focused. The previous author
  // position here was being wiped by drawButtonMenu's pre-render clear,
  // making it invisible.
  if (selectedSpineIndex >= 0 && focusedBookTitle != nullptr && *focusedBookTitle != '\0') {
    constexpr int kTitleFontId = UI_10_FONT_ID;
    // Tightened from +3 to 0 (halfway between the original +3 and the
    // -3 we tried before): keeps the descender area (g, p, q, y) above
    // the icon-bar's pre-render fillRect band so the bottom row of
    // those letters renders intact, while leaving a small visible gap
    // between the cell-row shadow and the title.
    constexpr int kTitleTopGap = 0;
    const int titleY = shelfRowY + kCellHeight + kShadowDepth + kTitleTopGap;
    const auto truncated = renderer.truncatedText(kTitleFontId, focusedBookTitle, rect.width - 2 * kSidePad);
    const int tw = renderer.getTextWidth(kTitleFontId, truncated.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(kTitleFontId, rect.x + (rect.width - tw) / 2, titleY, truncated.c_str(), true,
                      EpdFontFamily::REGULAR);
  }
  (void)focusedBookAuthor;  // intentionally unused; consumed by drawButtonMenu via focusedBookAuthorForLabel
}

void LyraFlowTheme::drawButtonMenu(GfxRenderer& renderer, Rect /*rect*/, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  // Bottom-anchored horizontal icon bar, modelled on LyraCarouselTheme.
  // The selected icon gets a rounded black highlight and its label is
  // centered just above the row. The passed `rect` is ignored — the bar
  // always anchors to the screen bottom regardless of where the home
  // carousel placed its menu area.
  if (buttonCount <= 0) return;

  constexpr int kMenuIconSize = 32;
  constexpr int kMenuIconPad = 14;          // → tile height = 14+32+14 = 60
  constexpr int kHighlightPad = 7;          // ring of padding around selected icon
  constexpr int kHighlightCorner = 6;
  constexpr int kMenuLabelTopGap = 3;       // gap between label and icon row
  constexpr int kMenuLabelBottomGap = 4;    // gap below label baseline
  constexpr int kMenuRowDrop = 31;          // pushes the bar closer to the screen bottom

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int hintsH = LyraFlowMetrics::values.buttonHintsHeight;
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int tileW = screenW / buttonCount;
  const int labelLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int rowY = screenH - hintsH - tileH - kMenuLabelTopGap - labelLineHeight - kMenuLabelBottomGap + kMenuRowDrop;
  const int labelY = rowY - kMenuLabelTopGap - labelLineHeight;

  // Wipe the bar's vertical span so any prior render of the previous list
  // layout doesn't bleed through under the new tighter geometry.
  renderer.fillRect(0, labelY, screenW, screenH - hintsH - labelY, false);

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = i * tileW;
    const int iconX = tileX + (tileW - kMenuIconSize) / 2;
    const int iconY = rowY + kMenuIconPad;
    const bool selected = (selectedIndex == i);

    if (selected) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = rowY + (tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kHighlightCorner,
                               Color::Black);
    }

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::BookmarkIcon) {
        // Status-bar bookmark ribbon shape, drawn in the same slot as a
        // regular icon. Mirrors the Flow list version, just centered in
        // the tile instead of left-aligned.
        constexpr int ribbonWidth = 16;
        constexpr int ribbonHeight = 22;
        constexpr int notchSize = 6;
        const int ribbonX = iconX + (kMenuIconSize - ribbonWidth) / 2;
        const int ribbonY = iconY + (kMenuIconSize - ribbonHeight) / 2;
        const int centerX = ribbonX + ribbonWidth / 2;
        const int polyX[5] = {ribbonX, ribbonX + ribbonWidth, ribbonX + ribbonWidth, centerX, ribbonX};
        const int polyY[5] = {ribbonY, ribbonY, ribbonY + ribbonHeight, ribbonY + ribbonHeight - notchSize,
                              ribbonY + ribbonHeight};
        renderer.fillPolygon(polyX, polyY, 5, !selected);
      } else {
        const uint8_t* bmp = LyraTheme::iconForName(icon, kMenuIconSize);
        if (bmp != nullptr) {
          if (selected) {
            renderer.drawIconInverted(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          } else {
            renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize, kMenuIconSize);
          }
        }
      }
    }
  }

  // Centered label above the icon row. Dual-purpose:
  //   - When an icon is focused: shows that icon's name (existing behavior).
  //   - When no icon is focused BUT a Collections book is focused:
  //     HomeActivity has set focusedBookAuthorForLabel to that book's
  //     author; we use it as the label text. Same physical slot, so the
  //     user always sees ONE contextual label adjacent to the icon bar.
  //
  // One-shot: focusedBookAuthorForLabel is cleared at the end so a stale
  // value can't leak into a subsequent frame.
  std::string labelStr;
  if (selectedIndex >= 0 && selectedIndex < buttonCount && buttonLabel != nullptr) {
    labelStr = buttonLabel(selectedIndex);
  } else if (!focusedBookAuthorForLabel.empty()) {
    labelStr = focusedBookAuthorForLabel;
  }
  if (!labelStr.empty()) {
    const auto centered = renderer.truncatedText(SMALL_FONT_ID, labelStr.c_str(), screenW - 40);
    const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, centered.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, (screenW - labelWidth) / 2, labelY + 2, centered.c_str(), true,
                      EpdFontFamily::REGULAR);
  }
  focusedBookAuthorForLabel.clear();
}
