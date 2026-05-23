#include "SeriesMiniPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
}

std::string SeriesMiniPickerActivity::labelFor(int idx) const {
  if (isOptionsRow(idx)) return std::string(tr(STR_SERIES_OPTIONS));
  if (idx < 0 || idx >= static_cast<int>(memberPaths.size())) return {};
  const std::string& path = memberPaths[idx];
  const size_t slash = path.find_last_of('/');
  const std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;
  const size_t dot = fname.find_last_of('.');
  return (dot != std::string::npos && dot > 0) ? fname.substr(0, dot) : fname;
}

void SeriesMiniPickerActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void SeriesMiniPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = false;  // not a cancel — picker is informational; nothing to return.
    setResult(std::move(result));
    finish();
    return;
  }

  // Long-press Confirm on a book row → open the existing per-book
  // action menu via the onLongPress callback. Skipped for the Options
  // row since that already IS a menu trigger.
  if (!longPressHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    if (!isOptionsRow(selectedIndex) && onLongPress) {
      longPressHandled = true;
      onLongPress(memberPaths[selectedIndex]);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressHandled) {
      // Swallow the release that ended the long-press so we don't
      // ALSO trigger the short-press handler below.
      longPressHandled = false;
      return;
    }
    if (isOptionsRow(selectedIndex)) {
      if (onOptions) onOptions();
      return;
    }
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(memberPaths.size())) {
      if (onOpen) onOpen(memberPaths[selectedIndex]);
    }
    return;
  }

  const int count = totalRowCount();
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void SeriesMiniPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Header = series name so the user immediately knows which series
  // they're viewing.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, seriesName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalRowCount(), selectedIndex,
               [this](int index) { return labelFor(index); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
