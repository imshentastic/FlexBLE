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
  // CrumBLE Collections (phase 2): opens a checkbox-list picker for the
  // book against ALL collections, with a "+ New collection..." option.
  // Replaces the phase-1 AddToFavorites/RemoveFromFavorites pair.
  AddToCollection = 5,
  // CrumBLE: long-press action menu on the home carousel / shelf shows this
  // so the user can clear a book off the Recent Books list without having
  // to clear its read state in the file browser. Only meaningful from the
  // home screen; the file browser doesn't expose it.
  RemoveFromRecentBooks = 7,
  // CrumBLE: long-press the shelf header (collection tab) on the Flow
  // theme to invoke this. Walks SD card looking for newly-added books
  // and refreshes the LibraryIndex so they appear in Recently Added /
  // All Books. Only surfaced from the home shelf header — not the file
  // browser or per-book action menu.
  RescanLibrary = 8,
  // CrumBLE: also on the shelf-header action menu. Opens a
  // SortPickerActivity for the active collection. Hidden for the
  // "Recently Added" virtual collection since its sort is intrinsic.
  SortBy = 9,
  // CrumBLE: shelf-header item that flips the collapseSeries flag
  // on the active collection. Label updates to reflect current
  // state ("Series collapse: ON" vs "...: OFF").
  ToggleCollapseSeries = 10,
  // CrumBLE debug: opens a viewer listing the book's OPF metadata
  // (title, author, language, series name + index). Useful when
  // series collapse isn't grouping books as expected — lets the user
  // verify whether the calibre:series tag is actually present in the
  // EPUB.
  ShowMetadata = 11,
  // CrumBLE: shelf-header items for user-collection management.
  // Rename opens the keyboard with the current name; Delete prompts
  // for confirmation then removes the collection (books on disk
  // untouched). Both hidden for virtual collections; Delete hidden
  // for the seeded Favorites.
  RenameCollection = 12,
  DeleteCollection = 13,
  // CrumBLE: shelf-header item available on ANY collection. Opens
  // the keyboard for a name then creates a new user collection and
  // switches active to it. A faster path than long-pressing a book
  // → "Add to collection..." → "+ New collection...". Useful from
  // virtual collections (Recently Added / All Books) where the
  // user-collection management items are otherwise hidden.
  CreateNewCollectionFromHeader = 14,
  // CrumBLE: shelf-header item on user collections. Opens a multi-
  // select picker over Recent Books so the user can add several
  // books to the active collection in one pass instead of long-
  // pressing each book individually.
  AddBooksToActiveCollection = 15,
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
