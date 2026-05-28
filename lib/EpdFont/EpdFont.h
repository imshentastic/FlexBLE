#pragma once
#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;

  const EpdGlyph* findGlyph(uint32_t cp) const;
  const EpdGlyph* getGlyph(uint32_t cp) const;

  /// Returns the kerning adjustment (4.4 fixed-point in pixels) between two codepoints.
  /// Returns 0 if no kerning data exists for the pair.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;

  /// Returns the ligature codepoint for a pair, or 0 if no ligature exists.
  uint32_t getLigature(uint32_t leftCp, uint32_t rightCp) const;

  /// Greedily applies ligature substitutions starting from cp, consuming
  /// as many following codepoints from text as possible. Returns the
  /// (possibly substituted) codepoint; advances text past consumed chars.
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;

  /// Sets the fallback font consulted when a codepoint is missing from
  /// this font's coverage and from its on-demand miss handler. Single-
  /// hop: the fallback's own fallback is not chased. `fallback_` is
  /// mutable so callers holding `const EpdFont*` (e.g. EpdFontFamily)
  /// can configure it. Port of rhythmerc/crosspoint-reader 023a8b1.
  void setFallback(const EpdFont* fallback) const { fallback_ = fallback; }
  const EpdFont* getFallback() const { return fallback_; }

 private:
  mutable const EpdFont* fallback_ = nullptr;
};
