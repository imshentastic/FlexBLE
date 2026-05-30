#pragma once

// ChapterCmbSlimBuilder -- Slice 2 attempt A. Walks pre-parsed paragraphs
// out of the .cmb sidecar (CmbReader) and feeds them through the existing
// ParsedText layout core, exactly the way ChapterHtmlSlimParser does for
// HTML chapters. The point is to skip the expat parse + CSS resolve +
// table/footnote machinery on a cold open when we have the .cmb already.
//
// Scope intentionally narrow:
//   - kCmbBlockText paragraphs only (text, alignment, heading_level,
//     style runs). Bionic, guide dots, hyphenation pass-through.
//   - kCmbBlockImage / kCmbBlockHr / kCmbBlockPageBreak are SKIPPED in
//     this first cut (no images, no <hr>). Adding them is a follow-up.
//   - No anchor-table, no footnote-table; both default to empty
//     vectors so Section.cpp's existing writers serialise zero entries.
//
// If parseAndBuildPages returns false the caller must clear its lut +
// pageCount and fall back to ChapterHtmlSlimParser. Section.cpp does
// exactly that.

#include <CmbFormat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"

class Page;
class GfxRenderer;

class ChapterCmbSlimBuilder {
 public:
  using PageCompleteFn = std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t)>;

  ChapterCmbSlimBuilder(const std::string& cmbPath, GfxRenderer& renderer, int fontId, float lineCompression,
                        bool extraParagraphSpacing, bool forceParagraphIndents, uint8_t paragraphAlignment,
                        uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                        bool bionicReadingEnabled, bool guideReadingEnabled, int chapterIdx,
                        PageCompleteFn completePageFn, const std::function<void()>& popupFn = nullptr)
      : cmbPath(cmbPath),
        renderer(renderer),
        completePageFn(std::move(completePageFn)),
        popupFn(popupFn),
        fontId(fontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        forceParagraphIndents(forceParagraphIndents),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        bionicReadingEnabled(bionicReadingEnabled),
        guideReadingEnabled(guideReadingEnabled),
        chapterIdx(chapterIdx) {}

  ~ChapterCmbSlimBuilder() = default;

  // Returns true when the chapter rendered to completion. Returns false
  // on open failure / read failure / out-of-memory; in that case the
  // caller MUST discard whatever pages it accumulated and rebuild via
  // the XHTML parser path.
  bool parseAndBuildPages();

  // Empty placeholders for Section.cpp parity. Anchors/footnotes aren't
  // yet wired through .cmb -- the returned vectors stay empty so
  // Section.cpp writes the same zero-entry tables it would for an
  // anchor-less HTML chapter.
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }
  bool wasLowMemoryFallbackTriggered() const { return false; }
  bool wasLowMemoryAbortTriggered() const { return lowMemoryAbort; }

 private:
  const std::string& cmbPath;
  GfxRenderer& renderer;
  PageCompleteFn completePageFn;
  std::function<void()> popupFn;
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  bool forceParagraphIndents;
  uint8_t paragraphAlignment;  // user-setting cast to CssTextAlign on use
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool bionicReadingEnabled;
  bool guideReadingEnabled;
  int chapterIdx;

  std::unique_ptr<Page> currentPage;
  int16_t currentPageNextY = 0;
  int completedPageCount = 0;
  bool lowMemoryAbort = false;

  // Mirrors ChapterHtmlSlimParser fields for compatibility with the
  // page-complete callback signature.
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;

  // Stays empty -- see getAnchors() doc above.
  std::vector<std::pair<std::string, uint16_t>> anchorData;

  // Resolve CMB alignment + user setting into the BlockStyle alignment to
  // hand to ParsedText. kCmbAlignDefault means "use the user setting".
  BlockStyle resolveBlockStyleForText(uint8_t cmbAlignment, uint8_t headingLevel) const;

  // Emit one CMB text paragraph. Returns false on OOM.
  bool processTextParagraph(const std::string& text, const std::vector<cmb::CmbStyleRun>& runs, uint8_t alignment,
                            uint8_t headingLevel);

  // Run layout on the supplied ParsedText and append the resulting
  // lines to currentPage; flushes pages when they overflow.
  void layoutAndEmit(ParsedText& textBlock);

  // Append one rendered line to the current page. Flushes the page
  // when the next line would overflow viewportHeight.
  void addLineToPage(std::shared_ptr<TextBlock> line);

  // Look up the style mask at byte offset `pos` by linear-scanning runs.
  // CMB run table size is tiny (almost always < 16 entries per
  // paragraph) so linear is fine.
  static uint8_t styleAtOffset(uint16_t pos, const std::vector<cmb::CmbStyleRun>& runs);
};
