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
    CMB_EXPECT(w.finish("Title", "Author", {}));
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
    const std::vector<std::string> spine = {"OEBPS/c0.xhtml", "OEBPS/c1.xhtml", "OEBPS/c2.xhtml"};
    CMB_EXPECT(w.finish("Round Trip", "Test Author", spine));
  }

  cmb::CmbReader r;
  CMB_EXPECT(r.open(path));
  CMB_EXPECT_EQ(r.chapter_count(), static_cast<uint16_t>(chapters.size()));

  // v2 spine file round-trip.
  CMB_EXPECT_EQ(r.spine_files().size(), static_cast<size_t>(3));
  CMB_EXPECT_EQ(r.spine_files()[0], std::string("OEBPS/c0.xhtml"));
  CMB_EXPECT_EQ(r.spine_files()[1], std::string("OEBPS/c1.xhtml"));
  CMB_EXPECT_EQ(r.spine_files()[2], std::string("OEBPS/c2.xhtml"));

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
    CMB_EXPECT(w.finish("Mixed Blocks", "Author", {"OEBPS/single.xhtml"}));
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

void test_styled_paragraphs(const char* path) {
  std::cout << "[test] styled paragraphs (runs + heading + alignment + anchor)\n";
  {
    cmb::CmbWriter w;
    CMB_EXPECT(w.open(path));

    w.begin_chapter();

    // Paragraph 1: a heading with an anchor id and centered alignment.
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockText;
      p.text = "Chapter One";
      p.heading_level = 1;
      p.alignment = cmb::kCmbAlignCenter;
      p.anchor_id = "ch1";
      CMB_EXPECT(w.write_paragraph(p));
    }

    // Paragraph 2: body text with mixed bold + italic runs and a
    // trailing left-aligned setting.
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockText;
      p.text = "The quick brown fox jumps over the lazy dog.";
      p.alignment = cmb::kCmbAlignLeft;
      // "quick" (offset 4, length 5) -- bold
      p.runs.push_back({4, 5, cmb::kCmbStyleBold});
      // "brown fox" (offset 10, length 9) -- italic
      p.runs.push_back({10, 9, cmb::kCmbStyleItalic});
      // "lazy" (offset 35, length 4) -- bold + italic + underline
      p.runs.push_back({35, 4, static_cast<uint8_t>(cmb::kCmbStyleBold | cmb::kCmbStyleItalic | cmb::kCmbStyleUnderline)});
      CMB_EXPECT(w.write_paragraph(p));
    }

    // Paragraph 3: image block with an anchor id.
    const uint16_t img_key = w.add_image_ref(0x4000, 100, 150);
    {
      cmb::CmbParagraph p;
      p.type = cmb::kCmbBlockImage;
      p.image_key = img_key;
      p.anchor_id = "cover";
      CMB_EXPECT(w.write_paragraph(p));
    }

    w.end_chapter();
    CMB_EXPECT(w.finish("Styled", "Tester", {"OEBPS/ch.xhtml"}));
  }

  cmb::CmbReader r;
  CMB_EXPECT(r.open(path));
  CMB_EXPECT_EQ(r.chapter_count(), 1u);
  CMB_EXPECT_EQ(r.chapter_paragraph_count(0), 3u);

  cmb::CmbParagraph p;
  // Paragraph 1 -- heading.
  CMB_EXPECT(r.load_paragraph(0, 0, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockText));
  CMB_EXPECT_EQ(p.text, std::string("Chapter One"));
  CMB_EXPECT_EQ(static_cast<int>(p.heading_level), 1);
  CMB_EXPECT_EQ(static_cast<int>(p.alignment), static_cast<int>(cmb::kCmbAlignCenter));
  CMB_EXPECT_EQ(p.anchor_id, std::string("ch1"));
  CMB_EXPECT_EQ(p.runs.size(), 0u);

  // Paragraph 2 -- styled body.
  CMB_EXPECT(r.load_paragraph(0, 1, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockText));
  CMB_EXPECT_EQ(p.text, std::string("The quick brown fox jumps over the lazy dog."));
  CMB_EXPECT_EQ(static_cast<int>(p.heading_level), 0);
  CMB_EXPECT_EQ(static_cast<int>(p.alignment), static_cast<int>(cmb::kCmbAlignLeft));
  CMB_EXPECT_EQ(p.anchor_id, std::string(""));
  CMB_EXPECT_EQ(p.runs.size(), 3u);
  CMB_EXPECT_EQ(p.runs[0].start, 4u);
  CMB_EXPECT_EQ(p.runs[0].length, 5u);
  CMB_EXPECT_EQ(static_cast<int>(p.runs[0].style), static_cast<int>(cmb::kCmbStyleBold));
  CMB_EXPECT_EQ(p.runs[1].start, 10u);
  CMB_EXPECT_EQ(p.runs[1].length, 9u);
  CMB_EXPECT_EQ(static_cast<int>(p.runs[1].style), static_cast<int>(cmb::kCmbStyleItalic));
  CMB_EXPECT_EQ(p.runs[2].start, 35u);
  CMB_EXPECT_EQ(p.runs[2].length, 4u);
  CMB_EXPECT_EQ(static_cast<int>(p.runs[2].style),
                static_cast<int>(cmb::kCmbStyleBold | cmb::kCmbStyleItalic | cmb::kCmbStyleUnderline));

  // Paragraph 3 -- image with anchor.
  CMB_EXPECT(r.load_paragraph(0, 2, p));
  CMB_EXPECT_EQ(p.type, static_cast<uint8_t>(cmb::kCmbBlockImage));
  CMB_EXPECT_EQ(p.image_key, 0u);
  CMB_EXPECT_EQ(p.anchor_id, std::string("cover"));
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
  test_styled_paragraphs(tmp_path);
  test_magic_rejection(tmp_path);

  std::remove(tmp_path);

  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURE(S)\n";
  return 1;
}
