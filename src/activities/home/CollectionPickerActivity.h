#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE Collections — picker shown when the user wants to manage which
// collections a specific book belongs to. Rendered as a vertical list:
//
//   [+ New collection...]    ← always at the top; opens KeyboardEntryActivity
//   [x] Favorites             ← already-in-collection rows are checked
//   [ ] Sci-Fi                ← unchecked rows can be toggled into membership
//   [ ] To Re-Read            ← navigation is Up/Down + Confirm
//
// Confirm toggles the membership (or, on the new-collection row, kicks off
// the create-new flow). Back exits. The picker mutates CollectionsStore
// in place; the result is informational (isCancelled=false on a clean
// exit). Callers don't need to read result.data — they should just
// re-render whatever depends on collection membership.
class CollectionPickerActivity final : public Activity {
 public:
  CollectionPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                           std::string bookTitle)
      : Activity("CollectionPicker", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath;
  std::string bookTitle;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  // Index 0 is always the "+ New collection..." item; collection indices
  // start at 1. itemCount() reflects this layout.
  int itemCount() const;
  bool isNewCollectionItem(int index) const { return index == 0; }

  void openNewCollectionPrompt();
  void toggleCollectionAt(int index);
};
