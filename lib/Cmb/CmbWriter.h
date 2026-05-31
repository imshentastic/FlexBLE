#pragma once

// CmbWriter -- streaming write of the CrumBLE .cmb compact-binary book
// format. Usage:
//
//   cmb::CmbWriter w;
//   if (!w.open("book.cmb")) { ... }
//
//   for (each chapter) {
//     w.begin_chapter();
//     for (each paragraph) w.write_paragraph(p);
//     w.end_chapter();
//   }
//
//   for (each image) w.add_image_ref(local_header_offset, w, h);
//
//   w.finish(metadata_title, metadata_author);
//   w.close();
//
// Internally backed by a 4 KB BufferedFileWriter that batches small
// writes. Chapter content blobs are written sequentially (no backward
// seeks during chapter content). All tables (per-chapter descriptors,
// chapter table, image refs, metadata) are written AFTER all chapter
// content, then the 32-byte header is patched in at file offset 0 in
// a single seek-and-write.
//
// Heap budget: ~4 KB write buffer + transient vectors for the chapter
// table + image refs (~16 + 8 bytes per entry, allocated as the writer
// runs). For a 377-chapter War-and-Peace EPUB that's ~6 KB peak, well
// under the worst-case on-device budget.

#include <cstdint>
#include <string>
#include <vector>

#ifdef ARDUINO
// On-device: route file I/O through the SdFat-backed HalStorage layer.
// stdio fopen/fwrite don't work because SdFat doesn't register itself
// as a VFS for libc -- the SD card is only reachable through HalFile.
#include <HalStorage.h>
#else
// Host-side (unit tests, prospective desktop converter): plain stdio.
#include <cstdio>
#endif

#include "CmbFormat.h"

namespace cmb {

// 4 KB write-buffered file wrapper. On device the backing file is a
// HalFile (SdFat); on host it's a stdio FILE*. The split is hidden
// behind a single member type so the rest of CmbWriter doesn't care.
// Exposed in this header so unit tests can directly exercise the
// buffering / seek behavior if needed.
class BufferedFileWriter {
 public:
  BufferedFileWriter() = default;
  ~BufferedFileWriter() { close(); }
  BufferedFileWriter(const BufferedFileWriter&) = delete;
  BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

  bool open(const char* path);
  void close();
  bool is_open() const;

  // Buffered byte write. Returns false on I/O failure.
  bool write(const void* data, size_t size);

  // Flush the buffer to the underlying file. Called automatically on
  // close(); callers usually don't need this except before a seek().
  bool flush();

  // Move the file cursor. Flushes the buffer first so subsequent writes
  // land at the new position. Used by CmbWriter::finish() to patch
  // in the header at file offset 0.
  bool seek(uint32_t offset);

  // Current logical file position (post-buffer). O(1).
  uint32_t tell() const { return pos_; }

 private:
  static constexpr size_t kBufSize = 4096;
#ifdef ARDUINO
  FsFile f_;
#else
  FILE* f_ = nullptr;
#endif
  uint32_t pos_ = 0;
  size_t used_ = 0;
  uint8_t buf_[kBufSize] = {};
};

class CmbWriter {
 public:
  CmbWriter() = default;
  ~CmbWriter() { close(); }
  CmbWriter(const CmbWriter&) = delete;
  CmbWriter& operator=(const CmbWriter&) = delete;

  bool open(const char* path);
  void close();
  bool is_open() const { return bw_.is_open(); }

  // Chapter management. begin_chapter() resets per-chapter state;
  // every write_paragraph() between begin/end appends to the current
  // chapter's content blob. end_chapter() flushes the chapter's
  // descriptor table (file_offset + char_offset per paragraph) and
  // records the chapter's table entry.
  void begin_chapter();
  bool write_paragraph(const CmbParagraph& p);
  void end_chapter();

  // Image reference table. Add one entry per image referenced by any
  // paragraph in the book. Returns the image index to put in the
  // paragraph's image_key field. Idempotent on the (offset, w, h)
  // tuple -- duplicate adds return the same index.
  uint16_t add_image_ref(uint32_t local_header_offset, uint16_t width, uint16_t height);

  // Final pass: writes all the tables (per-chapter descriptors,
  // chapter table, image refs, metadata) and patches the header in
  // at offset 0. Must be called once after all chapters are done;
  // subsequent close() will leave the file valid.
  //
  // `metadata.spine` MUST be `chapter_count`-long (one entry per
  // chapter, in spine order). The full BookMetadataCache set --
  // title, author, language, cover/text-ref hrefs, spine cumulative
  // sizes, CSS files -- gets serialised so `Epub::load()` can
  // populate the metadata cache directly from .cmb without parsing
  // the EPUB ZIP central directory + content.opf.
  bool finish(const CmbBookMetadata& metadata);

 private:
  BufferedFileWriter bw_;

  // Total paragraph count across all chapters (header.paragraph_count).
  uint32_t paragraph_count_ = 0;

  // Per-chapter table -- built as we go, written in finish().
  std::vector<CmbChapterEntry> chapters_;

  // Per-image table -- built as add_image_ref() is called, written in finish().
  std::vector<CmbImageRef> images_;

  // Per-chapter descriptor table, accumulating during begin_chapter ..
  // end_chapter. end_chapter() writes this to disk (at the chapter's
  // para_table_offset) and clears the vector for the next chapter.
  std::vector<CmbParaDescriptor> current_chapter_descriptors_;
  uint32_t current_chapter_char_count_ = 0;
  bool in_chapter_ = false;

  // Serializes a paragraph to its on-disk tagged-record encoding and
  // writes to bw_. Returns the byte length actually written (so the
  // caller can update char_offset accumulators). Returns 0 on I/O
  // error; check bw_.is_open() to disambiguate from "empty record".
  size_t serialize_paragraph(const CmbParagraph& p);
};

}  // namespace cmb
