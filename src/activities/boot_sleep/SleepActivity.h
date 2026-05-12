#pragma once
#include "../Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool canSnapshotOverlayBackground)
      : Activity("Sleep", renderer, mappedInput), canSnapshotOverlayBackground(canSnapshotOverlayBackground) {}
  void onEnter() override;

  // Pick a fresh random image from /.sleep (or /sleep) and draw it without any popup or text.
  // Used by the deep-sleep tap-to-cycle path: APP_STATE must already be loaded; the renderer
  // and display must already be initialized; fonts are not required because only a BMP is drawn.
  // No-op if no usable image is found — the existing on-screen image stays visible.
  static void cycleScreensaverFromDeepSleep(GfxRenderer& renderer);

  // Snapshot the current framebuffer to SD so the cycle path can re-use it as
  // the background behind a transparent sleep PNG without needing fonts or the
  // EPUB parser. EpubReaderActivity::onExit calls this so the cache reflects
  // the last book page the user saw.
  static void snapshotFramebufferForCycle();

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderBlankSleepScreen() const;
  void renderOverlaySleepScreen() const;
  // Compose a (possibly transparent) PNG over the last reader page, with the
  // same background-rebuild + grayscale-pass flow used by overlay mode. Used
  // by Custom mode when a PNG is picked so the reader page shows through
  // transparent regions of the image.
  void composePngOverReaderPage(const std::string& pngPath) const;
  bool canSnapshotOverlayBackground = false;
  bool overlayPageBufferStored = false;
  bool overlayPageBufferTrusted = false;
};
