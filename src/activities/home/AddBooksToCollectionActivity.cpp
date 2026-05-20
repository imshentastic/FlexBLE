#include "AddBooksToCollectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CollectionsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"

void AddBooksToCollectionActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  recentPaths.clear();
  // Capture the recent-books order at entry. Snapshotting keeps the
  // row indices stable even if RECENT_BOOKS were to be mutated by
  // background work (it isn't, currently, but defensive against
  // future changes).
  const auto& books = RECENT_BOOKS.getBooks();
  recentPaths.reserve(books.size());
  for (const auto& b : books) recentPaths.push_back(b.path);
  requestUpdate();
}

std::string AddBooksToCollectionActivity::rowLabel(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(recentPaths.size())) return {};
  const std::string& path = recentPaths[idx];
  // Filename minus extension. Falls back gracefully if the path has
  // no slash / no extension. Matches the labeling used by the
  // collection picker and series mini-picker for visual consistency.
  const size_t slash = path.find_last_of('/');
  const std::string fname = (slash != std::string::npos) ? path.substr(slash + 1) : path;
  const size_t dot = fname.find_last_of('.');
  const std::string title = (dot != std::string::npos && dot > 0) ? fname.substr(0, dot) : fname;
  const bool isIn = CollectionsStore::getInstance().isBookInCollection(collectionId, path);
  std::string label = isIn ? "[x] " : "[ ] ";
  label += title;
  return label;
}

void AddBooksToCollectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = false;  // toggles already persisted by CollectionsStore.
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(recentPaths.size())) return;
    CollectionsStore::getInstance().toggleBookInCollection(collectionId, recentPaths[selectedIndex]);
    requestUpdate();
    return;
  }

  const int count = static_cast<int>(recentPaths.size());
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void AddBooksToCollectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, collectionName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(recentPaths.size()),
               selectedIndex, [this](int index) { return rowLabel(index); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
