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
  // FlexBLE Collections (phase 1): toggle membership in the Favorites
  // collection. Replaced by a generic picker in phase 2.
  AddToFavorites = 5,
  RemoveFromFavorites = 6,
  // FlexBLE: long-press action menu on the home carousel / shelf shows this
  // so the user can clear a book off the Recent Books list without having
  // to clear its read state in the file browser. Only meaningful from the
  // home screen; the file browser doesn't expose it.
  RemoveFromRecentBooks = 7,
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
