#include "RearrangeCollectionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "../ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kTitleFontId = UI_10_FONT_ID;
constexpr int kTitleMaxLines = 2;
constexpr int kCompactTitleY = 14;
constexpr int kTallHeaderTitleBottomPadding = 8;
constexpr int kCompactHeaderTitleBottomPadding = 4;
constexpr int kTitleLineGap = 1;
constexpr int kBatteryTextReserveWidth = 90;
}  // namespace

void RearrangeCollectionsActivity::onEnter() {
  Activity::onEnter();
  markByIndex.assign(items.size(), 0);
  nextMarkNumber = 1;
  selectedIndex = items.empty() ? 0 : firstUnmarkedFrom(0);
  requestUpdate();
}

int RearrangeCollectionsActivity::firstUnmarkedFrom(int start) const {
  const int n = static_cast<int>(items.size());
  if (n == 0) return -1;
  for (int step = 0; step < n; ++step) {
    const int idx = (start + step) % n;
    if (markByIndex[idx] == 0) return idx;
  }
  return -1;
}

void RearrangeCollectionsActivity::moveSelection(int dir) {
  const int n = static_cast<int>(items.size());
  if (n == 0) return;
  for (int step = 1; step <= n; ++step) {
    const int idx = ((selectedIndex + dir * step) % n + n) % n;
    if (markByIndex[idx] == 0) {
      selectedIndex = idx;
      return;
    }
  }
  // Fully marked -- caller's loop should have detected completion already.
}

void RearrangeCollectionsActivity::loop() {
  if (ignoreConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
      return;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      ignoreConfirmRelease = false;
    }
  }

  // Back: pop the most recent mark if any, else cancel out of the activity
  // entirely. This is the "Undo / Back" duality from the spec.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (nextMarkNumber > 1) {
      const int targetMark = nextMarkNumber - 1;
      for (int i = 0; i < static_cast<int>(markByIndex.size()); ++i) {
        if (markByIndex[i] == targetMark) {
          markByIndex[i] = 0;
          selectedIndex = i;  // park cursor on the un-done item
          break;
        }
      }
      nextMarkNumber--;
      requestUpdate();
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (items.empty()) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    // Guard against double-confirm on the already-marked item (e.g. if user
    // somehow lingers on it). Should be unreachable since moveSelection
    // skips marked items, but harmless to check.
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(markByIndex.size()) ||
        markByIndex[selectedIndex] != 0) {
      return;
    }
    markByIndex[selectedIndex] = nextMarkNumber++;

    // If every item now has a mark, emit the final order and exit.
    if (nextMarkNumber > static_cast<int>(items.size())) {
      RearrangeCollectionsResult out;
      out.orderedIds.resize(items.size());
      for (size_t i = 0; i < items.size(); ++i) {
        // markByIndex[i] is 1..N; place id at position (mark - 1).
        out.orderedIds[markByIndex[i] - 1] = items[i].id;
      }
      setResult(ActivityResult{std::move(out)});
      finish();
      return;
    }

    // Advance to the next unmarked item (wraps around if needed).
    moveSelection(/*dir=*/+1);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] {
    moveSelection(/*dir=*/+1);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    moveSelection(/*dir=*/-1);
    requestUpdate();
  });
}

void RearrangeCollectionsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int titleX = metrics.contentSidePadding;
  const int titleMaxWidth = std::max(0, pageWidth - titleX - metrics.contentSidePadding - kBatteryTextReserveWidth);

  const std::string titleText = tr(STR_REARRANGE_COLLECTIONS_TITLE);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, titleText.c_str(), titleMaxWidth, kTitleMaxLines, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = static_cast<int>(titleLines.size()) * titleLineHeight +
                               std::max(0, static_cast<int>(titleLines.size()) - 1) * kTitleLineGap;
  const bool tallHeader = metrics.headerHeight > 60;
  const int titleY = metrics.topPadding + (tallHeader ? metrics.batteryBarHeight + 3 : kCompactTitleY);
  const int titleBottomPadding = tallHeader ? kTallHeaderTitleBottomPadding : kCompactHeaderTitleBottomPadding;
  const int actionHeaderHeight =
      std::max(metrics.headerHeight, titleY - metrics.topPadding + titleBlockHeight + titleBottomPadding);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, actionHeaderHeight}, "");

  for (int i = 0; i < static_cast<int>(titleLines.size()); ++i) {
    renderer.drawText(kTitleFontId, titleX, titleY + i * (titleLineHeight + kTitleLineGap), titleLines[i].c_str(), true,
                      EpdFontFamily::BOLD);
  }

  const int contentTop = metrics.topPadding + actionHeaderHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  // Marked rows show their mark number right-justified; unmarked rows have
  // no value text. Dimmed flag makes already-marked rows visually quieter so
  // the user can focus on what's left to mark.
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(items.size()), selectedIndex,
      [this](int index) { return items[index].name; },
      /*rowSubtitle=*/nullptr,
      /*rowIcon=*/nullptr,
      [this](int index) {
        if (markByIndex[index] == 0) return std::string{};
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", markByIndex[index]);
        return std::string(buf);
      },
      /*highlightValue=*/true,
      /*rowDimmed=*/[this](int index) { return markByIndex[index] != 0; });

  // Button hints: Back is dynamic ("< Back" when no marks, "Undo" mid-flow),
  // Confirm tells the user which mark number is next.
  char markLabelBuf[16];
  snprintf(markLabelBuf, sizeof(markLabelBuf), "Mark %d", nextMarkNumber);
  const char* backLabel = (nextMarkNumber > 1) ? tr(STR_UNDO) : tr(STR_BACK);
  const auto labels = mappedInput.mapLabels(backLabel, markLabelBuf, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
