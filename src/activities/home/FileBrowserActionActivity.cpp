#include "FileBrowserActionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kTitleFontId = UI_10_FONT_ID;
constexpr int kTitleMaxLines = 2;
constexpr int kCompactTitleY = 14;
constexpr int kTallHeaderTitleBottomPadding = 8;
constexpr int kCompactHeaderTitleBottomPadding = 4;
constexpr int kTitleLineGap = 1;
constexpr int kBatteryTextReserveWidth = 90;
}  // namespace

void FileBrowserActionActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void FileBrowserActionActivity::loop() {
  if (ignoreConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
      return;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // CrumBLE: items wired with inlineToggle run the callback in place and
    // stay in the menu. The row's rightValueGetter will re-evaluate on
    // the next paint so the user sees the change without leaving the menu.
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size()) &&
        items[selectedIndex].inlineToggle) {
      items[selectedIndex].inlineToggle();
      requestUpdate();
      return;
    }
    setResult(FileBrowserActionResult{static_cast<int>(items[selectedIndex].action)});
    finish();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(items.size()));
    requestUpdate();
  });
}

void FileBrowserActionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int titleX = metrics.contentSidePadding;
  const int titleMaxWidth = std::max(0, pageWidth - titleX - metrics.contentSidePadding - kBatteryTextReserveWidth);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, title.c_str(), titleMaxWidth, kTitleMaxLines, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight +
                               std::max(0, static_cast<int>(titleLines.size()) - 1) * kTitleLineGap;
  const bool tallHeader = metrics.headerHeight > 60;
  const int titleY = metrics.topPadding + (tallHeader ? metrics.batteryBarHeight + 3 : kCompactTitleY);
  const int titleBottomPadding = tallHeader ? kTallHeaderTitleBottomPadding : kCompactHeaderTitleBottomPadding;
  const int actionHeaderHeight =
      std::max(metrics.headerHeight, titleY - metrics.topPadding + titleBlockHeight + titleBottomPadding);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, actionHeaderHeight}, "");

  for (int i = 0; i < static_cast<int>(titleLines.size()); ++i) {
    renderer.drawText(kTitleFontId, titleX, titleY + i * (titleLineHeight + kTitleLineGap), titleLines[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }

  // CrumBLE: optional secondary label right-justified on the title row
  // (used by the shelf-header menu to surface the current sort mode at
  // a glance, e.g. "Favorites    Title (A-Z)"). Drawn in REGULAR weight
  // and a smaller font so the title still reads as primary. Clamped to
  // the title's reserved area so it never collides with the battery
  // readout on the far right.
  if (!headerRightLabel.empty()) {
    constexpr int kRightLabelFontId = UI_10_FONT_ID;
    const std::string rightLabel = renderer.truncatedText(kRightLabelFontId, headerRightLabel.c_str(),
                                                          std::max(0, titleMaxWidth / 2));
    const int rw = renderer.getTextWidth(kRightLabelFontId, rightLabel.c_str(), EpdFontFamily::REGULAR);
    const int rx = titleX + titleMaxWidth - rw;
    renderer.drawText(kRightLabelFontId, rx, titleY, rightLabel.c_str(), true, EpdFontFamily::REGULAR);
  }

  const int contentTop = metrics.topPadding + actionHeaderHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  // CrumBLE: highlightValue=true matches the visual style of the main Settings
  // menu -- the right-justified value box inverts on the selected row -- so
  // the shelf-header toggles read as "real" settings rather than action items
  // with extra text. drawList still falls back to plain text when rowValue
  // returns "", so action-only rows (Rename, Sort by, Rescan) render normally.
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(items.size()), selectedIndex,
               [this](int index) { return std::string(I18N.get(items[index].labelId)); },
               /*rowSubtitle=*/nullptr,
               /*rowIcon=*/nullptr,
               [this](int index) {
                 // Getter wins over static value -- lets rows update live
                 // as inlineToggle flips the underlying state.
                 return items[index].rightValueGetter ? items[index].rightValueGetter() : items[index].rightValue;
               },
               /*highlightValue=*/true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
