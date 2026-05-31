#pragma once

// CmbReader -- read-side of the CrumBLE .cmb compact-binary book
// format. Loads the small fixed-size tables (header, chapter table,
// image refs, metadata) into RAM at open(); the per-chapter paragraph
// descriptor tables stay on SD and are seeked on demand. This is the
// trick that keeps a 377-spine-item book's open() at ~5 KB heap
// instead of ~136 KB.
//
// Usage:
//
//   cmb::CmbReader r;
//   if (!r.open("book.cmb")) { ... }
//
//   r.chapter_count();
//   r.image_count();
//   r.metadata_title();
//   r.metadata_author();
//
//   for (uint16_t ci = 0; ci < r.chapter_count(); ++ci) {
//     const auto N = r.chapter_paragraph_count(ci);
//     for (uint16_t pi = 0; pi < N; ++pi) {
//       cmb::CmbParagraph p;
//       if (r.load_paragraph(ci, pi, p)) { ... }
//     }
//   }
//
// Heap budget at open: header (32 B) + chapter table (16 B per
// chapter) + image refs (8 B per image) + metadata strings. For a
// 377-chapter, 50-image book that's 32 + 6 KB + 400 B + ~2 KB =
// ~8 KB peak; per-paragraph descriptors are seeked on demand.

#include <cstdint>
#include <string>
#include <vector>

#ifdef ARDUINO
// On-device: file I/O goes through the SdFat-backed HalStorage layer.
// stdio fopen/fread don't see the SD card (no VFS registration), so
// the reader has to use HalFile here -- mirroring CmbWriter.
#include <HalStorage.h>
#else
#include <cstdio>
#endif

#include "CmbFormat.h"

namespace cmb {

class CmbReader {
 public:
  CmbReader() = default;
  ~CmbReader() { close(); }
  CmbReader(const CmbReader&) = delete;
  CmbReader& operator=(const CmbReader&) = delete;

  bool open(const char* path);
  void close();
  bool is_open() const;

  // Header fields. Valid after a successful open().
  uint32_t paragraph_count() const { return header_.paragraph_count; }
  uint16_t chapter_count() const { return header_.chapter_count; }
  uint16_t image_count() const { return header_.image_count; }

  // Per-chapter accessors. chapter_idx must be < chapter_count();
  // returns 0 if out of range.
  uint16_t chapter_paragraph_count(uint16_t chapter_idx) const;
  uint32_t chapter_char_count(uint16_t chapter_idx) const;

  // Load the Nth paragraph of a chapter. Seeks to the descriptor
  // table on SD to find the file offset, then seeks to that offset
  // to read the tagged record. Returns true on success; on false,
  // `out` is left in an unspecified state and the read should be
  // treated as missing.
  bool load_paragraph(uint16_t chapter_idx, uint16_t para_idx, CmbParagraph& out);

  // Direct image-ref access. Indices match the image_key stored on
  // CmbParagraph (for kCmbBlockImage). out_width / out_height are
  // the EPUB-declared values; 0 means "resolve at display time".
  bool image_ref(uint16_t image_idx, CmbImageRef& out) const;

  // Aggregate metadata loaded at open(). Title/author/language/cover/
  // text-ref strings + spine entries (with cumulative sizes) + CSS
  // file paths -- everything Epub::load needs to populate
  // BookMetadataCache without parsing the EPUB.
  const CmbBookMetadata& metadata() const { return metadata_; }

  // Convenience shorthands kept for the round-trip tests and for the
  // converter / stats-style callers that only need a single field.
  const std::string& metadata_title() const { return metadata_.title; }
  const std::string& metadata_author() const { return metadata_.author; }
  // Spine entry hrefs in spine order; legacy accessor that returns a
  // freshly-built vector of just the hrefs for callers that don't
  // need the cumulative_size data. Prefer metadata().spine for new
  // code.
  std::vector<std::string> spine_files() const {
    std::vector<std::string> out;
    out.reserve(metadata_.spine.size());
    for (const auto& e : metadata_.spine) out.push_back(e.href);
    return out;
  }

 private:
#ifdef ARDUINO
  FsFile f_;
#else
  FILE* f_ = nullptr;
#endif
  CmbHeader header_{};

  // In-RAM tables. Sized at open() and never resized after.
  std::vector<CmbChapterEntry> chapters_;
  std::vector<CmbImageRef> images_;
  CmbBookMetadata metadata_;

  // Helper: seek + read one paragraph record at a known absolute
  // file offset. Used by load_paragraph after the descriptor lookup.
  bool read_paragraph_at(uint32_t file_offset, CmbParagraph& out);
};

}  // namespace cmb
