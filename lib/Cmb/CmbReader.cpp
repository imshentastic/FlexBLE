#include "CmbReader.h"

#include <cstdio>
#include <cstring>

namespace cmb {

namespace {

// Small wrapper around fread that treats partial reads as failures --
// every read in the .cmb format is fixed-size and known, so a short
// read indicates either EOF or a corrupted file. Either way the
// caller should bail.
inline bool read_exact(FILE* f, void* buf, size_t size) {
  return std::fread(buf, 1, size, f) == size;
}

inline bool seek_to(FILE* f, uint32_t offset) {
  return std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0;
}

}  // namespace

bool CmbReader::open(const char* path) {
  if (f_ != nullptr) close();
  f_ = std::fopen(path, "rb");
  if (f_ == nullptr) return false;

  // ---- header ----
  uint8_t header_bytes[32];
  if (!read_exact(f_, header_bytes, sizeof(header_bytes))) {
    close();
    return false;
  }
  if (!cmb_magic_matches(header_bytes)) {
    close();
    return false;
  }
  cmb_read_header(header_bytes, header_);

  // For forward-compat with v2+: a future reader that wants to parse
  // a v1 file is fine. The other direction -- v1 reader seeing a
  // higher version -- is rejected for now since the v1->v2 changes
  // may add fields the v1 reader can't interpret correctly. When the
  // companion task lands and we bump to v2, this check loosens to
  // "version <= kCmbVersion".
  if (header_.version != kCmbVersion) {
    close();
    return false;
  }

  // ---- chapter table ----
  if (header_.chapter_count > 0) {
    if (!seek_to(f_, header_.chapter_offset)) {
      close();
      return false;
    }
    chapters_.resize(header_.chapter_count);
    for (size_t i = 0; i < chapters_.size(); ++i) {
      uint8_t buf[16];
      if (!read_exact(f_, buf, sizeof(buf))) {
        close();
        return false;
      }
      CmbChapterEntry& c = chapters_[i];
      c.para_table_offset = cmb_read_u32(buf);
      c.reserved0 = cmb_read_u32(buf + 4);
      c.paragraph_count = cmb_read_u16(buf + 8);
      c.reserved1 = cmb_read_u16(buf + 10);
      c.char_count = cmb_read_u32(buf + 12);
    }
  }

  // ---- image ref table ----
  if (header_.image_count > 0) {
    if (!seek_to(f_, header_.image_offset)) {
      close();
      return false;
    }
    images_.resize(header_.image_count);
    for (size_t i = 0; i < images_.size(); ++i) {
      uint8_t buf[8];
      if (!read_exact(f_, buf, sizeof(buf))) {
        close();
        return false;
      }
      CmbImageRef& r = images_[i];
      r.local_header_offset = cmb_read_u32(buf);
      r.width = cmb_read_u16(buf + 4);
      r.height = cmb_read_u16(buf + 6);
    }
  }

  // ---- metadata blob ----
  // Title + author only for v1. TOC entries are reserved at the end
  // of the blob but unused by the reader at this checkpoint.
  if (!seek_to(f_, header_.meta_offset)) {
    close();
    return false;
  }
  auto read_lp_string = [this](std::string& out) {
    uint8_t lenbuf[2];
    if (!read_exact(f_, lenbuf, sizeof(lenbuf))) return false;
    const uint16_t len = cmb_read_u16(lenbuf);
    out.resize(len);
    if (len > 0 && !read_exact(f_, out.data(), len)) {
      out.clear();
      return false;
    }
    return true;
  };
  if (!read_lp_string(metadata_title_) || !read_lp_string(metadata_author_)) {
    close();
    return false;
  }

  return true;
}

void CmbReader::close() {
  if (f_ != nullptr) {
    std::fclose(f_);
    f_ = nullptr;
  }
  header_ = CmbHeader{};
  chapters_.clear();
  images_.clear();
  metadata_title_.clear();
  metadata_author_.clear();
}

uint16_t CmbReader::chapter_paragraph_count(uint16_t chapter_idx) const {
  if (chapter_idx >= chapters_.size()) return 0;
  return chapters_[chapter_idx].paragraph_count;
}

uint32_t CmbReader::chapter_char_count(uint16_t chapter_idx) const {
  if (chapter_idx >= chapters_.size()) return 0;
  return chapters_[chapter_idx].char_count;
}

bool CmbReader::image_ref(uint16_t image_idx, CmbImageRef& out) const {
  if (image_idx >= images_.size()) return false;
  out = images_[image_idx];
  return true;
}

bool CmbReader::load_paragraph(uint16_t chapter_idx, uint16_t para_idx, CmbParagraph& out) {
  if (f_ == nullptr) return false;
  if (chapter_idx >= chapters_.size()) return false;
  const CmbChapterEntry& c = chapters_[chapter_idx];
  if (para_idx >= c.paragraph_count) return false;

  // ---- read the per-chapter descriptor for this paragraph (8 bytes
  // on SD; NOT cached in RAM) ----
  const uint32_t desc_offset = c.para_table_offset + static_cast<uint32_t>(para_idx) * sizeof(CmbParaDescriptor);
  if (!seek_to(f_, desc_offset)) return false;
  uint8_t desc_buf[8];
  if (!read_exact(f_, desc_buf, sizeof(desc_buf))) return false;
  const uint32_t file_offset = cmb_read_u32(desc_buf);
  // char_offset is in desc_buf+4 but unused for paragraph fetch;
  // exposed as a separate query if/when callers need it.

  return read_paragraph_at(file_offset, out);
}

bool CmbReader::read_paragraph_at(uint32_t file_offset, CmbParagraph& out) {
  if (!seek_to(f_, file_offset)) return false;

  uint8_t envelope[3];
  if (!read_exact(f_, envelope, sizeof(envelope))) return false;
  out.type = envelope[0];
  const uint16_t payload_len = cmb_read_u16(envelope + 1);

  // Default-init payload fields so unknown / empty types come back
  // in a defined state.
  out.text.clear();
  out.image_key = kCmbNoImage;

  switch (out.type) {
    case kCmbBlockText: {
      if (payload_len < 4) return false;
      uint8_t hdr[4];
      if (!read_exact(f_, hdr, sizeof(hdr))) return false;
      const uint32_t text_len = cmb_read_u32(hdr);
      if (text_len + 4 > payload_len) return false;
      out.text.resize(text_len);
      if (text_len > 0 && !read_exact(f_, out.text.data(), text_len)) {
        out.text.clear();
        return false;
      }
      break;
    }
    case kCmbBlockImage: {
      if (payload_len < 2) return false;
      uint8_t hdr[2];
      if (!read_exact(f_, hdr, sizeof(hdr))) return false;
      out.image_key = cmb_read_u16(hdr);
      break;
    }
    case kCmbBlockHr:
    case kCmbBlockPageBreak:
      // No payload; envelope was enough.
      break;
    default:
      // Unknown tag -- v1 reader is allowed to either bail or skip.
      // For now we report success with type set to the unknown tag
      // value so the caller can decide. The length prefix would let
      // us advance past it if needed.
      break;
  }
  return true;
}

}  // namespace cmb
