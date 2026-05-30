#pragma once

// CmbConverter -- EPUB -> .cmb single-pass conversion.
//
// Wraps an opened `Epub` and emits a .cmb file alongside it. Uses
// `CmbWriter` for the on-disk format and `Epub::readItemContentsToBytes`
// to pull each spine item's raw XHTML out of the EPUB ZIP.
//
// Phase A.C v0 (this checkpoint): minimal text extraction.
//   - One CmbParagraph per spine chapter, containing the chapter's
//     entire stripped-of-tags text as a single kCmbBlockText record.
//   - Metadata: title + author from Epub::getTitle() / getAuthor().
//   - No image refs, no inline-style runs, no anchor table, no
//     per-paragraph splitting on <p>/<div>/<h*>.
//
// Phase A.C v1 (next checkpoint): proper paragraph segmentation via
// the expat parser + entity decoding hooks.
//
// Phase A.C v2: inline-style runs (bold/italic/underline), heading
// levels, anchor id collection for in-book link nav.
//
// The Epub passed in MUST already be loaded (`Epub::load(...)` called
// successfully). Conversion does not depend on the layout-specific
// font/margin settings -- the produced .cmb is layout-independent
// per the format design.

#include <string>

#include "../Epub/Epub.h"

namespace cmb {

// Convert `book` to a .cmb file at `output_path`. Returns true on
// success, false on any I/O or format error along the way; on false
// the partial output file may be left on disk in an unspecified
// state -- callers should treat it as missing.
//
// The book's title + author become the .cmb metadata. Spine order
// is preserved as chapter order in the output (chapter index i in
// the .cmb corresponds to spine index i in the EPUB).
bool convert_epub_to_cmb(Epub& book, const char* output_path);

}  // namespace cmb
