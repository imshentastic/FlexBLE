#pragma once
#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE: full-screen activity that lets the user reorder collections by
// tapping Confirm in the order they want them displayed. Each tap assigns
// the next sequential mark (1, 2, 3, ...). When every item is marked, the
// activity finishes and emits the new order via RearrangeCollectionsResult.
//
// Mid-rearrange Back (when at least one mark exists) pops the last mark
// instead of cancelling -- letting the user undo a misclick without
// restarting the whole sequence. Back with zero marks behaves like normal
// cancel (returns to the previous menu).
class RearrangeCollectionsActivity final : public Activity {
 public:
  struct Item {
    std::string id;
    std::string name;
  };

  RearrangeCollectionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Item> items,
                               bool ignoreInitialConfirmRelease = true)
      : Activity("RearrangeCollections", renderer, mappedInput),
        items(std::move(items)),
        ignoreConfirmRelease(ignoreInitialConfirmRelease) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  // 1-based mark per item; 0 means unmarked. Indexed in parallel with `items`.
  std::vector<int> markByIndex;
  int nextMarkNumber = 1;
  int selectedIndex = 0;
  bool ignoreConfirmRelease = true;

  // Move cursor to the next/previous unmarked row, wrapping around.
  void moveSelection(int dir);
  // First unmarked index >= start (wrapping), or -1 if none left.
  int firstUnmarkedFrom(int start) const;
};
