#include "ChapterCmbSlimBuilder.h"

#include <Arduino.h>
#include <CmbReader.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <new>

#include "Epub/Page.h"
#include "Epub/css/CssStyle.h"

namespace {
// CMB style mask bits and EpdFontFamily::Style bits intentionally
// overlap (BOLD=1, ITALIC=2, UNDERLINE=4, STRIKETHROUGH=8) so this is a
// straight reinterpretation. Kept as a function so the assumption is
// documented and stays grep-able.
inline EpdFontFamily::Style mapCmbStyleToFontStyle(const uint8_t cmbStyle) {
  return static_cast<EpdFontFamily::Style>(cmbStyle & 0x0F);
}

inline CssTextAlign mapCmbAlignment(const uint8_t cmbAlignment, const uint8_t userParagraphAlignment) {
  if (cmbAlignment == cmb::kCmbAlignDefault) {
    const auto user = static_cast<CssTextAlign>(userParagraphAlignment);
    return (user == CssTextAlign::None) ? CssTextAlign::Justify : user;
  }
  switch (cmbAlignment) {
    case cmb::kCmbAlignLeft:
      return CssTextAlign::Left;
    case cmb::kCmbAlignCenter:
      return CssTextAlign::Center;
    case cmb::kCmbAlignRight:
      return CssTextAlign::Right;
    case cmb::kCmbAlignJustify:
    default:
      return CssTextAlign::Justify;
  }
}

inline bool isWordBreak(const char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
}  // namespace

uint8_t ChapterCmbSlimBuilder::styleAtOffset(const uint16_t pos, const std::vector<cmb::CmbStyleRun>& runs) {
  uint8_t mask = 0;
  for (const auto& run : runs) {
    if (pos >= run.start && pos < static_cast<uint32_t>(run.start) + run.length) {
      mask |= run.style;
    }
  }
  return mask;
}

BlockStyle ChapterCmbSlimBuilder::resolveBlockStyleForText(const uint8_t cmbAlignment,
                                                           const uint8_t headingLevel) const {
  BlockStyle style;
  style.alignment = mapCmbAlignment(cmbAlignment, paragraphAlignment);
  style.textAlignDefined = true;

  // Headings get a half-line of breathing room above and below. The CSS
  // path is doing more sophisticated work here (margin-collapsing, em-
  // based padding); this is a coarse first approximation that keeps
  // headings visually distinct in the slice-2 attempt-A pass.
  if (headingLevel >= 1 && headingLevel <= 6) {
    const auto lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);
    style.marginTop = static_cast<int16_t>(lineHeight / 2);
    style.marginBottom = static_cast<int16_t>(lineHeight / 2);
  }
  return style;
}

bool ChapterCmbSlimBuilder::parseAndBuildPages() {
  cmb::CmbReader reader;
  if (!reader.open(cmbPath.c_str())) {
    LOG_DBG("ECB", "open failed: %s", cmbPath.c_str());
    return false;
  }

  if (chapterIdx < 0 || chapterIdx >= reader.chapter_count()) {
    LOG_DBG("ECB", "chapter idx %d out of range (count=%u)", chapterIdx, reader.chapter_count());
    return false;
  }

  const uint16_t paragraphCount = reader.chapter_paragraph_count(static_cast<uint16_t>(chapterIdx));
  LOG_DBG("ECB", "Building chapter %d: paragraphs=%u free=%u maxAlloc=%u", chapterIdx, paragraphCount,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  uint32_t lastPopupTick = millis();
  constexpr uint32_t kPopupTickMs = 250;

  for (uint16_t i = 0; i < paragraphCount; ++i) {
    if (popupFn && (millis() - lastPopupTick) >= kPopupTickMs) {
      popupFn();
      lastPopupTick = millis();
    }

    cmb::CmbParagraph paragraph;
    if (!reader.load_paragraph(static_cast<uint16_t>(chapterIdx), i, paragraph)) {
      LOG_ERR("ECB", "load_paragraph(%u,%u) failed", chapterIdx, i);
      return false;
    }

    if (paragraph.type == cmb::kCmbBlockText) {
      if (!processTextParagraph(paragraph.text, paragraph.runs, paragraph.alignment, paragraph.heading_level)) {
        return false;
      }
    } else {
      // kCmbBlockImage / kCmbBlockHr / kCmbBlockPageBreak: not handled
      // in slice 2 attempt A. Silently skipped -- pages get smaller but
      // textual content is intact. If the result looks wrong the caller
      // falls back to the XHTML parser.
    }

    if (lowMemoryAbort) {
      return false;
    }
    yield();
  }

  // Flush the final partially-filled page.
  if (currentPage && !currentPage->elements.empty()) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex);
    completedPageCount++;
    currentPage.reset();
  }

  LOG_DBG("ECB", "Chapter %d built: pages=%d free=%u maxAlloc=%u", chapterIdx, completedPageCount, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  return true;
}

bool ChapterCmbSlimBuilder::processTextParagraph(const std::string& text,
                                                 const std::vector<cmb::CmbStyleRun>& runs, const uint8_t alignment,
                                                 const uint8_t headingLevel) {
  // Empty paragraph: still consumes vertical space so emit a placeholder
  // line via an empty ParsedText (matches HTML parser's <br>/empty-<p>
  // handling).
  const BlockStyle blockStyle = resolveBlockStyleForText(alignment, headingLevel);

  ParsedText textBlock(extraParagraphSpacing, forceParagraphIndents, hyphenationEnabled, bionicReadingEnabled,
                       guideReadingEnabled, blockStyle);

  if (!text.empty()) {
    const size_t n = text.size();
    size_t i = 0;
    bool attachNext = false;
    while (i < n) {
      // Skip leading whitespace runs (multiple consecutive spaces
      // collapse to one word boundary).
      while (i < n && isWordBreak(text[i])) {
        ++i;
        attachNext = false;  // whitespace always resets attach state
      }
      if (i >= n) break;

      const size_t wordStart = i;
      uint8_t currentStyle = styleAtOffset(static_cast<uint16_t>(wordStart), runs);
      size_t segStart = wordStart;
      bool firstSegmentOfWord = true;
      while (i < n && !isWordBreak(text[i])) {
        // Detect mid-word style transition by sampling at byte boundaries.
        // Only sample at codepoint boundaries to avoid splitting a UTF-8
        // multibyte sequence; continuation bytes (10xxxxxx) skip past.
        const auto byte = static_cast<uint8_t>(text[i]);
        const bool isContinuation = (byte & 0xC0) == 0x80;
        if (!isContinuation && i > segStart) {
          const uint8_t styleHere = styleAtOffset(static_cast<uint16_t>(i), runs);
          if (styleHere != currentStyle) {
            const std::string segment = text.substr(segStart, i - segStart);
            const EpdFontFamily::Style fontStyle = mapCmbStyleToFontStyle(currentStyle);
            const bool underline = (currentStyle & cmb::kCmbStyleUnderline) != 0;
            const bool attach = firstSegmentOfWord ? attachNext : true;
            textBlock.addWord(segment, fontStyle, underline, attach, false);
            firstSegmentOfWord = false;
            segStart = i;
            currentStyle = styleHere;
          }
        }
        ++i;
      }
      if (i > segStart) {
        const std::string segment = text.substr(segStart, i - segStart);
        const EpdFontFamily::Style fontStyle = mapCmbStyleToFontStyle(currentStyle);
        const bool underline = (currentStyle & cmb::kCmbStyleUnderline) != 0;
        const bool attach = firstSegmentOfWord ? attachNext : true;
        textBlock.addWord(segment, fontStyle, underline, attach, false);
      }
      // After a whole whitespace-bounded token, the next token is a
      // fresh word (no attach).
      attachNext = false;
    }
  }

  layoutAndEmit(textBlock);
  return !lowMemoryAbort;
}

void ChapterCmbSlimBuilder::layoutAndEmit(ParsedText& textBlock) {
  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      return;
    }
    currentPageNextY = 0;
  }

  const BlockStyle& blockStyle = textBlock.getBlockStyle();
  const auto lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);

  if (blockStyle.marginTop > 0) currentPageNextY += blockStyle.marginTop;
  if (blockStyle.paddingTop > 0) currentPageNextY += blockStyle.paddingTop;

  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  textBlock.layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& tb) { addLineToPage(tb); });

  if (blockStyle.marginBottom > 0) currentPageNextY += blockStyle.marginBottom;
  if (blockStyle.paddingBottom > 0) currentPageNextY += blockStyle.paddingBottom;
  if (extraParagraphSpacing) currentPageNextY += lineHeight / 2;
}

void ChapterCmbSlimBuilder::addLineToPage(std::shared_ptr<TextBlock> line) {
  const auto lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);

  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      return;
    }
    currentPageNextY = 0;
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex);
    completedPageCount++;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      return;
    }
    currentPageNextY = 0;
  }

  const int16_t xOffset = line->getBlockStyle().leftInset();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY = static_cast<int16_t>(currentPageNextY + lineHeight);
}
