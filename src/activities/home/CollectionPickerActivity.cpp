#include "CollectionPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CollectionsStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

void CollectionPickerActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

namespace {
// Returns only the user-editable (non-virtual) collections so the picker
// doesn't expose Recently Added / All Books — those are auto-managed and
// don't make sense as toggle targets.
std::vector<const Collection*> editableCollections() {
  std::vector<const Collection*> out;
  const auto& all = CollectionsStore::getInstance().getCollections();
  out.reserve(all.size());
  for (const auto& c : all) {
    if (!c.isVirtual) out.push_back(&c);
  }
  return out;
}
}  // namespace

int CollectionPickerActivity::itemCount() const {
  // 1 for "+ New collection..." + one row per editable (user) collection.
  return 1 + static_cast<int>(editableCollections().size());
}

void CollectionPickerActivity::toggleCollectionAt(int index) {
  // index 0 is "+ New collection..." (handled elsewhere); index 1+ maps
  // 1:1 onto the editableCollections() array.
  const auto cols = editableCollections();
  const int colIdx = index - 1;
  if (colIdx < 0 || colIdx >= static_cast<int>(cols.size())) return;
  CollectionsStore::getInstance().toggleBookInCollection(cols[colIdx]->id, bookPath);
  requestUpdate();
}

void CollectionPickerActivity::openNewCollectionPrompt() {
  // Captured-by-value bookPath ensures the lambda still has the path if
  // CollectionsStore gets compacted/moved between now and the keyboard
  // returning.
  const std::string captured = bookPath;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEW_COLLECTION_PROMPT),
                                              /*initialText=*/"", /*maxLength=*/40, InputType::Text),
      [this, captured](const ActivityResult& res) {
        if (res.isCancelled) return;
        const auto& kr = std::get<KeyboardResult>(res.data);
        // Trim leading/trailing whitespace so an accidental space-only
        // name doesn't slip past the empty check below.
        std::string trimmed = kr.text;
        auto leftSpace = trimmed.find_first_not_of(" \t");
        auto rightSpace = trimmed.find_last_not_of(" \t");
        if (leftSpace == std::string::npos) {
          trimmed.clear();
        } else {
          trimmed = trimmed.substr(leftSpace, rightSpace - leftSpace + 1);
        }
        if (trimmed.empty()) {
          requestUpdate();
          return;
        }
        const std::string newId = CollectionsStore::getInstance().createCollection(trimmed);
        if (!newId.empty()) {
          // Auto-add the focused book to the new collection — the user
          // almost certainly opened the picker because they wanted this
          // book grouped somewhere, so skip a second confirm step.
          CollectionsStore::getInstance().toggleBookInCollection(newId, captured);
          // Focus the freshly-created row so the user can see it landed
          // in the list with the checkbox on. Indices in the picker are
          // 1-based over editableCollections() (index 0 is the "+ New
          // collection..." entry), so map by editable-collection index
          // rather than global collection index.
          const auto cols = editableCollections();
          for (size_t i = 0; i < cols.size(); ++i) {
            if (cols[i]->id == newId) {
              selectedIndex = 1 + static_cast<int>(i);
              break;
            }
          }
        }
        requestUpdate();
      });
}

void CollectionPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = false;  // not a cancel — toggles already persisted as side effects.
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (isNewCollectionItem(selectedIndex)) {
      openNewCollectionPrompt();
    } else {
      toggleCollectionAt(selectedIndex);
    }
    return;
  }

  const int count = itemCount();
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void CollectionPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Header shows the book's title so the user has unambiguous context for
  // what they're modifying — they may have arrived here from either the
  // carousel or the shelf and shouldn't have to remember which.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, bookTitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int count = itemCount();
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, count, selectedIndex,
               [this](int index) {
                 if (isNewCollectionItem(index)) {
                   return std::string(tr(STR_NEW_COLLECTION));
                 }
                 const auto cols = editableCollections();
                 const int colIdx = index - 1;
                 if (colIdx < 0 || colIdx >= static_cast<int>(cols.size())) {
                   return std::string{};
                 }
                 // [x] / [ ] checkbox prefix mirrors common picker UX so
                 // the membership state is obvious at a glance — no need
                 // for a separate column or icon.
                 const bool isIn =
                     CollectionsStore::getInstance().isBookInCollection(cols[colIdx]->id, bookPath);
                 return std::string(isIn ? "[x] " : "[ ] ") + cols[colIdx]->name;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
