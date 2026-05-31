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

  // (anchor_id, page_index) pairs collected as paragraphs were emitted.
  // Populated when a CMB paragraph carries a non-empty anchor_id (set by
  // the converter from the source XHTML's `id` attribute). Used by
  // Section's anchor-table writer to support in-book fragment
  // navigation (footnote -> target, TOC anchor -> chapter mid-point).
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }
  // Always false: the CMB path has no equivalent of the XHTML parser's
  // "images suppressed under BLE" fallback yet (image blocks are
  // skipped outright, not degraded). Kept for Section.cpp API parity.
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

  // Emit a horizontal rule onto the current page, breaking the page if
  // the rule + its spacing would overflow viewportHeight.
  void processHorizontalRule();

  // Hard page break: flush whatever's on currentPage and start a fresh one
  // even if the current one isn't full. Matches HTML's <mbp:pagebreak/>.
  void processPageBreak();

  // Record the anchor at the current page boundary. Called for every
  // CMB paragraph that has a non-empty anchor_id, so in-book fragment
  // navigation (footnote -> target) lands on the right page.
  void recordAnchor(const std::string& anchorId);

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
