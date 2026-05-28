#pragma once

#include <string>
#include <vector>

#include "CollectionsStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE Collections — sort picker for the shelf-header action menu.
// Lists the available sort modes for a given collection (skips Manual
// for virtual collections since their book lists aren't user-ordered)
// and pre-highlights the currently-active mode. Returns the user's
// pick as a SortPickerResult on the ActivityResult; the caller is
// expected to call CollectionsStore::setSortMode and invalidate any
// path caches that depend on the order.
class SortPickerActivity final : public Activity {
 public:
  SortPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                     CollectionSort currentMode, bool allowManual)
      : Activity("SortPicker", renderer, mappedInput),
        title(std::move(title)),
        currentMode(currentMode),
        allowManual(allowManual) {
    // Build the option list at construction so it stays stable across
    // re-renders. Order matches the CollectionSort enum.
    if (allowManual) options.push_back(CollectionSort::Manual);
    options.push_back(CollectionSort::TitleAlpha);
    options.push_back(CollectionSort::TitleAlphaDesc);
    options.push_back(CollectionSort::AuthorAlpha);
    options.push_back(CollectionSort::AuthorAlphaDesc);
    options.push_back(CollectionSort::DateAddedDesc);
    options.push_back(CollectionSort::DateAddedAsc);
    options.push_back(CollectionSort::DateLastReadDesc);
    // Pre-select the current mode if it's in the list; otherwise
    // default to index 0.
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i] == currentMode) {
        selectedIndex = static_cast<int>(i);
        break;
      }
    }
  }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  CollectionSort currentMode;
  bool allowManual;
  std::vector<CollectionSort> options;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

 public:
  // CrumBLE: exposed so callers (HomeActivity's shelf-header menu) can
  // surface the active sort mode in their own UI without duplicating
  // the enum -> string switch.
  static const char* labelFor(CollectionSort mode);
};
