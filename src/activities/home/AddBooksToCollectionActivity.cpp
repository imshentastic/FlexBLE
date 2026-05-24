#include "AddBooksToCollectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <unordered_set>

#include "CollectionsStore.h"
#include "FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"

void AddBooksToCollectionActivity::rebuildRows() {
  recentPaths.clear();
  std::unordered_set<std::string> seen;
  // Recently-opened books first (the common quick-add pool), newest first.
  const auto& books = RECENT_BOOKS.getBooks();
  recentPaths.reserve(books.size());
  for (const auto& b : books) {
    if (seen.insert(b.path).second) recentPaths.push_back(b.path);
  }
  // Then any books already in the collection but NOT in the recent list -- e.g.
  // older books the user added via "Browse files..." -- appended at the bottom
  // so they show with a checkmark on future visits.
  for (const auto& p : CollectionsStore::getInstance().resolveBookPaths(collectionId)) {
    if (seen.insert(p).second) recentPaths.push_back(p);
  }
}

void AddBooksToCollectionActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  rebuildRows();
  requestUpdate();
}

std::string AddBooksToCollectionActivity::rowLabel(int idx) const {
  // Last row is the "Browse files..." entry (opens the File Manager to add an
  // older book from a folder). No checkbox prefix.
  if (idx == static_cast<int>(recentPaths.size())) {
    return tr(STR_BROWSE_FILES);
  }
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
    const int browseRow = static_cast<int>(recentPaths.size());
    if (selectedIndex == browseRow) {
      // Open the File Manager as a book picker. The selected book's path comes
      // back via FilePathResult; we add it to the collection (no-op if already
      // in) and rebuild so it shows checked at the bottom of the list.
      startActivityForResult(
          std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickBook),
          [this](const ActivityResult& res) {
            if (!res.isCancelled) {
              if (const auto* fp = std::get_if<FilePathResult>(&res.data)) {
                if (!CollectionsStore::getInstance().isBookInCollection(collectionId, fp->path)) {
                  CollectionsStore::getInstance().toggleBookInCollection(collectionId, fp->path);
                }
              }
            }
            rebuildRows();
            requestUpdate();
          });
      return;
    }
    if (selectedIndex < 0 || selectedIndex >= browseRow) return;
    CollectionsStore::getInstance().toggleBookInCollection(collectionId, recentPaths[selectedIndex]);
    requestUpdate();
    return;
  }

  const int count = static_cast<int>(recentPaths.size()) + 1;  // +1 for the "Browse files..." row
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
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(recentPaths.size()) + 1,
               selectedIndex, [this](int index) { return rowLabel(index); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
