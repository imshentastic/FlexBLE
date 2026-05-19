#include "BookMetadataViewerActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"

namespace {
// Convenience: returns either `value` or the i18n "(none)" sentinel
// when value is empty. Centralizes the empty-vs-missing presentation
// so the row labels read consistently.
std::string orNone(const std::string& value) {
  return value.empty() ? std::string(tr(STR_METADATA_NONE)) : value;
}
}  // namespace

void BookMetadataViewerActivity::buildRows() {
  rows.clear();
  // Path is always available — show it first so the user can confirm
  // they're inspecting the book they think they are.
  rows.emplace_back(tr(STR_METADATA_PATH), bookPath);

  // Format derives from extension. Used as a hint when the body fields
  // come back empty for non-EPUB types.
  std::string format;
  if (FsHelpers::hasEpubExtension(bookPath)) format = "EPUB";
  else if (FsHelpers::hasXtcExtension(bookPath)) format = "XTC";
  else if (FsHelpers::hasTxtExtension(bookPath)) format = "TXT";
  else if (FsHelpers::hasMarkdownExtension(bookPath)) format = "Markdown";
  else format = "Unknown";
  rows.emplace_back(tr(STR_METADATA_FORMAT), format);

  if (!FsHelpers::hasEpubExtension(bookPath)) {
    // Only EPUB has the OPF metadata we care about (title/author/series).
    // For other formats we just show the path + format and stop —
    // populating the remaining rows with "(none)" would be misleading.
    return;
  }

  // Load Epub: populates title/author/language. extractSeriesFromOpf
  // separately captures series fields (load() short-circuits and
  // doesn't capture them when book.bin already exists). Together
  // these give the complete picture.
  Epub epub(bookPath, "/.crosspoint");
  std::string title;
  std::string author;
  std::string language;
  if (epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true)) {
    title = epub.getTitle();
    author = epub.getAuthor();
    language = epub.getLanguage();
  }
  // Fresh OPF parse JUST for series — bypasses the book.bin cache.
  epub.extractSeriesFromOpf();
  const std::string seriesName = epub.getSeriesName();
  const std::string seriesIndex = epub.getSeriesIndex();

  rows.emplace_back(tr(STR_METADATA_TITLE), orNone(title));
  rows.emplace_back(tr(STR_METADATA_AUTHOR), orNone(author));
  rows.emplace_back(tr(STR_METADATA_LANGUAGE), orNone(language));
  rows.emplace_back(tr(STR_METADATA_SERIES), orNone(seriesName));
  rows.emplace_back(tr(STR_METADATA_SERIES_INDEX), orNone(seriesIndex));
}

void BookMetadataViewerActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  // Show a loading popup since the EPUB parse can take 100-500 ms on
  // larger books. Without this the screen stays blank during the work.
  const Rect popupRect = GUI.drawPopup(renderer, "Reading metadata...");
  GUI.fillPopupProgress(renderer, popupRect, 50);
  buildRows();
  GUI.fillPopupProgress(renderer, popupRect, 100);
  requestUpdate();
}

void BookMetadataViewerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult result;
    result.isCancelled = false;
    setResult(std::move(result));
    finish();
    return;
  }

  const int count = static_cast<int>(rows.size());
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void BookMetadataViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_METADATA_TITLE_HEADER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(rows.size()), selectedIndex,
               [this](int index) {
                 if (index < 0 || index >= static_cast<int>(rows.size())) return std::string{};
                 // "Label: Value" on one row. drawList will truncate
                 // long values (especially paths) with ellipsis; the
                 // user can scroll to see them in full via the
                 // selection highlight which usually fits more width.
                 return rows[index].first + ": " + rows[index].second;
               });

  // Just Back to dismiss — no editing here.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_BACK), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
