#include "ControlsOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "activities/settings/ButtonRemapActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ControlsOptionsActivity::onEnter() {
  Activity::onEnter();

  rebuildSettingsList();
  requestUpdate();
}

void ControlsOptionsActivity::onExit() { Activity::onExit(); }

void ControlsOptionsActivity::rebuildSettingsList() {
  settings.clear();

  const auto allSettings = getSettingsList();
  auto addControlSetting = [&](StrId nameId) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(),
                                 [nameId](const auto& setting) { return setting.nameId == nameId; });
    if (it != allSettings.end()) {
      settings.push_back(*it);
      return;
    }
    LOG_ERR("CTRL", "Missing control setting definition for nameId=%d", static_cast<int>(nameId));
  };
  auto addControlSettingByKey = [&](const char* key) {
    const auto it = std::find_if(allSettings.begin(), allSettings.end(), [key](const auto& setting) {
      return setting.key && std::strcmp(setting.key, key) == 0;
    });
    if (it != allSettings.end()) {
      settings.push_back(*it);
      return;
    }
    LOG_ERR("CTRL", "Missing control setting definition for key=%s", key);
  };

  const bool hasTiltPageTurnSetting = std::any_of(allSettings.begin(), allSettings.end(), [](const auto& setting) {
    return setting.nameId == StrId::STR_TILT_PAGE_TURN;
  });
  const size_t expectedControlsSettingsCount = hasTiltPageTurnSetting ? 15 : 13;
  settings.reserve(expectedControlsSettingsCount);

  settings.push_back(SettingInfo::SectionHeader(StrId::STR_POWER_BUTTON));
  addControlSetting(StrId::STR_SHORT_PWR_BTN);
  addControlSetting(StrId::STR_LONG_PRESS_ACTION);
  settings.push_back(SettingInfo::SectionHeader(StrId::STR_FRONT_BUTTONS));
  settings.push_back(SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  settings.push_back(
      SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS_READER, SettingAction::RemapFrontButtonsReader));
  addControlSettingByKey("frontButtonOrientationAware");
  addControlSetting(StrId::STR_LONG_PRESS_BEHAVIOR);
  addControlSetting(StrId::STR_LONG_PRESS_MENU_ACTION);
  settings.push_back(SettingInfo::SectionHeader(StrId::STR_SIDE_BUTTONS));
  addControlSetting(StrId::STR_SIDE_BTN_LAYOUT);
  addControlSettingByKey("sideButtonOrientationAware");
  addControlSetting(StrId::STR_SIDE_BTN_LONG_PRESS);
  if (hasTiltPageTurnSetting) {
    settings.push_back(SettingInfo::SectionHeader(StrId::STR_OTHER));
    addControlSetting(StrId::STR_TILT_PAGE_TURN);
  }

  settingsCount = static_cast<int>(settings.size());
  selectedIndex = 0;
  while (selectedIndex < settingsCount && settings[selectedIndex].type == SettingType::SECTION_HEADER) {
    selectedIndex++;
  }
  if (selectedIndex >= settingsCount) {
    selectedIndex = 0;
  }
}

void ControlsOptionsActivity::moveSelection(bool forward) {
  if (settingsCount <= 0) return;

  for (int i = 0; i < settingsCount; i++) {
    selectedIndex = forward ? ButtonNavigator::nextIndex(selectedIndex, settingsCount)
                            : ButtonNavigator::previousIndex(selectedIndex, settingsCount);
    if (settings[selectedIndex].type != SettingType::SECTION_HEADER) {
      break;
    }
  }
}

void ControlsOptionsActivity::toggleCurrentSetting() {
  if (selectedIndex < 0 || selectedIndex >= settingsCount) return;
  const auto& setting = settings[selectedIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool cur = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !cur;
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t cur = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (cur + 1) % static_cast<uint8_t>(setting.enumValues.size());
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t cur = SETTINGS.*(setting.valuePtr);
    if (cur + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = cur + setting.valueRange.step;
    }
    SETTINGS.saveToFile();
  } else if (setting.type == SettingType::ACTION) {
    if (setting.action == SettingAction::RemapFrontButtons) {
      startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, false),
                             [](const ActivityResult&) { SETTINGS.saveToFile(); });
      return;
    }
    if (setting.action == SettingAction::RemapFrontButtonsReader) {
      startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, true),
                             [](const ActivityResult&) { SETTINGS.saveToFile(); });
      return;
    }
  }
}

void ControlsOptionsActivity::loop() {
  buttonNavigator.onNextRelease([this] {
    moveSelection(true);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    moveSelection(false);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    finish();
    return;
  }
}

void ControlsOptionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.buttonHintsHeight : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;

  GUI.drawHeader(renderer, Rect{contentX, metrics.topPadding, contentWidth, metrics.headerHeight}, tr(STR_CAT_CONTROLS),
                 nullptr);

  GUI.drawList(
      renderer,
      Rect{contentX, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, contentWidth,
           pageHeight -
               (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight + metrics.verticalSpacing * 2)},
      settingsCount, selectedIndex, [this](int i) { return std::string(I18N.get(settings[i].nameId)); }, nullptr,
      nullptr,
      [this](int i) {
        const auto& setting = settings[i];
        std::string valueText;
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          valueText = SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          const uint8_t safeValue = value < setting.enumValues.size() ? value : 0;
          valueText = I18N.get(setting.enumValues[safeValue]);
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          valueText = std::to_string(SETTINGS.*(setting.valuePtr));
        }
        return valueText;
      },
      true, nullptr, [this](int i) { return settings[i].type == SettingType::SECTION_HEADER; });

  const bool currentIsAction =
      selectedIndex >= 0 && selectedIndex < settingsCount && settings[selectedIndex].type == SettingType::ACTION;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), currentIsAction ? tr(STR_SELECT) : tr(STR_TOGGLE),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
