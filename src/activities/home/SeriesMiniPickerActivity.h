#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE Collections — mini-picker for a series cell on the shelf.
// Confirm on a series cell (or long-press, depending on context)
// opens this. Lists the series' books in series-index order. Each row
// is openable (short Confirm → reader) or long-pressable (→ existing
// per-book action menu). A final "Options..." row at the bottom
// triggers series-level operations: short Confirm there invokes the
// caller-supplied options handler (HomeActivity wires this to a
// CollectionPickerActivity for "Add to collection..." applied to all
// series members).

class SeriesMiniPickerActivity final : public Activity {
 public:
  // Callbacks let HomeActivity stay in control of routing without
  // adding dependencies on its private methods. Both are invoked
  // BEFORE finish() returns, so the activity stays alive long enough
  // to push child activities.
  using OnOpenBook = std::function<void(const std::string& bookPath)>;
  using OnBookLongPress = std::function<void(const std::string& bookPath)>;
  using OnOptions = std::function<void()>;

  SeriesMiniPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string seriesName,
                           std::vector<std::string> memberPaths, OnOpenBook onOpen, OnBookLongPress onLongPress,
                           OnOptions onOptions)
      : Activity("SeriesMiniPicker", renderer, mappedInput),
        seriesName(std::move(seriesName)),
        memberPaths(std::move(memberPaths)),
        onOpen(std::move(onOpen)),
        onLongPress(std::move(onLongPress)),
        onOptions(std::move(onOptions)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string seriesName;
  std::vector<std::string> memberPaths;
  OnOpenBook onOpen;
  OnBookLongPress onLongPress;
  OnOptions onOptions;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  // Long-press detection: same threshold as FileBrowser (1000 ms).
  bool longPressHandled = false;

  // The "Options..." row sits at index memberPaths.size().
  int totalRowCount() const { return static_cast<int>(memberPaths.size()) + 1; }
  bool isOptionsRow(int idx) const { return idx == static_cast<int>(memberPaths.size()); }

  // Returns the row label. For book rows, derives from filename.
  std::string labelFor(int idx) const;
};
