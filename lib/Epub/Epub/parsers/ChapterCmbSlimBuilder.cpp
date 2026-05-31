#include "ChapterCmbSlimBuilder.h"

#include <Arduino.h>
#include <CmbReader.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <new>

#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
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

  // Show the "Indexing..." popup up front, then tick it every 250 ms so
  // it animates while the build runs. Mirrors ChapterHtmlSlimParser
  // -- without the initial call the popup never appears, even when the
  // chapter build takes long enough to be visible to the user.
  if (popupFn) popupFn();
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

    switch (paragraph.type) {
      case cmb::kCmbBlockText:
        if (!processTextParagraph(paragraph.text, paragraph.runs, paragraph.alignment, paragraph.heading_level)) {
          return false;
        }
        break;
      case cmb::kCmbBlockHr:
        processHorizontalRule();
        break;
      case cmb::kCmbBlockPageBreak:
        processPageBreak();
        break;
      case cmb::kCmbBlockImage: {
        cmb::CmbImageRef ref{};
        if (epub != nullptr && reader.image_ref(paragraph.image_key, ref)) {
          if (!processImageBlock(ref.local_header_offset, ref.width, ref.height)) {
            return false;
          }
        }
        // Unknown image_key or missing epub: skip silently. The slot
        // collapses; surrounding paragraphs flow normally.
        break;
      }
      default:
        // Unknown future block type. CMB format reserves unknown
        // tags as "skip via length prefix"; the reader already
        // returns out.type set to the unknown value so we can just
        // ignore here.
        break;
    }

    // Record the anchor at whichever page the previous block landed
    // on. Anchors fire regardless of block type so footnote targets
    // (typically the first text paragraph of the chapter) and hr-
    // separated sections both resolve correctly.
    if (!paragraph.anchor_id.empty()) {
      recordAnchor(paragraph.anchor_id);
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

void ChapterCmbSlimBuilder::processHorizontalRule() {
  // Sizing mirrors ChapterHtmlSlimParser::emitHorizontalRule: 1/4 of the
  // available content width, centered, 2 px thick, with a half-line of
  // breathing room above and below.
  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      return;
    }
    currentPageNextY = 0;
  }

  const auto lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression + 0.5f);
  const int16_t verticalSpacing = static_cast<int16_t>(lineHeight / 2);
  constexpr uint8_t kRuleThickness = 2;
  const int16_t availableWidth = std::max<int16_t>(1, static_cast<int16_t>(viewportWidth));
  const int16_t width = std::max<int16_t>(1, static_cast<int16_t>(availableWidth / 4));
  const int16_t xPos = static_cast<int16_t>((availableWidth - width) / 2);
  const int16_t totalHeight = static_cast<int16_t>(verticalSpacing + kRuleThickness + verticalSpacing);

  // Flush the page if the rule + its spacing won't fit; same rule the
  // XHTML emitter uses.
  if (!currentPage->elements.empty() && currentPageNextY + totalHeight > viewportHeight) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex);
    completedPageCount++;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      return;
    }
    currentPageNextY = 0;
  }

  currentPageNextY = static_cast<int16_t>(currentPageNextY + verticalSpacing);
  auto pageRule = std::shared_ptr<PageHorizontalRule>(
      new (std::nothrow) PageHorizontalRule(width, kRuleThickness, xPos, currentPageNextY));
  if (!pageRule) {
    lowMemoryAbort = true;
    return;
  }
  currentPage->elements.push_back(pageRule);
  currentPageNextY = static_cast<int16_t>(currentPageNextY + kRuleThickness + verticalSpacing);
}

void ChapterCmbSlimBuilder::processPageBreak() {
  // Force a page boundary even if the current page isn't full. Skips
  // entirely when the current page is empty (a leading <mbp:pagebreak/>
  // would otherwise emit an empty page that the reader would then
  // skip-display).
  if (currentPage && !currentPage->elements.empty()) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex);
    completedPageCount++;
    currentPage.reset();
    currentPageNextY = 0;
  }
}

bool ChapterCmbSlimBuilder::processImageBlock(const uint32_t localHeaderOffset, const uint16_t declaredWidth,
                                              const uint16_t declaredHeight) {
  if (epub == nullptr) return true;  // silently skip when caller didn't pass an Epub

  // Recover the entry's filename from its local file header so we know
  // which decoder to dispatch to (the factory keys off extension).
  std::string entryName;
  if (!epub->getZipEntryFilenameAtOffset(localHeaderOffset, &entryName) || entryName.empty()) {
    LOG_DBG("ECB", "image at offset %u: filename lookup failed, skipping", localHeaderOffset);
    return true;
  }
  std::string ext;
  const size_t dot = entryName.rfind('.');
  if (dot != std::string::npos) ext = entryName.substr(dot);
  if (!ImageDecoderFactory::isFormatSupported(ext)) {
    LOG_DBG("ECB", "image '%s': format not supported, skipping", entryName.c_str());
    return true;
  }

  // Extract the inflated image bytes to a per-chapter cache file. ImageBlock
  // reads from this path at render time (decoder reopens it).
  const std::string cachedImagePath = imageBasePath + std::to_string(imageCounter++) + ext;
  {
    FsFile cachedImageFile;
    if (!Storage.openFileForWrite("ECB", cachedImagePath, cachedImageFile)) {
      LOG_ERR("ECB", "could not open image cache file %s", cachedImagePath.c_str());
      return true;
    }
    constexpr size_t kExtractChunkSize = 1024;
    const bool extractedOk = epub->readItemContentsToStreamAtOffset(localHeaderOffset, cachedImageFile, kExtractChunkSize);
    cachedImageFile.flush();
    cachedImageFile.close();
    if (!extractedOk) {
      LOG_ERR("ECB", "image extract from offset %u failed", localHeaderOffset);
      Storage.remove(cachedImagePath.c_str());
      return true;
    }
    delay(50);  // SD card sync, same defensive pause as the XHTML path
  }

  // Pull source dimensions from the just-extracted file. EPUBs often
  // declare width/height in markup (carried through to CmbImageRef);
  // fall back to those when the decoder can't probe.
  ImageDimensions dims = {0, 0};
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(cachedImagePath);
  if (decoder == nullptr || !decoder->getDimensions(cachedImagePath, dims)) {
    if (declaredWidth > 0 && declaredHeight > 0) {
      dims.width = declaredWidth;
      dims.height = declaredHeight;
    } else {
      LOG_DBG("ECB", "image '%s': dims unknown, skipping", cachedImagePath.c_str());
      Storage.remove(cachedImagePath.c_str());
      return true;
    }
  }

  // Fit to viewport preserving aspect ratio (no CSS path -- CMB doesn't
  // carry per-image style).
  const int maxW = viewportWidth;
  const int maxH = viewportHeight;
  int displayW = dims.width;
  int displayH = dims.height;
  if (displayW <= 0 || displayH <= 0) {
    Storage.remove(cachedImagePath.c_str());
    return true;
  }
  if (displayW > maxW || displayH > maxH) {
    const float scaleX = displayW > maxW ? static_cast<float>(maxW) / displayW : 1.0f;
    const float scaleY = displayH > maxH ? static_cast<float>(maxH) / displayH : 1.0f;
    const float scale = std::min(scaleX, scaleY);
    displayW = std::max(1, static_cast<int>(displayW * scale + 0.5f));
    displayH = std::max(1, static_cast<int>(displayH * scale + 0.5f));
  }

  // Make sure we have a page; flush if the image won't fit in remaining
  // vertical space and the page isn't empty (avoid leading-blank pages).
  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      Storage.remove(cachedImagePath.c_str());
      return false;
    }
    currentPageNextY = 0;
  }
  if (!currentPage->elements.empty() && currentPageNextY + displayH > viewportHeight) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex);
    completedPageCount++;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      lowMemoryAbort = true;
      Storage.remove(cachedImagePath.c_str());
      return false;
    }
    currentPageNextY = 0;
  }

  // Center horizontally; if the image is taller than the page, just cap
  // it at the viewport height (matches the XHTML parser's fallback).
  const int16_t xPos = static_cast<int16_t>((viewportWidth - displayW) / 2);
  const int16_t finalH = static_cast<int16_t>(std::min(displayH, viewportHeight - currentPageNextY));
  if (finalH <= 0) {
    Storage.remove(cachedImagePath.c_str());
    return true;
  }

  auto imageBlock = std::shared_ptr<ImageBlock>(new (std::nothrow) ImageBlock(cachedImagePath, displayW, finalH));
  if (!imageBlock) {
    lowMemoryAbort = true;
    Storage.remove(cachedImagePath.c_str());
    return false;
  }
  auto pageImage = std::shared_ptr<PageImage>(new (std::nothrow) PageImage(imageBlock, xPos, currentPageNextY));
  if (!pageImage) {
    lowMemoryAbort = true;
    return false;
  }
  currentPage->elements.push_back(pageImage);
  currentPageNextY = static_cast<int16_t>(currentPageNextY + finalH);
  return true;
}

void ChapterCmbSlimBuilder::recordAnchor(const std::string& anchorId) {
  // Anchor lands on whichever page the previously-emitted block ended
  // up on. completedPageCount == the next page index since pages are
  // counted post-flush, so we use it as the target page for any anchor
  // that immediately precedes the next block (matches the XHTML
  // parser's pendingAnchorId -> next-block-page contract).
  anchorData.push_back({anchorId, static_cast<uint16_t>(completedPageCount)});
}
