#pragma once

#include <string>
#include <vector>
#include <utility>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// CrumBLE — read-only viewer that dumps a book's OPF metadata so the
// user can sanity-check what fields are actually present in their
// files. Triggered from the long-press action menu's "Show metadata"
// entry. Pure inspection; no edits.
//
// Each row is "Label: Value". For EPUBs we run a fresh OPF parse
// (extractSeriesFromOpf + Epub::load) so the values reflect what's
// actually on disk, not just what's in book.bin. For XTC / TXT /
// MD the OPF doesn't apply — we show file format + path only.

class BookMetadataViewerActivity final : public Activity {
 public:
  BookMetadataViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath)
      : Activity("BookMetadataViewer", renderer, mappedInput), bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath;
  // Pairs of (label, value). Built once in onEnter so the render path
  // stays cheap. value == "(none)" for fields that were absent.
  std::vector<std::pair<std::string, std::string>> rows;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void buildRows();
};
