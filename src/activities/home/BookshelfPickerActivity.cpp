#include "BookshelfPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "../ActivityResult.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

BookshelfPickerActivity::BookshelfPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::vector<std::string> labels, int currentIndex)
    : Activity("BookshelfPicker", renderer, mappedInput),
      labels_(std::move(labels)),
      currentIndex_(currentIndex) {}

void BookshelfPickerActivity::onEnter() {
  Activity::onEnter();
  // Land the cursor on the currently-active collection when possible so
  // long-press -> Confirm is a no-op that closes the picker (matches the
  // FontSelectionActivity affordance).
  selectedIndex_ =
      (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(labels_.size())) ? currentIndex_ : 0;
  requestUpdate();
}

void BookshelfPickerActivity::onExit() { Activity::onExit(); }

void BookshelfPickerActivity::loop() {
  using B = MappedInputManager::Button;

  // Swallow the Confirm release that follows the long-press that opened
  // the picker, so it doesn't immediately trigger "select" on the row
  // currentIndex_ landed on. After the held Confirm has been released
  // (or was never held at the moment of entry), the gate falls open.
  if (ignoreConfirmRelease_) {
    if (mappedInput.wasReleased(B::Confirm)) {
      ignoreConfirmRelease_ = false;
      return;
    }
    if (!mappedInput.isPressed(B::Confirm)) {
      ignoreConfirmRelease_ = false;
    }
  }

  const int listSize = static_cast<int>(labels_.size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  if (mappedInput.wasReleased(B::Confirm)) {
    ChoicePromptResult res;
    res.choice = selectedIndex_;
    setResult(ActivityResult{res});
    finish();
    return;
  }

  if (mappedInput.wasReleased(B::Back)) {
    ChoicePromptResult res;
    res.choice = -1;  // Back = cancel sentinel
    ActivityResult ar;
    ar.isCancelled = true;
    ar.data = res;
    setResult(std::move(ar));
    finish();
    return;
  }
}

void BookshelfPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOKSHELF));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(labels_.size()), selectedIndex_,
      [this](int index) { return labels_[index]; }, nullptr, nullptr,
      [this](int index) -> std::string { return index == currentIndex_ ? tr(STR_SELECTED) : ""; }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
