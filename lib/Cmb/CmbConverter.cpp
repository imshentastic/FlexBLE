#include "CmbConverter.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "CmbWriter.h"

namespace cmb {

namespace {

// Strip XHTML/HTML tags from a UTF-8 byte buffer and return the
// resulting plain-text content. Single-pass, no regex, no expat
// dependency. Handles:
//   - Tags <foo> and </foo> -- skipped entirely (including attributes
//     and self-closing forms like <br/>). Doesn't parse the tag name;
//     just consumes characters until the matching '>'.
//   - Comments <!-- ... --> -- skipped.
//   - The common named entities (&amp;, &lt;, &gt;, &quot;, &apos;,
//     &nbsp;) and numeric refs &#N; / &#xH;.
//   - Whitespace collapse: any run of ASCII whitespace (space, tab,
//     newline, CR, FF) becomes a single space. Leading + trailing
//     whitespace is trimmed.
//
// Phase A.C v0 limitation: paragraph boundaries (<p>, <div>, <h1..6>,
// <br/>) are NOT preserved -- the whole chapter collapses into one
// run of words separated by spaces. Acceptable for round-trip testing
// the writer/reader; v1 of the converter splits on these boundaries.
std::string strip_html_to_plain_text(std::string_view src) {
  std::string out;
  // Reserve a guess: stripped text is usually 60-80% of source size.
  out.reserve(src.size() * 3 / 4);

  const size_t n = src.size();
  size_t i = 0;
  bool last_was_space = true;  // emits no leading whitespace

  auto emit_char = [&](char c) {
    if (c == '\0') return;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
      if (!last_was_space) {
        out.push_back(' ');
        last_was_space = true;
      }
      return;
    }
    out.push_back(c);
    last_was_space = false;
  };

  auto emit_codepoint = [&](uint32_t cp) {
    // Re-encode codepoint as UTF-8 bytes. Common case: ASCII falls
    // through emit_char's whitespace handling cleanly.
    if (cp < 0x80) {
      emit_char(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      last_was_space = false;
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      last_was_space = false;
    } else if (cp < 0x110000) {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      last_was_space = false;
    }
    // Codepoints >= 0x110000 are illegal -- silently drop.
  };

  while (i < n) {
    const char c = src[i];
    if (c == '<') {
      // Comment? <!-- ... -->
      if (i + 3 < n && src[i + 1] == '!' && src[i + 2] == '-' && src[i + 3] == '-') {
        // Scan for "-->"
        size_t j = i + 4;
        while (j + 2 < n && !(src[j] == '-' && src[j + 1] == '-' && src[j + 2] == '>')) {
          ++j;
        }
        i = (j + 2 < n) ? j + 3 : n;
        // Treat comment as a separator (emit space so adjacent text
        // doesn't run together when a comment splits it).
        if (!last_was_space) {
          out.push_back(' ');
          last_was_space = true;
        }
        continue;
      }
      // Generic tag: skip to matching '>'. Doesn't try to parse the
      // tag; intentionally permissive on malformed HTML.
      while (i < n && src[i] != '>') ++i;
      if (i < n) ++i;  // consume '>'
      // Tags act as soft separators between text runs.
      if (!last_was_space) {
        out.push_back(' ');
        last_was_space = true;
      }
      continue;
    }
    if (c == '&') {
      // Entity. Common named refs + numeric refs.
      // Scan for terminating ';' (cap at 8 chars to stay safe on
      // malformed input).
      size_t end = i + 1;
      while (end < n && end < i + 10 && src[end] != ';' && src[end] != '<' && src[end] != '&') {
        ++end;
      }
      if (end < n && src[end] == ';') {
        const std::string_view entity = src.substr(i + 1, end - i - 1);
        if (entity == "amp") {
          emit_char('&');
        } else if (entity == "lt") {
          emit_char('<');
        } else if (entity == "gt") {
          emit_char('>');
        } else if (entity == "quot") {
          emit_char('"');
        } else if (entity == "apos") {
          emit_char('\'');
        } else if (entity == "nbsp") {
          emit_char(' ');
        } else if (entity.size() >= 2 && entity[0] == '#') {
          // Numeric ref: &#N; (decimal) or &#xH; / &#XH; (hex).
          uint32_t cp = 0;
          if (entity[1] == 'x' || entity[1] == 'X') {
            for (size_t k = 2; k < entity.size(); ++k) {
              const char d = entity[k];
              if (d >= '0' && d <= '9') {
                cp = cp * 16 + static_cast<uint32_t>(d - '0');
              } else if (d >= 'a' && d <= 'f') {
                cp = cp * 16 + static_cast<uint32_t>(d - 'a' + 10);
              } else if (d >= 'A' && d <= 'F') {
                cp = cp * 16 + static_cast<uint32_t>(d - 'A' + 10);
              } else {
                cp = 0;
                break;
              }
            }
          } else {
            for (size_t k = 1; k < entity.size(); ++k) {
              const char d = entity[k];
              if (d < '0' || d > '9') {
                cp = 0;
                break;
              }
              cp = cp * 10 + static_cast<uint32_t>(d - '0');
            }
          }
          if (cp != 0) emit_codepoint(cp);
        }
        // Unknown named entity -- drop silently. (Could fall back to
        // emitting the raw '&...;' but that confuses downstream
        // text-content callers more than dropping does.)
        i = end + 1;
        continue;
      }
      // No terminating ';' -- treat '&' as literal.
      emit_char('&');
      ++i;
      continue;
    }
    emit_char(c);
    ++i;
  }

  // Trim trailing whitespace (we always emit a single space for runs,
  // so at most one trailing space to peel off).
  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

}  // namespace

bool convert_epub_to_cmb(Epub& book, const char* output_path) {
  CmbWriter w;
  if (!w.open(output_path)) return false;

  const int chapter_count = book.getSpineItemsCount();
  for (int ci = 0; ci < chapter_count; ++ci) {
    const auto entry = book.getSpineItem(ci);

    size_t raw_size = 0;
    uint8_t* raw = book.readItemContentsToBytes(entry.href, &raw_size);

    w.begin_chapter();
    if (raw != nullptr && raw_size > 0) {
      // Phase A.C v0: one paragraph per chapter containing all the
      // stripped-of-tags text. Subsequent commits split on <p>/<div>/<h*>
      // boundaries and capture inline styling.
      std::string text = strip_html_to_plain_text(
          std::string_view{reinterpret_cast<const char*>(raw), raw_size});
      if (!text.empty()) {
        CmbParagraph p;
        p.type = kCmbBlockText;
        p.text = std::move(text);
        if (!w.write_paragraph(p)) {
          // Write failure on this paragraph -- skip rest of chapter
          // (end_chapter still writes the descriptor table for what
          // we did emit) but treat the overall conversion as failed.
          std::free(raw);
          w.end_chapter();
          w.close();
          return false;
        }
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
