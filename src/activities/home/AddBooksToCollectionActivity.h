#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE — bulk-add picker for a user collection. Lists the books
// currently in RECENT_BOOKS (most-recently-opened first) with a
// checkbox prefix showing current membership in the target
// collection. Confirm on a row toggles membership immediately.
// Back exits. The header shows the target collection's name so
// the user has unambiguous context for which collection they're
// modifying.
//
// Why RECENT_BOOKS as the pool: the most common case is "I've
// recently been reading book X and want to file it" — those books
// are right there. Books that haven't been opened yet can be added
// via the existing per-book long-press → "Add to collection..."
// flow (which lets you pick any collection from anywhere in the
// file browser). This activity is a convenience for batching,
// not a replacement.

class AddBooksToCollectionActivity final : public Activity {
 public:
  AddBooksToCollectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string collectionId,
                               std::string collectionName)
      : Activity("AddBooksToCollection", renderer, mappedInput),
        collectionId(std::move(collectionId)),
        collectionName(std::move(collectionName)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string collectionId;
  std::string collectionName;
  // Snapshot of RECENT_BOOKS at onEnter so the row count stays
  // stable across the picker's lifetime (in case RECENT_BOOKS
  // mutates from elsewhere — unlikely but defensive).
  std::vector<std::string> recentPaths;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  std::string rowLabel(int idx) const;
};
