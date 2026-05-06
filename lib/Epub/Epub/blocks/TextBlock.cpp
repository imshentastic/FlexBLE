#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint16_t MAX_WORDS_PER_TEXT_BLOCK = 512;
constexpr uint32_t MAX_SERIALIZED_WORD_BYTES = 4096;
constexpr uint32_t SERIALIZED_TEXT_BLOCK_TAIL_BYTES =
    sizeof(EpdFontFamily::Style) + sizeof(bool) + sizeof(int16_t) * 7 + sizeof(bool);
constexpr uint32_t SERIALIZED_WORD_METADATA_BYTES = sizeof(uint32_t) + sizeof(int16_t) + sizeof(EpdFontFamily::Style) +
                                                    sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t);

bool readBoundedString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  if (!serialization::tryReadPod(file, len)) {
    LOG_ERR("TXB", "Deserialization failed: could not read word length");
    return false;
  }
  if (len > MAX_SERIALIZED_WORD_BYTES) {
    LOG_ERR("TXB", "Deserialization failed: word length %lu exceeds maximum", static_cast<unsigned long>(len));
    return false;
  }

  const int remaining = file.available();
  if (remaining < 0 || static_cast<uint32_t>(remaining) < len) {
    LOG_ERR("TXB", "Deserialization failed: truncated word payload (%lu bytes requested, %d available)",
            static_cast<unsigned long>(len), remaining);
    return false;
  }

  if (len == 0) {
    s.clear();
    return true;
  }

  s.resize(len);
  if (file.read(&s[0], len) != static_cast<int>(len)) {
    LOG_ERR("TXB", "Deserialization failed: could not read %lu-byte word payload", static_cast<unsigned long>(len));
    return false;
  }
  return true;
}

}  // namespace

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Validate iterator bounds before rendering
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      words.size() != wordBionicBoundary.size() || words.size() != wordBionicSuffixX.size() ||
      words.size() != wordGuideDotXOffset.size()) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u, dotX=%u)\n",
            (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
            (uint32_t)wordBionicBoundary.size(), (uint32_t)wordBionicSuffixX.size(),
            (uint32_t)wordGuideDotXOffset.size());
    return;
  }

  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const uint8_t boundary = wordBionicBoundary[i];

    if (boundary > 0) {
      // Bionic split: draw bold prefix (max 9 codepoints = 36 UTF-8 bytes + null).
      // suffixX is pre-computed at cache creation time to avoid font metric lookups at render time.
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      char boldBuf[40];
      const size_t boldLen = std::min<size_t>({static_cast<size_t>(boundary), words[i].size(), sizeof(boldBuf) - 1});
      memcpy(boldBuf, words[i].c_str(), boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(fontId, wordX, y, boldBuf, true, boldStyle);
      const int suffixX = wordX + wordBionicSuffixX[i];
      renderer.drawText(fontId, suffixX, y, words[i].c_str() + boldLen, true, currentStyle);
    } else {
      renderer.drawText(fontId, wordX, y, words[i].c_str(), true, currentStyle);
    }

    if (wordGuideDotXOffset[i] > 0) {
      renderer.drawText(fontId, wordX + wordGuideDotXOffset[i], y, "\xc2\xb7", true, EpdFontFamily::REGULAR);
    }

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // y is the top of the text line; add ascender to reach baseline, then offset 2px below
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, 3, true);
    }

    if ((currentStyle & EpdFontFamily::STRIKETHROUGH) != 0) {
      const std::string& w = words[i];
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // Position at roughly mid-glyph height. Offset down from the half-ascender
      // point to align with the visual centre of lowercase letters.
      // Added a 6 pixel offset after testing on various fonts to improve the visual alignment of the strike-through
      // line.
      const int strikeY = y + renderer.getFontAscenderSize(fontId) / 2 + 6;

      int startX = wordX;
      int strikeWidth = fullWordWidth;

      // Skip em-space prefix same as underline does
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        strikeWidth = visibleWidth;
      }

      renderer.drawLine(startX, strikeY, startX + strikeWidth, strikeY, 3, true);
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      words.size() != wordBionicBoundary.size() || words.size() != wordBionicSuffixX.size() ||
      words.size() != wordGuideDotXOffset.size()) {
    LOG_ERR("TXB",
            "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u, dotX=%u)\n",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(wordBionicBoundary.size()),
            static_cast<uint32_t>(wordBionicSuffixX.size()), static_cast<uint32_t>(wordGuideDotXOffset.size()));
    return false;
  }

  // Word data
  if (!serialization::tryWritePod(file, static_cast<uint16_t>(words.size()))) {
    LOG_ERR("TXB", "Serialization failed: could not write word count");
    return false;
  }
  for (const auto& w : words) {
    if (!serialization::tryWriteString(file, w)) {
      LOG_ERR("TXB", "Serialization failed: could not write word payload");
      return false;
    }
  }
  for (auto x : wordXpos) {
    if (!serialization::tryWritePod(file, x)) return false;
  }
  for (auto s : wordStyles) {
    if (!serialization::tryWritePod(file, s)) return false;
  }
  for (auto b : wordBionicBoundary) {
    if (!serialization::tryWritePod(file, b)) return false;
  }
  for (auto sx : wordBionicSuffixX) {
    if (!serialization::tryWritePod(file, sx)) return false;
  }
  for (auto dx : wordGuideDotXOffset) {
    if (!serialization::tryWritePod(file, dx)) return false;
  }

  // Style (alignment + margins/padding/indent)
  return serialization::tryWritePod(file, blockStyle.alignment) &&
         serialization::tryWritePod(file, blockStyle.textAlignDefined) &&
         serialization::tryWritePod(file, blockStyle.marginTop) &&
         serialization::tryWritePod(file, blockStyle.marginBottom) &&
         serialization::tryWritePod(file, blockStyle.marginLeft) &&
         serialization::tryWritePod(file, blockStyle.marginRight) &&
         serialization::tryWritePod(file, blockStyle.paddingTop) &&
         serialization::tryWritePod(file, blockStyle.paddingBottom) &&
         serialization::tryWritePod(file, blockStyle.paddingLeft) &&
         serialization::tryWritePod(file, blockStyle.paddingRight) &&
         serialization::tryWritePod(file, blockStyle.textIndent) &&
         serialization::tryWritePod(file, blockStyle.textIndentDefined);
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<uint8_t> wordBionicBoundary;
  std::vector<uint16_t> wordBionicSuffixX;
  std::vector<uint16_t> wordGuideDotXOffset;
  BlockStyle blockStyle;

  // Word count
  if (!serialization::tryReadPod(file, wc)) {
    LOG_ERR("TXB", "Deserialization failed: could not read word count");
    return nullptr;
  }

  // A TextBlock is one rendered line of text, so counts far above a few hundred are not legitimate.
  // Clamp aggressively here so corrupted cache data cannot trigger huge STL allocations on the ESP32-C3.
  if (wc > MAX_WORDS_PER_TEXT_BLOCK) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  const uint32_t minimumRemainingBytes =
      static_cast<uint32_t>(wc) * SERIALIZED_WORD_METADATA_BYTES + SERIALIZED_TEXT_BLOCK_TAIL_BYTES;
  const int remainingBeforeWords = file.available();
  if (remainingBeforeWords < 0 || static_cast<uint32_t>(remainingBeforeWords) < minimumRemainingBytes) {
    LOG_ERR("TXB", "Deserialization failed: truncated block metadata (%u words need at least %lu bytes, %d available)",
            wc, static_cast<unsigned long>(minimumRemainingBytes), remainingBeforeWords);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  wordBionicBoundary.resize(wc);
  wordBionicSuffixX.resize(wc);
  wordGuideDotXOffset.resize(wc);
  for (auto& w : words) {
    if (!readBoundedString(file, w)) {
      return nullptr;
    }
  }

  const uint32_t remainingMetadataBytes =
      static_cast<uint32_t>(wc) *
          (sizeof(int16_t) + sizeof(EpdFontFamily::Style) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t)) +
      SERIALIZED_TEXT_BLOCK_TAIL_BYTES;
  const int remainingAfterWords = file.available();
  if (remainingAfterWords < 0 || static_cast<uint32_t>(remainingAfterWords) < remainingMetadataBytes) {
    LOG_ERR("TXB", "Deserialization failed: truncated post-word metadata (%lu bytes needed, %d available)",
            static_cast<unsigned long>(remainingMetadataBytes), remainingAfterWords);
    return nullptr;
  }

  for (auto& x : wordXpos) {
    if (!serialization::tryReadPod(file, x)) return nullptr;
  }
  for (auto& s : wordStyles) {
    if (!serialization::tryReadPod(file, s)) return nullptr;
  }
  for (auto& b : wordBionicBoundary) {
    if (!serialization::tryReadPod(file, b)) return nullptr;
  }
  for (auto& sx : wordBionicSuffixX) {
    if (!serialization::tryReadPod(file, sx)) return nullptr;
  }
  for (auto& dx : wordGuideDotXOffset) {
    if (!serialization::tryReadPod(file, dx)) return nullptr;
  }

  // Style (alignment + margins/padding/indent)
  if (!serialization::tryReadPod(file, blockStyle.alignment) ||
      !serialization::tryReadPod(file, blockStyle.textAlignDefined) ||
      !serialization::tryReadPod(file, blockStyle.marginTop) ||
      !serialization::tryReadPod(file, blockStyle.marginBottom) ||
      !serialization::tryReadPod(file, blockStyle.marginLeft) ||
      !serialization::tryReadPod(file, blockStyle.marginRight) ||
      !serialization::tryReadPod(file, blockStyle.paddingTop) ||
      !serialization::tryReadPod(file, blockStyle.paddingBottom) ||
      !serialization::tryReadPod(file, blockStyle.paddingLeft) ||
      !serialization::tryReadPod(file, blockStyle.paddingRight) ||
      !serialization::tryReadPod(file, blockStyle.textIndent) ||
      !serialization::tryReadPod(file, blockStyle.textIndentDefined)) {
    LOG_ERR("TXB", "Deserialization failed: truncated block style metadata");
    return nullptr;
  }

  auto* textBlock = new (std::nothrow)
      TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), std::move(wordBionicBoundary),
                std::move(wordBionicSuffixX), std::move(wordGuideDotXOffset), blockStyle);
  if (!textBlock) {
    LOG_ERR("TXB", "Deserialization failed: could not allocate TextBlock");
    return nullptr;
  }

  return std::unique_ptr<TextBlock>(textBlock);
}
