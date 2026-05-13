#pragma once
#include <I18n.h>

#include <vector>

#include "../Activity.h"
#include "../settings/SettingsActivity.h"
#include "util/ButtonNavigator.h"

class ControlsOptionsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int settingsCount = 0;
  std::vector<SettingInfo> settings;

  void rebuildSettingsList();
  void moveSelection(bool forward);
  void toggleCurrentSetting();

 public:
  explicit ControlsOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ControlsOptions", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
};
