#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderAutoPageTurnIntervalActivity final : public Activity {
 public:
  explicit EpubReaderAutoPageTurnIntervalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                  const int initialSeconds)
      : Activity("EpubReaderAutoPageTurnInterval", renderer, mappedInput), seconds(initialSeconds) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  int seconds = 30;
  ButtonNavigator buttonNavigator;

  void adjustSeconds(int delta);
};
