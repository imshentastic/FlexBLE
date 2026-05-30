#pragma once

// CMB (CrumBLE Book) -- pre-processed single-file binary format for fast
// cold book-open + reduced heap pressure under BLE. Companion to .pxc
// (which caches pre-rendered page bitmaps); .cmb caches the upstream
// stuff -- chapter index, paragraph data, image refs, anchors, metadata.
//
// Designed by CrumBLE; format owned by us. Inspired by the patterns in
// microreader's MrbFormat.h (CidVonHighwind/microreader) -- chapter
// table in RAM with per-paragraph index seeked on demand, image refs
// stored as ZIP local-header offsets so the reader can pull image
// bytes from the original EPUB without walking the ZIP central
// directory. We do NOT copy their bytes; magic is "CMB1", versioning
// is ours.
//
// File layout (all multi-byte values little-endian):
//
//   [Header 32 bytes]
//   [Per-chapter content blobs ............ variable]
//   [Per-chapter descriptor tables ........ chapter_count x ND entries]
//   [Chapter table ........................ chapter_count x 16 bytes]
//   [Image ref table ...................... image_count   x  8 bytes]
//   [Anchor table ......................... variable]
//   [Metadata + TOC blobs ................. variable]
//
// Per-chapter content blob layout (inside the byte range a chapter's
// table entry points at): a series of TAGGED records, each prefixed by
// a 1-byte type tag + a 2-byte length-of-payload, followed by that many
// bytes of type-specific data. Type tags are extensible -- new block
// kinds (page break, footnote, etc.) can be added in future versions
// without breaking older readers (they SKIP unknown tags using the
// length prefix). See kCmbBlockType* constants below.
//
// Per-chapter descriptor table (written immediately after the chapter's
// last paragraph blob):
//   N x { file_offset(u32), char_offset(u32) }
//   - file_offset: absolute byte position of the Nth block record in
//     the file. Allows O(1) random access to any paragraph + accurate
//     char-based reading progress without reading any block data.
//   - char_offset: cumulative UTF-8 bytes of text BEFORE this block.
//
// The per-chapter descriptor table is INDEXED in the chapter table
// (para_table_offset) but is NOT loaded into RAM on open() -- the
// reader seeks the right table entry on demand. This is the win that
// keeps a 377-spine-item book's open() cost at ~5 KB instead of
// ~136 KB.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace cmb {

// ---------------------------------------------------------------------------
// Magic + version
// ---------------------------------------------------------------------------
// "CMB1" intentionally avoids "MRB" (microreader) and "CPB" (CrossPoint
// generic) collisions. The trailing digit is the format generation; the
// 16-bit version field below tracks per-generation revisions.

inline constexpr uint8_t kCmbMagic[4] = {'C', 'M', 'B', '1'};

// Version history:
//   1  -- initial layout (header + chapter table + image refs + anchors
//         + metadata). Per-chapter content is opaque blob.
//   2  -- (planned) adds cover_thumb_offset + thumb table for pre-baked
//         cover BMPs at all device-rendered sizes. See companion task
//         "Pre-bake cover thumbs inside .cmb file".
inline constexpr uint16_t kCmbVersion = 1;

// ---------------------------------------------------------------------------
// Header (32 bytes, fixed)
// ---------------------------------------------------------------------------
//
// Designed at exactly 32 bytes so static_assert below catches accidental
// drift on any compiler. Any future fields will land in a fresh
// extension region pointed to by meta_offset until a version bump
// justifies extending the fixed header.

struct CmbHeader {
  uint8_t magic[4];           // "CMB1"
  uint16_t version;           // kCmbVersion
  uint16_t flags;             // reserved for future use; writers SHOULD set to 0
  uint32_t paragraph_count;   // total tagged-block records across all chapters
  uint16_t chapter_count;
  uint16_t image_count;
  uint32_t anchor_offset;     // file offset of anchor table (id -> para_index)
  uint32_t chapter_offset;    // file offset of chapter table
  uint32_t image_offset;      // file offset of image ref table
  uint32_t meta_offset;       // file offset of metadata + TOC blob region
};
static_assert(sizeof(CmbHeader) == 32, "CmbHeader must be exactly 32 bytes");

// ---------------------------------------------------------------------------
// Chapter table entry (16 bytes each)
// ---------------------------------------------------------------------------

struct CmbChapterEntry {
  uint32_t para_table_offset;  // file offset of the per-chapter descriptor table
  uint32_t reserved0;          // unused, write 0
  uint16_t paragraph_count;    // number of tagged-block records in this chapter
  uint16_t reserved1;          // unused, write 0
  uint32_t char_count;         // total UTF-8 bytes of text in the chapter
};
static_assert(sizeof(CmbChapterEntry) == 16, "CmbChapterEntry must be exactly 16 bytes");

// ---------------------------------------------------------------------------
// Image reference entry (8 bytes each)
// ---------------------------------------------------------------------------
//
// width / height are EPUB-declared (from <img width="..." height="...">
// or equivalent). Most EPUBs don't declare them, in which case the
// writer leaves them at 0 and the reader resolves the actual dimensions
// at display time by seeking to local_header_offset in the EPUB ZIP and
// reading ~30 bytes from the local file header (no central-directory
// walk required).

struct CmbImageRef {
  uint32_t local_header_offset;  // offset to local file header in original EPUB ZIP
  uint16_t width;                // EPUB-declared width, or 0 if unknown
  uint16_t height;               // EPUB-declared height, or 0 if unknown
};
static_assert(sizeof(CmbImageRef) == 8, "CmbImageRef must be exactly 8 bytes");

// ---------------------------------------------------------------------------
// Per-chapter descriptor entry (8 bytes each; NOT loaded into RAM)
// ---------------------------------------------------------------------------

struct CmbParaDescriptor {
  uint32_t file_offset;  // absolute file offset of this block record
  uint32_t char_offset;  // cumulative UTF-8 bytes of text before this block
};
static_assert(sizeof(CmbParaDescriptor) == 8, "CmbParaDescriptor must be exactly 8 bytes");

// ---------------------------------------------------------------------------
// Block-type tags (used inside the per-chapter content blob)
// ---------------------------------------------------------------------------
//
// Each tagged record is { type(u8), payload_length(u16), payload(u8 x len) }.
// Unknown types MUST be skipped by readers using the length prefix --
// this is the forward-compatibility hook.

inline constexpr uint8_t kCmbBlockText = 0;        // paragraph of text + styling
inline constexpr uint8_t kCmbBlockImage = 1;       // image reference (idx into image table)
inline constexpr uint8_t kCmbBlockHr = 2;          // horizontal rule
inline constexpr uint8_t kCmbBlockPageBreak = 3;   // hard page break
// 4..127 reserved for future standard types
// 128..255 reserved for vendor extensions

// ---------------------------------------------------------------------------
// Sentinel values for optional fields
// ---------------------------------------------------------------------------

inline constexpr uint8_t kCmbAlignDefault = 0xFF;
inline constexpr int16_t kCmbIndentNone = 0x7FFF;
inline constexpr uint16_t kCmbSpacingDefault = 0xFFFF;
inline constexpr uint16_t kCmbNoImage = 0xFFFF;

// ---------------------------------------------------------------------------
// Little-endian serialization helpers
// ---------------------------------------------------------------------------
//
// We never trust the compiler's endianness or struct layout for the
// on-disk format. Writers go through these helpers byte-by-byte;
// readers do the same on the way in. Inlined so the cost is identical
// to a direct cast on a little-endian host.

inline void cmb_write_u8(uint8_t* dst, uint8_t v) { dst[0] = v; }

inline void cmb_write_u16(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
}

inline void cmb_write_i16(uint8_t* dst, int16_t v) { cmb_write_u16(dst, static_cast<uint16_t>(v)); }

inline void cmb_write_u32(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
  dst[2] = static_cast<uint8_t>(v >> 16);
  dst[3] = static_cast<uint8_t>(v >> 24);
}

inline uint8_t cmb_read_u8(const uint8_t* src) { return src[0]; }

inline uint16_t cmb_read_u16(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}

inline int16_t cmb_read_i16(const uint8_t* src) { return static_cast<int16_t>(cmb_read_u16(src)); }

inline uint32_t cmb_read_u32(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

// Header pack / unpack -- writes the 32-byte fixed header out as bytes
// using the LE helpers above. Use these in CmbWriter::finish() and
// CmbReader::open() instead of memcpy'ing the struct (which would
// depend on compiler-specific padding).

inline void cmb_write_header(uint8_t out[32], const CmbHeader& h) {
  std::memcpy(out, h.magic, 4);
  cmb_write_u16(out + 4, h.version);
  cmb_write_u16(out + 6, h.flags);
  cmb_write_u32(out + 8, h.paragraph_count);
  cmb_write_u16(out + 12, h.chapter_count);
  cmb_write_u16(out + 14, h.image_count);
  cmb_write_u32(out + 16, h.anchor_offset);
  cmb_write_u32(out + 20, h.chapter_offset);
  cmb_write_u32(out + 24, h.image_offset);
  cmb_write_u32(out + 28, h.meta_offset);
}

inline void cmb_read_header(const uint8_t in[32], CmbHeader& h) {
  std::memcpy(h.magic, in, 4);
  h.version = cmb_read_u16(in + 4);
  h.flags = cmb_read_u16(in + 6);
  h.paragraph_count = cmb_read_u32(in + 8);
  h.chapter_count = cmb_read_u16(in + 12);
  h.image_count = cmb_read_u16(in + 14);
  h.anchor_offset = cmb_read_u32(in + 16);
  h.chapter_offset = cmb_read_u32(in + 20);
  h.image_offset = cmb_read_u32(in + 24);
  h.meta_offset = cmb_read_u32(in + 28);
}

// Returns true iff the four magic bytes match "CMB1". Use this BEFORE
// trusting any other header field -- defends against opening a file
// that happens to start with parseable bytes but isn't a .cmb at all.
inline bool cmb_magic_matches(const uint8_t magic[4]) {
  return magic[0] == 'C' && magic[1] == 'M' && magic[2] == 'B' && magic[3] == '1';
}

// ---------------------------------------------------------------------------
// In-memory paragraph representation
// ---------------------------------------------------------------------------
//
// Layout-independent: text is UTF-8 with no embedded line breaks (the
// reader runs layout at chapter-load time). Style information starts
// minimal -- text, optional inline image ref, paragraph type -- and
// will grow over follow-up commits (style runs for bold/italic,
// alignment, heading level, anchor ids for in-book links). Encoded
// on disk inside a chapter's content blob as a tagged record:
//
//   {type:u8, payload_length:u16, payload:bytes[payload_length]}
//
// Where the payload layout depends on the type tag. Readers must
// SKIP unknown type tags using the length prefix -- this is what
// keeps v1 readers forward-compatible with future block kinds.
//
// Payload layouts (subject to extension in subsequent commits):
//
//   kCmbBlockText:
//     text_len:u32
//     text:bytes[text_len]      -- UTF-8
//
//   kCmbBlockImage:
//     image_key:u16             -- index into image ref table
//
//   kCmbBlockHr, kCmbBlockPageBreak:
//     (empty payload)

struct CmbParagraph {
  uint8_t type = kCmbBlockText;
  // For kCmbBlockText: UTF-8 paragraph contents. Empty for non-text.
  std::string text;
  // For kCmbBlockImage: index into the file's image-ref table.
  // kCmbNoImage when not an image block.
  uint16_t image_key = kCmbNoImage;
};

}  // namespace cmb
