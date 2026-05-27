#include "SortPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"

const char* SortPickerActivity::labelFor(CollectionSort mode) {
  switch (mode) {
    case CollectionSort::Manual:
      return tr(STR_SORT_MANUAL);
    case CollectionSort::TitleAlpha:
      return tr(STR_SORT_TITLE_ALPHA);
    case CollectionSort::TitleAlphaDesc:
      return tr(STR_SORT_TITLE_ALPHA_DESC);
    case CollectionSort::DateAddedDesc:
      return tr(STR_SORT_DATE_ADDED_DESC);
    case CollectionSort::DateAddedAsc:
      return tr(STR_SORT_DATE_ADDED_ASC);
    case CollectionSort::DateLastReadDesc:
      return tr(STR_SORT_DATE_LAST_READ_DESC);
    case CollectionSort::AuthorAlpha:
      return tr(STR_SORT_AUTHOR_LAST_ALPHA);
    case CollectionSort::AuthorAlphaDesc:
      return tr(STR_SORT_AUTHOR_LAST_ALPHA_DESC);
  }
  return "";
}

void SortPickerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SortPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(options.size())) {
      return;
    }
    setResult(SortPickerResult{static_cast<int>(options[selectedIndex])});
    finish();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(options.size()));
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(options.size()));
    requestUpdate();
  });
}

void SortPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(options.size()), selectedIndex,
               [this](int index) {
                 if (index < 0 || index >= static_cast<int>(options.size())) return std::string{};
                 // Bullet the currently-active mode so it's visible at
                 // a glance which sort is in effect right now. The
                 // selectedIndex (highlight) and currentMode (bullet)
                 // are independent: highlight = what the user is
                 // hovering on, bullet = what's already applied.
                 std::string label = options[index] == currentMode ? "* " : "  ";
                 label += labelFor(options[index]);
                 return label;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
