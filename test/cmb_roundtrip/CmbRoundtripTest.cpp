// Host-side round-trip test for the .cmb format library (CmbWriter +
// CmbReader). Synthesises a small in-memory book, writes to a temp
// file, reads it back, and verifies every accessible field matches.
//
// Builds without the rest of the firmware -- no ESP-IDF, no
// EpubReaderActivity, no Epub class. Just the format library +
// stdlib. Run via test/run_cmb_roundtrip_test.sh.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Cmb/CmbFormat.h"
#include "Cmb/CmbReader.h"
#include "Cmb/CmbWriter.h"

namespace {

int g_failures = 0;

#define CMB_EXPECT(cond)                                                                                              \
  do {                                                                                                                \
    if (!(cond)) {                                                                                                    \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #cond << "\n";                                  \
      ++g_failures;                                                                                                   \
    }                                                                                                                 \
  } while (0)

#define CMB_EXPECT_EQ(a, b)                                                                                           \
  do {                                                                                                                \
    auto _av = (a);                                                                                                   \
    auto _bv = (b);                                                                                                   \
    if (!(_av == _bv)) {                                                                                              \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " << #a << " (=" << _av << ") != " << #b             \
                << " (=" << _bv << ")\n";                                                                             \
      ++g_failures;                                                                                                   \
    }                                                                                                                 \
  } while (0)

void test_empty_book(const char* path) {
  std::cout << "[test] empty book (0 chapters)\n";
  {
    cmb::CmbWriter w;
    CMB_EXPECT(w.open(path));
    CMB_EXPECT(w.finish("Title", "Author"));
  }
  cmb::CmbReader r;
  CMB_EXPECT(r.open(path));
  CMB_EXPECT_EQ(r.chapter_count(), 0u);
  CMB_EXPECT_EQ(r.image_count(), 0u);
  CMB_EXPECT_EQ(r.paragraph_count(), 0u);
  CMB_EXPECT_EQ(r.metadata_title(), std::string("Title"));
  CMB_EXPECT_EQ(r.metadata_author(), std::string("Author"));
}

void test_text_paragraphs(const char* path) {
  std::cout << "[test] 3 chapters, varied text\n";
  // Fixture: each chapter has a different number of paragraphs to
  // stress the per-chapter descriptor table.
  const std::vector<std::vector<std::string>> chapters = {
      {"First paragraph of chapter zero.", "Second paragraph."},
      {"Only paragraph in chapter one."},
      {"Three", "paragraphs", "in chapter two with longer text content."},
  };

  {
    cmb::CmbWriter w;
    CMB_EXPECT(w.open(path));
    for (const auto& chapter : chapters) {
      w.begin_chapter();
      for (const auto& p_text : chapter) {
        cmb::CmbParagraph p;
        p.type = cmb::kCmbBlockText;
        p.text = p_text;
        CMB_EXPECT(w.write_paragraph(p));
      }
      w.end_chapter();
    }
    CMB_EXPECT(w.finish("Round Trip", "Test Author"));
  }

  cmb::CmbReader r;
  CMB_EXPECT(r.open(path));
  CMB_EXPECT_EQ(r.chapter_count(), static_cast<uint16_t>(chapters.size()));

  uint32_t expected_total = 0;
  for (size_t ci = 0; ci < chapters.size(); ++ci) {
    expected_total += static_cast<uint32_t>(chapters[ci].size());
    CMB_EXPECT_EQ(r.chapter_paragraph_count(static_cast<uint16_t>(ci)),
                  static_cast<uint16_t>(chapters[ci].size()));

    // char_count should be sum of UTF-8 byte lengths of every text
    // paragraph in the chapter.
    uint32_t expected_chars = 0;
    for (const auto& p_text : chapters[ci]) expected_chars += static_cast<uint32_t>(p_text.size());
    CMB_EXPECT_EQ(r.chapter_char_count(static_cast<uint16_t>(ci)), expected_chars);

    // Load each paragraph back and verify text content.
    for (size_t pi = 0; pi < chapters[ci].size(); ++pi) {
      cmb::CmbParagraph out;
      CMB_EXPECT(r.load_paragraph(static_cast<uint16_t>(ci), static_cast<uint16_t>(pi), out));
      CMB_EXPECT_EQ(out.type, static_cast<uint8_t>(cmb::kCmbBlockText));
      CMB_EXPECT_EQ(out.text, chapters[ci][pi]);
    }
  }
  CMB_EXPECT_EQ(r.paragraph_count(), expected_total);
}

void test_image_refs_and_blocks(const char* path) {
  std::cout << "[test] image refs + image blocks + hr + page break\n";
  {
    cmb::CmbWriter w;
    CMB_EXPECT(w.open(path));
    // Pre-register two images. Third add of the same (offset,w,h)
    // tuple should dedupe to the same key.
    const uint16_t k0 = w.add_image_ref(0x1000, 100, 150);
    const uint16_t k1 = w.add_image_ref(0x2000, 200, 320);
    const uint16_t k0_again = w.add_image_ref(0x1000, 100, 150);
    CMB_EXPECT_EQ(k0, 0u);
    CMB_EXPECT_EQ(k1, 1u);
    CMB_EXPECT_EQ(k0_again, 0u);  // dedup

    w.begin_chapter();
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockText;
      p.text = "Text before image";
      CMB_EXPECT(w.write_paragraph(p));
    }
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockImage;
      p.image_key = k0;
      CMB_EXPECT(w.write_paragraph(p));
    }
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockHr;
      CMB_EXPECT(w.write_paragraph(p));
    }
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockPageBreak;
      CMB_EXPECT(w.write_paragraph(p));
    }
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockImage;
      p.image_key = k1;
      CMB_EXPECT(w.write_paragraph(p));
    }
    w.end_chapter();
    CMB_EXPECT(w.finish("Mixed Blocks", "Author"));
  }

  cmb::CmbReader r;
  CMB_EXPECT(r.open(path));
  CMB_EXPECT_EQ(r.chapter_count(), 1u);
  CMB_EXPECT_EQ(r.image_count(), 2u);
  CMB_EXPECT_EQ(r.chapter_paragraph_count(0), 5u);

  cmb::CmbImageRef img;
  CMB_EXPECT(r.image_ref(0, img));
  CMB_EXPECT_EQ(img.local_header_offset, 0x1000u);
  CMB_EXPECT_EQ(img.width, 100u);
  CMB_EXPECT_EQ(img.height, 150u);
  CMB_EXPECT(r.image_ref(1, img));
  CMB_EXPECT_EQ(img.local_header_offset, 0x2000u);
  CMB_EXPECT_EQ(img.width, 200u);
  CMB_EXPECT_EQ(img.height, 320u);

  cmb::CmbParagraph p;
  CMB_EXPECT(r.load_paragraph(0, 0, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockText));
  CMB_EXPECT_EQ(p.text, std::string("Text before image"));

  CMB_EXPECT(r.load_paragraph(0, 1, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockImage));
  CMB_EXPECT_EQ(p.image_key, 0u);

  CMB_EXPECT(r.load_paragraph(0, 2, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockHr));

  CMB_EXPECT(r.load_paragraph(0, 3, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockPageBreak));

  CMB_EXPECT(r.load_paragraph(0, 4, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockImage));
  CMB_EXPECT_EQ(p.image_key, 1u);
}

void test_magic_rejection(const char* path) {
  std::cout << "[test] reader rejects non-cmb files\n";
  // Write a file that starts with the wrong magic.
  FILE* f = std::fopen(path, "wb");
  if (f == nullptr) {
    std::cerr << "could not open " << path << " for fixture write\n";
    ++g_failures;
    return;
  }
  const char garbage[] = "NOTCMB1.....................";
  std::fwrite(garbage, 1, sizeof(garbage), f);
  std::fclose(f);

  cmb::CmbReader r;
  // open() should return false (magic mismatch). is_open() should be
  // false afterward.
  CMB_EXPECT(!r.open(path));
  CMB_EXPECT(!r.is_open());
}

}  // namespace

int main(int argc, char** argv) {
  const char* tmp_path = (argc > 1) ? argv[1] : "/tmp/cmb_roundtrip_test.bin";

  test_empty_book(tmp_path);
  test_text_paragraphs(tmp_path);
  test_image_refs_and_blocks(tmp_path);
  test_magic_rejection(tmp_path);

  std::remove(tmp_path);

  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURE(S)\n";
  return 1;
}
