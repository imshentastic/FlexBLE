#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

enum class FileBrowserAction : int {
  Delete = 0,
  PinFavorite = 1,
  UnpinFavorite = 2,
  DeleteCache = 3,
  ToggleCompleted = 4,
  // FlexBLE Collections (phase 2): opens a checkbox-list picker for the
  // book against ALL collections, with a "+ New collection..." option.
  // Replaces the phase-1 AddToFavorites/RemoveFromFavorites pair.
  AddToCollection = 5,
  // FlexBLE: long-press action menu on the home carousel / shelf shows this
  // so the user can clear a book off the Recent Books list without having
  // to clear its read state in the file browser. Only meaningful from the
  // home screen; the file browser doesn't expose it.
  RemoveFromRecentBooks = 7,
  // FlexBLE: long-press the shelf header (collection tab) on the Flow
  // theme to invoke this. Walks SD card looking for newly-added books
  // and refreshes the LibraryIndex so they appear in Recently Added /
  // All Books. Only surfaced from the home shelf header — not the file
  // browser or per-book action menu.
  RescanLibrary = 8,
  // FlexBLE: also on the shelf-header action menu. Opens a
  // SortPickerActivity for the active collection. Hidden for the
  // "Recently Added" virtual collection since its sort is intrinsic.
  SortBy = 9,
};

class FileBrowserActionActivity final : public Activity {
 public:
  struct MenuItem {
    FileBrowserAction action;
    StrId labelId;
  };

  FileBrowserActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                            std::vector<MenuItem> items, bool ignoreInitialConfirmRelease = false)
      : Activity("FileBrowserAction", renderer, mappedInput),
        title(std::move(title)),
        items(std::move(items)),
        ignoreConfirmRelease(ignoreInitialConfirmRelease) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::string title;
  std::vector<MenuItem> items;
  int selectedIndex = 0;
  bool ignoreConfirmRelease = false;
};
