#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE #81: full-screen list modal for picking which collection's
// books to view in the Bookshelf grid. Triggered by long-press on the
// home icon-bar's Bookshelf entry. Visual style mirrors
// ReaderOptionsActivity (header + scrollable list + button hints) so
// the two modals feel like siblings instead of a popup-vs-list mix.
//
// Result: ChoicePromptResult { choice }. choice == -1 means Back/Cancel;
// choice >= 0 indexes into the labels vector the caller passed in.
class BookshelfPickerActivity final : public Activity {
 public:
  // labels: collection names to show in the list (1:1 with caller-side
  // id table; the caller maps choice back to a collection id).
  // currentIndex: which row to mark as currently-active with the
  // "Selected" trailing label, mirroring FontSelectionActivity. Pass -1
  // if none should be marked.
  BookshelfPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<std::string> labels,
                          int currentIndex);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  ButtonNavigator buttonNavigator_;
  std::vector<std::string> labels_;
  int currentIndex_ = -1;
  int selectedIndex_ = 0;
  // The picker is spawned from a long-press of Confirm. Without this
  // gate, the eventual Confirm release would immediately re-fire as a
  // "select" on whatever row currentIndex_ landed on. Mirrors
  // ChoicePromptActivity's ignoreConfirmRelease_.
  bool ignoreConfirmRelease_ = true;
};
