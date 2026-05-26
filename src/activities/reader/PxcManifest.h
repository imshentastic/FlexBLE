#pragma once

#include <cstdint>
#include <string>

// CrumBLE: parsed contents of META-INF/crumble-pxc.json -- the .pxc manifest
// the optimizer emits when it bakes pixel caches for a book. Compared against
// current SETTINGS on Bluetooth connect: if any of fontId / orientation /
// screenMargin / imageRendering differ, the user's current layout won't match
// the bake's viewport math and the .pxc files will be rejected at render time.
// The rest are stored for diagnostics and optional UI display (the mismatch
// prompt uses fontFamily/fontSize/sdFontFamilyName to show human-readable
// font names in the side-by-side comparison).
//
// Lives in its own header so BookSettingsDrawerActivity can use it without
// pulling in all of EpubReaderActivity.h.
struct PxcManifest {
  uint8_t orientation = 0;
  uint8_t screenMargin = 0;
  uint8_t imageRendering = 0;
  int32_t fontId = 0;
  uint16_t viewportW = 0;
  uint16_t viewportH = 0;
  uint16_t screenW = 0;
  uint16_t screenH = 0;
  uint16_t pxcCount = 0;
  // Raw font selection fields (for human-readable display in the prompt).
  // Older manifests won't have these -- parser leaves them at defaults and
  // the display path falls back to a generic label.
  uint8_t fontFamily = 0;
  uint8_t fontSize = 0;
  uint8_t sdFontSizeRange = 0;
  std::string sdFontFamilyName;  // empty when built-in font was used
};
