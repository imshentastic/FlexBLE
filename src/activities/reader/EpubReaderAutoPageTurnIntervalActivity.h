#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class GfxRenderer;

class EpubReaderAutoPageTurnIntervalActivity final : public Activity {
 public:
  explicit EpubReaderAutoPageTurnIntervalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                  int initialSeconds, bool ignoreInitialConfirmRelease = false)
      : Activity("EpubReaderAutoPageTurnInterval", renderer, mappedInput),
        seconds(initialSeconds),
        ignoreConfirmRelease(ignoreInitialConfirmRelease) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  int seconds;
  ButtonNavigator buttonNavigator;
  bool ignoreConfirmRelease = false;

  void adjustSeconds(int delta);
};
