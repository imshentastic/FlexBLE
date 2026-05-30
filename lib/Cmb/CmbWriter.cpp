#include "CmbWriter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cmb {

// ===========================================================================
// BufferedFileWriter
// ===========================================================================

bool BufferedFileWriter::open(const char* path) {
  if (f_ != nullptr) close();
  f_ = std::fopen(path, "wb+");
  if (f_ == nullptr) return false;
  pos_ = 0;
  used_ = 0;
  return true;
}

void BufferedFileWriter::close() {
  if (f_ == nullptr) return;
  // Best-effort flush; if it fails we still want to close the handle.
  (void)flush();
  std::fclose(f_);
  f_ = nullptr;
  pos_ = 0;
  used_ = 0;
}

bool BufferedFileWriter::flush() {
  if (f_ == nullptr) return false;
  if (used_ == 0) return true;
  const size_t written = std::fwrite(buf_, 1, used_, f_);
  used_ = 0;
  return written != 0;
}

bool BufferedFileWriter::write(const void* data, size_t size) {
  if (f_ == nullptr) return false;
  const uint8_t* src = static_cast<const uint8_t*>(data);

  while (size > 0) {
    // Fast path: whole big writes that don't fit in the buffer can bypass
    // the buffer entirely once it's been flushed -- saves a copy.
    if (used_ == 0 && size >= kBufSize) {
      const size_t written = std::fwrite(src, 1, size, f_);
      if (written != size) return false;
      pos_ += static_cast<uint32_t>(size);
      return true;
    }

    const size_t room = kBufSize - used_;
    const size_t copy = (size < room) ? size : room;
    std::memcpy(buf_ + used_, src, copy);
    used_ += copy;
    src += copy;
    size -= copy;
    pos_ += static_cast<uint32_t>(copy);

    if (used_ == kBufSize) {
      if (!flush()) return false;
    }
  }
  return true;
}

bool BufferedFileWriter::seek(uint32_t offset) {
  if (f_ == nullptr) return false;
  if (!flush()) return false;
  if (std::fseek(f_, static_cast<long>(offset), SEEK_SET) != 0) return false;
  pos_ = offset;
  return true;
}

// ===========================================================================
// CmbWriter
// ===========================================================================

bool CmbWriter::open(const char* path) {
  if (!bw_.open(path)) return false;

  // Reserve 32 bytes for the header. We don't know paragraph_count /
  // chapter_count / offsets yet, so write zeros and patch in finish().
  // pos_ advances to 32 so subsequent chapter content writes land at
  // offset 32+.
  uint8_t zero_header[32] = {};
  if (!bw_.write(zero_header, sizeof(zero_header))) {
    bw_.close();
    return false;
  }

  paragraph_count_ = 0;
  chapters_.clear();
  images_.clear();
  current_chapter_descriptors_.clear();
  current_chapter_char_count_ = 0;
  in_chapter_ = false;
  return true;
}

void CmbWriter::close() { bw_.close(); }

void CmbWriter::begin_chapter() {
  current_chapter_descriptors_.clear();
  current_chapter_char_count_ = 0;
  in_chapter_ = true;
}

size_t CmbWriter::serialize_paragraph(const CmbParagraph& p) {
  // Compute payload length first so the {tag, length, payload}
  // envelope is correct -- and so readers can SKIP unknown tags by
  // jumping length bytes forward without parsing the payload.
  //
  // Payload sizes per type (in bytes):
  //   Text:
  //     alignment(1) + heading_level(1) + text_len(4) + text(N)
  //       + run_count(2) + runs(R*5) + anchor_id_len(1) + anchor_id(A)
  //   Image:
  //     image_key(2) + anchor_id_len(1) + anchor_id(A)
  //   Hr / PageBreak:
  //     (empty)

  const size_t anchor_len = p.anchor_id.size();
  if (anchor_len > 255) return 0;  // anchor_id_len is a u8

  uint32_t payload_len = 0;
  switch (p.type) {
    case kCmbBlockText:
      payload_len = 1 + 1 + 4 + static_cast<uint32_t>(p.text.size()) + 2 +
                    static_cast<uint32_t>(p.runs.size()) * 5 + 1 +
                    static_cast<uint32_t>(anchor_len);
      break;
    case kCmbBlockImage:
      payload_len = 2 + 1 + static_cast<uint32_t>(anchor_len);
      break;
    case kCmbBlockHr:
    case kCmbBlockPageBreak:
      payload_len = 0;
      break;
    default:
      // Unknown type tag -- writer refuses to emit ambiguous records.
      // Standard tags only; higher-level converters should map their
      // block kinds onto these or negotiate a vendor-extension tag
      // (>= 128) with the format.
      return 0;
  }

  if (payload_len > 0xFFFF) {
    // length field is u16; any single paragraph exceeding 65 KB of
    // payload is a writer bug or a pathological book. Bail rather
    // than silently truncating.
    return 0;
  }
  if (p.runs.size() > 0xFFFF) return 0;  // run_count is u16

  uint8_t envelope[3];
  envelope[0] = p.type;
  cmb_write_u16(envelope + 1, static_cast<uint16_t>(payload_len));
  if (!bw_.write(envelope, sizeof(envelope))) return 0;

  switch (p.type) {
    case kCmbBlockText: {
      // Fixed-size head: alignment + heading_level + text_len.
      uint8_t head[6];
      head[0] = p.alignment;
      head[1] = p.heading_level;
      cmb_write_u32(head + 2, static_cast<uint32_t>(p.text.size()));
      if (!bw_.write(head, sizeof(head))) return 0;

      // Variable-length text body.
      if (!p.text.empty()) {
        if (!bw_.write(p.text.data(), p.text.size())) return 0;
      }

      // Run table.
      uint8_t run_count_buf[2];
      cmb_write_u16(run_count_buf, static_cast<uint16_t>(p.runs.size()));
      if (!bw_.write(run_count_buf, sizeof(run_count_buf))) return 0;
      for (const auto& r : p.runs) {
        uint8_t run_buf[5];
        cmb_write_u16(run_buf, r.start);
        cmb_write_u16(run_buf + 2, r.length);
        run_buf[4] = r.style;
        if (!bw_.write(run_buf, sizeof(run_buf))) return 0;
      }

      // Trailing anchor_id (length-prefixed u8).
      uint8_t anchor_len_buf = static_cast<uint8_t>(anchor_len);
      if (!bw_.write(&anchor_len_buf, 1)) return 0;
      if (anchor_len > 0 && !bw_.write(p.anchor_id.data(), anchor_len)) return 0;
      break;
    }
    case kCmbBlockImage: {
      uint8_t head[3];
      cmb_write_u16(head, p.image_key);
      head[2] = static_cast<uint8_t>(anchor_len);
      if (!bw_.write(head, sizeof(head))) return 0;
      if (anchor_len > 0 && !bw_.write(p.anchor_id.data(), anchor_len)) return 0;
      break;
    }
    case kCmbBlockHr:
    case kCmbBlockPageBreak:
      // No payload bytes.
      break;
  }
  return 3 + payload_len;
}

bool CmbWriter::write_paragraph(const CmbParagraph& p) {
  if (!in_chapter_) return false;
  if (!bw_.is_open()) return false;

  const uint32_t file_offset = bw_.tell();
  const uint32_t char_offset_before = current_chapter_char_count_;

  const size_t written = serialize_paragraph(p);
  if (written == 0) return false;

  // Descriptor records this paragraph's location + cumulative char
  // count. The cumulative count is BEFORE this paragraph so the reader
  // can ask "give me the paragraph at byte N of the chapter" in O(log
  // descriptors) without reading payload bytes.
  CmbParaDescriptor desc;
  desc.file_offset = file_offset;
  desc.char_offset = char_offset_before;
  current_chapter_descriptors_.push_back(desc);

  // Char count tally: only text contributes (image / hr / page break
  // have zero text content).
  if (p.type == kCmbBlockText) {
    current_chapter_char_count_ += static_cast<uint32_t>(p.text.size());
  }
  paragraph_count_++;
  return true;
}

void CmbWriter::end_chapter() {
  if (!in_chapter_) return;

  // Per-chapter descriptor table goes RIGHT AFTER the chapter's last
  // paragraph -- no backward seeks. Record the offset so the chapter
  // table entry can point at it.
  const uint32_t para_table_offset = bw_.tell();
  for (const auto& d : current_chapter_descriptors_) {
    uint8_t buf[8];
    cmb_write_u32(buf, d.file_offset);
    cmb_write_u32(buf + 4, d.char_offset);
    // Best-effort: a write failure here will surface from the next
    // bw_ operation; we don't propagate it through end_chapter()'s
    // void signature to keep the call site shape simple. finish()
    // also validates by re-checking bw_.is_open().
    (void)bw_.write(buf, sizeof(buf));
  }

  CmbChapterEntry entry{};
  entry.para_table_offset = para_table_offset;
  entry.reserved0 = 0;
  entry.paragraph_count = static_cast<uint16_t>(current_chapter_descriptors_.size());
  entry.reserved1 = 0;
  entry.char_count = current_chapter_char_count_;
  chapters_.push_back(entry);

  current_chapter_descriptors_.clear();
  current_chapter_char_count_ = 0;
  in_chapter_ = false;
}

uint16_t CmbWriter::add_image_ref(uint32_t local_header_offset, uint16_t width, uint16_t height) {
  // Idempotent on the (offset, w, h) tuple. Re-adding the same image
  // returns the same index -- prevents duplicate entries when the
  // same image is referenced from multiple paragraphs.
  for (size_t i = 0; i < images_.size(); ++i) {
    const CmbImageRef& r = images_[i];
    if (r.local_header_offset == local_header_offset && r.width == width && r.height == height) {
      return static_cast<uint16_t>(i);
    }
  }
  CmbImageRef r;
  r.local_header_offset = local_header_offset;
  r.width = width;
  r.height = height;
  images_.push_back(r);
  return static_cast<uint16_t>(images_.size() - 1);
}

bool CmbWriter::finish(const std::string& metadata_title, const std::string& metadata_author) {
  if (!bw_.is_open()) return false;
  if (in_chapter_) return false;  // caller forgot end_chapter()

  // ---- chapter table ----
  const uint32_t chapter_offset = bw_.tell();
  for (const auto& c : chapters_) {
    uint8_t buf[16];
    cmb_write_u32(buf, c.para_table_offset);
    cmb_write_u32(buf + 4, c.reserved0);
    cmb_write_u16(buf + 8, c.paragraph_count);
    cmb_write_u16(buf + 10, c.reserved1);
    cmb_write_u32(buf + 12, c.char_count);
    if (!bw_.write(buf, sizeof(buf))) return false;
  }

  // ---- image ref table ----
  const uint32_t image_offset = bw_.tell();
  for (const auto& im : images_) {
    uint8_t buf[8];
    cmb_write_u32(buf, im.local_header_offset);
    cmb_write_u16(buf + 4, im.width);
    cmb_write_u16(buf + 6, im.height);
    if (!bw_.write(buf, sizeof(buf))) return false;
  }

  // ---- anchor table (placeholder for now) ----
  // v1 reserves the slot; populated in a follow-up commit that adds
  // anchor collection during EPUB->CMB conversion.
  const uint32_t anchor_offset = bw_.tell();
  uint8_t anchor_count_buf[4];
  cmb_write_u32(anchor_count_buf, 0);
  if (!bw_.write(anchor_count_buf, sizeof(anchor_count_buf))) return false;

  // ---- metadata + TOC blob ----
  // Minimal v1 layout:
  //   title_len:u16, title:bytes
  //   author_len:u16, author:bytes
  //   toc_entry_count:u16, toc_entries...  (toc reserved for follow-up)
  const uint32_t meta_offset = bw_.tell();
  {
    uint8_t lenbuf[2];
    cmb_write_u16(lenbuf, static_cast<uint16_t>(metadata_title.size()));
    if (!bw_.write(lenbuf, sizeof(lenbuf))) return false;
    if (!metadata_title.empty() && !bw_.write(metadata_title.data(), metadata_title.size())) return false;

    cmb_write_u16(lenbuf, static_cast<uint16_t>(metadata_author.size()));
    if (!bw_.write(lenbuf, sizeof(lenbuf))) return false;
    if (!metadata_author.empty() && !bw_.write(metadata_author.data(), metadata_author.size())) return false;

    uint8_t toc_count[2];
    cmb_write_u16(toc_count, 0);
    if (!bw_.write(toc_count, sizeof(toc_count))) return false;
  }

  // ---- patch header at offset 0 ----
  CmbHeader h{};
  std::memcpy(h.magic, kCmbMagic, sizeof(kCmbMagic));
  h.version = kCmbVersion;
  h.flags = 0;
  h.paragraph_count = paragraph_count_;
  h.chapter_count = static_cast<uint16_t>(chapters_.size());
  h.image_count = static_cast<uint16_t>(images_.size());
  h.anchor_offset = anchor_offset;
  h.chapter_offset = chapter_offset;
  h.image_offset = image_offset;
  h.meta_offset = meta_offset;

  uint8_t header_bytes[32];
  cmb_write_header(header_bytes, h);

  if (!bw_.seek(0)) return false;
  if (!bw_.write(header_bytes, sizeof(header_bytes))) return false;
  if (!bw_.flush()) return false;

  // Don't seek back to end -- caller is expected to close() next.
  return true;
}

}  // namespace cmb
