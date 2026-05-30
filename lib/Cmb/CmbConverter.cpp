#include "CmbConverter.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expat.h>
#include <string>
#include <string_view>
#include <vector>

#include "CmbWriter.h"

namespace cmb {

namespace {

// ===========================================================================
// XhtmlParagraphWalker
// ===========================================================================
//
// Streams XHTML through expat and emits one CmbParagraph per
// paragraph-boundary tag (`<p>`, `<div>`, `<h1>` .. `<h6>`, `<li>`,
// `<blockquote>`) plus on `<br/>` boundaries. Inline-style tags
// (`<b>` / `<strong>`, `<i>` / `<em>`, `<u>`, `<s>` / `<strike>`)
// build style runs inside the current paragraph; the runs persist
// inside `CmbParagraph::runs` as non-overlapping `{start, length, mask}`
// triples.
//
// Heading level is captured from the source tag name: `<h1>` -> 1,
// `<h2>` -> 2, etc. Anchor ids are captured from the first `id`
// attribute encountered within a paragraph (paragraph-boundary tag
// OR a wrapping `<span>` / `<a>`).
//
// `<img>` and other media tags are deliberately ignored in this
// commit -- they need an image ref resolver pass (to look up the
// EPUB ZIP entry, get the local-file-header offset, register in the
// .cmb image table) that arrives in the next checkpoint.
//
// The output sink is a std::function<bool(const CmbParagraph&)> that
// returns false to stop the parse (e.g. on writer I/O failure).
class XhtmlParagraphWalker {
 public:
  using Sink = bool (*)(void* ctx, const CmbParagraph&);

  XhtmlParagraphWalker(Sink sink, void* ctx) : sink_(sink), ctx_(ctx) {}

  // Feed a chunk of XHTML bytes through expat. Returns false on
  // expat parse error or sink-rejection; partial state from previous
  // chunks is retained across calls.
  bool feed(const char* data, size_t size, bool is_final) {
    if (parser_ == nullptr) {
      parser_ = XML_ParserCreate(nullptr);
      if (parser_ == nullptr) return false;
      XML_SetUserData(parser_, this);
      XML_SetElementHandler(parser_, &XhtmlParagraphWalker::start_thunk, &XhtmlParagraphWalker::end_thunk);
      XML_SetCharacterDataHandler(parser_, &XhtmlParagraphWalker::chars_thunk);
    }
    if (XML_Parse(parser_, data, static_cast<int>(size), is_final ? 1 : 0) == XML_STATUS_ERROR) {
      return false;
    }
    return !sink_failed_;
  }

  // Emit any leftover paragraph the walker has buffered (e.g. when
  // the source XHTML doesn't close its last <p> before EOF).
  bool flush() {
    return emit_pending();
  }

  ~XhtmlParagraphWalker() {
    if (parser_ != nullptr) XML_ParserFree(parser_);
  }

 private:
  // ---- expat thunks ----

  static void XMLCALL start_thunk(void* ud, const XML_Char* name, const XML_Char** atts) {
    static_cast<XhtmlParagraphWalker*>(ud)->on_start(name, atts);
  }
  static void XMLCALL end_thunk(void* ud, const XML_Char* name) {
    static_cast<XhtmlParagraphWalker*>(ud)->on_end(name);
  }
  static void XMLCALL chars_thunk(void* ud, const XML_Char* text, int len) {
    static_cast<XhtmlParagraphWalker*>(ud)->on_chars(text, len);
  }

  // ---- tag classification ----

  static bool tag_eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

  static bool is_paragraph_boundary(const char* name) {
    return tag_eq(name, "p") || tag_eq(name, "div") || tag_eq(name, "li") || tag_eq(name, "blockquote") ||
           tag_eq(name, "h1") || tag_eq(name, "h2") || tag_eq(name, "h3") || tag_eq(name, "h4") ||
           tag_eq(name, "h5") || tag_eq(name, "h6");
  }
  static uint8_t heading_level_for_tag(const char* name) {
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') {
      return static_cast<uint8_t>(name[1] - '0');
    }
    return 0;
  }
  static bool is_bold_tag(const char* name) { return tag_eq(name, "b") || tag_eq(name, "strong"); }
  static bool is_italic_tag(const char* name) { return tag_eq(name, "i") || tag_eq(name, "em"); }
  static bool is_underline_tag(const char* name) { return tag_eq(name, "u") || tag_eq(name, "ins"); }
  static bool is_strike_tag(const char* name) { return tag_eq(name, "s") || tag_eq(name, "strike") || tag_eq(name, "del"); }
  static bool is_hard_break_tag(const char* name) { return tag_eq(name, "br"); }

  // ---- event handlers ----

  void on_start(const char* name, const XML_Char** atts) {
    // Capture id attribute if the current paragraph doesn't already
    // have one. First-id-wins keeps the behaviour stable for nested
    // wrappers like <a id="..."> <span ...>...</span> </a>.
    if (current_anchor_.empty()) {
      for (int i = 0; atts && atts[i]; i += 2) {
        if (tag_eq(atts[i], "id")) {
          current_anchor_ = atts[i + 1];
          break;
        }
      }
    }

    if (is_paragraph_boundary(name)) {
      // Boundary opens a new paragraph -- flush whatever was in
      // progress (handles the unclosed-<p> + <p>-as-sibling pattern).
      emit_pending();
      pending_heading_ = heading_level_for_tag(name);
      return;
    }
    if (is_hard_break_tag(name)) {
      emit_pending();
      return;
    }
    if (is_bold_tag(name)) {
      ++bold_depth_;
      return;
    }
    if (is_italic_tag(name)) {
      ++italic_depth_;
      return;
    }
    if (is_underline_tag(name)) {
      ++underline_depth_;
      return;
    }
    if (is_strike_tag(name)) {
      ++strike_depth_;
      return;
    }
    // <img>, <hr>, etc. are intentionally not handled here -- left for
    // a follow-up image-ref + block emitter that needs more EPUB
    // context than the walker has.
  }

  void on_end(const char* name) {
    if (is_paragraph_boundary(name)) {
      emit_pending();
      return;
    }
    if (is_bold_tag(name)) {
      if (bold_depth_ > 0) --bold_depth_;
      return;
    }
    if (is_italic_tag(name)) {
      if (italic_depth_ > 0) --italic_depth_;
      return;
    }
    if (is_underline_tag(name)) {
      if (underline_depth_ > 0) --underline_depth_;
      return;
    }
    if (is_strike_tag(name)) {
      if (strike_depth_ > 0) --strike_depth_;
      return;
    }
  }

  void on_chars(const XML_Char* text, int len) {
    if (len <= 0) return;
    // Skip leading whitespace when the paragraph is fresh -- avoids
    // synthesising a "paragraph" of just whitespace between tags.
    size_t start = 0;
    if (current_text_.empty()) {
      while (start < static_cast<size_t>(len) && is_xml_space(text[start])) ++start;
      if (start == static_cast<size_t>(len)) return;
    }

    const size_t before = current_text_.size();
    const uint8_t mask = current_style_mask();
    // Collapse runs of whitespace into a single space character as we
    // append. Cheap normalisation that matches how readers will lay
    // out the text anyway.
    bool last_was_space = !current_text_.empty() && current_text_.back() == ' ';
    for (size_t i = start; i < static_cast<size_t>(len); ++i) {
      const char c = text[i];
      if (is_xml_space(c)) {
        if (!last_was_space) {
          current_text_.push_back(' ');
          last_was_space = true;
        }
      } else {
        current_text_.push_back(c);
        last_was_space = false;
      }
    }

    if (mask != 0) {
      const size_t added = current_text_.size() - before;
      if (added > 0) {
        // Extend the last run if it ends at the same byte position
        // AND carries the same style mask. Otherwise start a fresh
        // run at `before`.
        if (!current_runs_.empty() && current_runs_.back().style == mask &&
            static_cast<size_t>(current_runs_.back().start) + current_runs_.back().length == before) {
          current_runs_.back().length = static_cast<uint16_t>(current_runs_.back().length + added);
        } else if (before <= 0xFFFF && added <= 0xFFFF) {
          CmbStyleRun r;
          r.start = static_cast<uint16_t>(before);
          r.length = static_cast<uint16_t>(added);
          r.style = mask;
          current_runs_.push_back(r);
        }
        // If a paragraph's text length exceeds 64 KB, run-position
        // overflows the u16 fields. We drop the affected run (the
        // text itself is still kept). This is fine for the first
        // converter version; production EPUBs don't have 64 KB
        // single-paragraph runs.
      }
    }
  }

  // ---- state helpers ----

  bool emit_pending() {
    // Trim trailing whitespace (we always collapse internal whitespace
    // to a single space; at most one trailing space to peel).
    while (!current_text_.empty() && current_text_.back() == ' ') {
      current_text_.pop_back();
    }
    if (current_text_.empty() && current_anchor_.empty() && pending_heading_ == 0) {
      // Nothing accumulated -- common when paragraph-boundary tags
      // wrap empty / whitespace-only spans. No paragraph emitted.
      reset_pending();
      return true;
    }

    CmbParagraph p;
    p.type = kCmbBlockText;
    p.text = std::move(current_text_);
    p.runs = std::move(current_runs_);
    p.heading_level = pending_heading_;
    p.anchor_id = std::move(current_anchor_);
    // alignment defaults to kCmbAlignDefault; CSS-driven alignment is
    // a future enhancement (would require a CSS parse pass).

    reset_pending();
    if (sink_ != nullptr) {
      if (!sink_(ctx_, p)) {
        sink_failed_ = true;
        return false;
      }
    }
    return true;
  }

  void reset_pending() {
    current_text_.clear();
    current_runs_.clear();
    current_anchor_.clear();
    pending_heading_ = 0;
  }

  uint8_t current_style_mask() const {
    uint8_t m = 0;
    if (bold_depth_ > 0) m |= kCmbStyleBold;
    if (italic_depth_ > 0) m |= kCmbStyleItalic;
    if (underline_depth_ > 0) m |= kCmbStyleUnderline;
    if (strike_depth_ > 0) m |= kCmbStyleStrikethrough;
    return m;
  }

  static bool is_xml_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

  // ---- members ----

  XML_Parser parser_ = nullptr;
  Sink sink_ = nullptr;
  void* ctx_ = nullptr;
  bool sink_failed_ = false;

  // Per-paragraph accumulator.
  std::string current_text_;
  std::vector<CmbStyleRun> current_runs_;
  std::string current_anchor_;
  uint8_t pending_heading_ = 0;

  // Open-depth counters for inline-style tags.
  int bold_depth_ = 0;
  int italic_depth_ = 0;
  int underline_depth_ = 0;
  int strike_depth_ = 0;
};

// Adapter that lets the walker push paragraphs straight into a
// CmbWriter without a std::function indirection.
struct WriterSinkCtx {
  CmbWriter* writer;
  bool any_failure;
};

bool writer_sink(void* ctx_v, const CmbParagraph& p) {
  auto* ctx = static_cast<WriterSinkCtx*>(ctx_v);
  if (ctx->any_failure) return false;
  if (!ctx->writer->write_paragraph(p)) {
    ctx->any_failure = true;
    return false;
  }
  return true;
}

}  // namespace

bool convert_epub_to_cmb(Epub& book, const char* output_path) {
  CmbWriter w;
  if (!w.open(output_path)) return false;

  WriterSinkCtx ctx{&w, false};

  const int chapter_count = book.getSpineItemsCount();
  for (int ci = 0; ci < chapter_count; ++ci) {
    const auto entry = book.getSpineItem(ci);

    size_t raw_size = 0;
    uint8_t* raw = book.readItemContentsToBytes(entry.href, &raw_size);

    w.begin_chapter();
    if (raw != nullptr && raw_size > 0) {
      // Fresh walker per chapter -- expat carries some per-document
      // state (DTD declarations, namespace tables) we don't want to
      // bleed across spine items.
      XhtmlParagraphWalker walker(writer_sink, &ctx);
      const bool parsed_ok = walker.feed(reinterpret_cast<const char*>(raw), raw_size, /*is_final=*/true);
      // Flush whatever the walker buffered past its last paragraph
      // close (handles XHTML that ends without a closing </p>).
      const bool flushed_ok = walker.flush();
      if (!parsed_ok || !flushed_ok || ctx.any_failure) {
        // Parse error / writer error -- finalize the chapter so the
        // descriptor table is consistent, then bail.
        std::free(raw);
        w.end_chapter();
        w.close();
        return false;
      }
    }
    std::free(raw);
    w.end_chapter();
  }

  if (!w.finish(book.getTitle(), book.getAuthor())) {
    w.close();
    return false;
  }
  w.close();
  return true;
}

}  // namespace cmb
