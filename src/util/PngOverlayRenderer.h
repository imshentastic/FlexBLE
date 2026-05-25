#pragma once

#include <string>

class GfxRenderer;

namespace PngOverlay {

// Decode a PNG into the renderer's CURRENT framebuffer using the shared
// sleep-screen pipeline: scale-to-fit, centered, transparency preserved
// (pixels with alpha < 128, or matching a tRNS color key, are skipped so the
// existing background shows through). Does NOT clear the framebuffer or call
// displayBuffer -- the caller prepares the background (white for full-screen, or
// the last reader page for an overlay) and presents afterward.
//
// Returns false on missing file, insufficient heap for the decoder, or a decode
// error.
bool decodeToBuffer(GfxRenderer& renderer, const std::string& filename, int pageWidth, int pageHeight);

}  // namespace PngOverlay
