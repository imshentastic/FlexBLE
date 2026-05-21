#include "BookSettingsDrawerActivity.h"

#include <BluetoothHIDManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool isLandscape(const GfxRenderer& r) {
  const auto o = r.getOrientation();
  return o == GfxRenderer::Orientation::LandscapeClockwise || o == GfxRenderer::Orientation::LandscapeCounterClockwise;
}

// Read the value text for a single SettingInfo-bound row.
std::string valueTextForSetting(const SettingInfo& info) {
  if (info.type == SettingType::TOGGLE && info.valuePtr != nullptr) {
    return SETTINGS.*(info.valuePtr) ? I18N.get(StrId::STR_STATE_ON) : I18N.get(StrId::STR_STATE_OFF);
  }
  if (info.type == SettingType::ENUM && info.valuePtr != nullptr) {
    const uint8_t cur = SETTINGS.*(info.valuePtr);
    if (cur < info.enumValues.size()) {
      return I18N.get(info.enumValues[cur]);
    }
    return std::string{};
  }
  if (info.type == SettingType::VALUE && info.valuePtr != nullptr) {
    return std::to_string(SETTINGS.*(info.valuePtr));
  }
  return std::string{};
}

void applyDeltaToSetting(const SettingInfo& info, int delta) {
  if (info.valuePtr == nullptr) return;
  if (info.type == SettingType::TOGGLE) {
    SETTINGS.*(info.valuePtr) = (SETTINGS.*(info.valuePtr) == 0) ? 1 : 0;
    return;
  }
  if (info.type == SettingType::ENUM) {
    if (info.enumValues.empty()) return;
    const int count = static_cast<int>(info.enumValues.size());
    int next = static_cast<int>(SETTINGS.*(info.valuePtr)) + delta;
    next = ((next % count) + count) % count;
    SETTINGS.*(info.valuePtr) = static_cast<uint8_t>(next);
    return;
  }
  if (info.type == SettingType::VALUE) {
    const int step = std::max<int>(1, info.valueRange.step);
    int next = static_cast<int>(SETTINGS.*(info.valuePtr)) + delta * step;
    if (next < info.valueRange.min) next = info.valueRange.max;
    if (next > info.valueRange.max) next = info.valueRange.min;
    SETTINGS.*(info.valuePtr) = static_cast<uint8_t>(next);
  }
}

}  // namespace

BookSettingsDrawerActivity::BookSettingsDrawerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("BookSettingsDrawer", renderer, mappedInput) {}

void BookSettingsDrawerActivity::onEnter() {
  Activity::onEnter();

  // Cache the underlying reader page so we can preserve it through every
  // partial-refresh redraw without re-rendering the EPUB. If the malloc
  // for the compressed backup fails (heap fragmentation under BLE
  // pressure is the usual cause), renderDrawer() falls back to either
  // the reader's existing backup or to whatever's already in the
  // framebuffer — both better than painting onto a cleared screen.
  readerBufferStored = renderer.storeBwBuffer();
  if (!readerBufferStored) {
    LOG_INF("BSD", "Failed to snapshot reader page; drawer will fall back to existing BW backup or in-place framebuffer");
  }

  buildItems();
  layoutDrawer();
  selectedIndex = 0;
  scrollOffset = 0;
  initialConfirmReleased = false;
  requestUpdate();
}

void BookSettingsDrawerActivity::onExit() {
  Activity::onExit();
  // Persist any changes the user made before the reader resumes.
  SETTINGS.saveToFile();
}

void BookSettingsDrawerActivity::buildItems() {
  items.clear();

  // 1) Pull every Reader-category non-Action setting, in declaration order.
  //
  // `getSettingsList()` returns std::vector<SettingInfo> BY VALUE. The
  // returned vector is a temporary that lives only for the duration of this
  // range-for loop; any pointer captured into `info` becomes dangling once
  // the loop exits. We used to do `infoPtr = &info` and bake that pointer
  // into the lambda closures stored on each Item — which "worked" right
  // after onEnter (the freed bytes were still intact) but broke after the
  // next allocation overwrote them. Loading a new font in particular
  // triggered enough heap churn that the SettingInfos for items right after
  // Font (Line Spacing, Orientation) got clobbered first; their enumValues
  // vector read as garbage and I18N.get(garbage_id) rendered as "????".
  //
  // Fix: copy SettingInfo by value into each lambda's closure. The
  // valuePtr / valueGetter / enumValues members are all owned-by-value, so
  // the copy stays valid as long as the lambda lives. SETTINGS itself is a
  // global, and the pointer-to-member in valuePtr is stable regardless of
  // where the SettingInfo copy lives.
  for (const auto& info : getSettingsList()) {
    if (info.category != StrId::STR_CAT_READER) continue;
    if (info.type == SettingType::ACTION || info.type == SettingType::SECTION_HEADER) continue;
    Item item;
    item.nameId = info.nameId;
    item.getValueText = [infoCopy = info]() { return valueTextForSetting(infoCopy); };
    item.change = [infoCopy = info](int delta) { applyDeltaToSetting(infoCopy, delta); };
    items.push_back(std::move(item));
  }

  // 2) Bluetooth quick-action. One-shot button: enables BLE (if needed) and
  // reconnects to the bonded remote synchronously, then closes the drawer.
  // If the user has no bonded remote, instead closes the drawer with a flag
  // asking the reader to launch the full BT settings UI for pairing.
  {
    Item bt;
    bt.nameId = StrId::STR_BT_QUICK_CONNECT;
    bt.isAction = true;
    bt.activate = [this]() {
      auto& btMgr = BluetoothHIDManager::getInstance();
      const bool hasBonded = SETTINGS.bleBondedDeviceAddr[0] != '\0';
      if (!hasBonded) {
        // No saved remote — bounce out to the BT settings activity so the
        // user can scan and pair. Reader's drawer result handler picks up
        // the flag and launches the BT UI.
        MenuResult result;
        result.settingsChanged = settingsChanged;
        result.requestBluetoothFlow = true;
        setResult(ActivityResult{result});
        finish();
        return;
      }
      // Show a "Connecting..." popup while the synchronous connect runs
      // (~2-3 s for NimBLE + GATT handshake). The user is still inside the
      // drawer render; we just paint over it.
      GUI.drawPopup(renderer, tr(STR_CONNECTING));
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      if (!btMgr.isEnabled()) {
        btMgr.enable();
      }
      btMgr.connectToDevice(SETTINGS.bleBondedDeviceAddr);
      // Whether connect succeeded or not, close the drawer. (If it failed,
      // the BT stack stays enabled — auto-reconnect will retry on next
      // button press.) Drawer dismissal mirrors the existing Back path.
      MenuResult result;
      result.settingsChanged = settingsChanged;
      setResult(ActivityResult{result});
      finish();
    };
    items.push_back(std::move(bt));
  }
}

void BookSettingsDrawerActivity::layoutDrawer() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  if (isLandscape(renderer)) {
    drawerW = sw / 2;
    drawerX = sw - drawerW;
    drawerY = 0;
    drawerH = sh;
  } else {
    drawerX = 0;
    drawerW = sw;
    drawerH = (sh * 60) / 100;
    drawerY = sh - drawerH;
  }
  const int listRegion = drawerH - kTabOverlap - kListTopPad - hintsHeight;
  itemsVisible = std::max(1, listRegion / itemHeight);
}

void BookSettingsDrawerActivity::clampSelection() {
  if (items.empty()) {
    selectedIndex = 0;
    return;
  }
  const int n = static_cast<int>(items.size());
  if (selectedIndex < 0) selectedIndex = n - 1;
  if (selectedIndex >= n) selectedIndex = 0;
}

void BookSettingsDrawerActivity::adjustScrollToSelection() {
  if (items.empty()) return;
  if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  } else if (selectedIndex >= scrollOffset + itemsVisible) {
    scrollOffset = selectedIndex - itemsVisible + 1;
  }
  scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, static_cast<int>(items.size()) - itemsVisible)));
}

void BookSettingsDrawerActivity::changeSelected(int delta) {
  if (items.empty()) return;
  auto& item = items[selectedIndex];
  if (item.isAction) {
    if (delta > 0 && item.activate) item.activate();
    return;
  }
  if (item.change) {
    item.change(delta);
    settingsChanged = true;
  }
}

void BookSettingsDrawerActivity::activateSelected() {
  if (items.empty()) return;
  auto& item = items[selectedIndex];
  if (item.isAction) {
    if (item.activate) item.activate();
    return;
  }
  if (item.change) {
    item.change(+1);
    settingsChanged = true;
  }
}

void BookSettingsDrawerActivity::loop() {
  const bool landscape = isLandscape(renderer);
  const auto& mi = mappedInput;
  using B = MappedInputManager::Button;

  // Back, navigation, and value-adjust are accepted immediately — the user
  // may hit Back to dismiss while still holding the long-press Confirm.
  if (mi.wasReleased(B::Back)) {
    // Pass the settings-changed flag back to the reader via MenuResult so its
    // result handler can skip the re-layout when nothing was modified.
    MenuResult result;
    result.settingsChanged = settingsChanged;
    setResult(ActivityResult{result});
    finish();
    return;
  }

  // List navigation: portrait uses Up/Down, landscape uses Right/Left.
  const bool prevList = mi.wasReleased(landscape ? B::Right : B::Up);
  const bool nextList = mi.wasReleased(landscape ? B::Left : B::Down);
  if (prevList) {
    selectedIndex--;
    clampSelection();
    adjustScrollToSelection();
    requestUpdate();
    return;
  }
  if (nextList) {
    selectedIndex++;
    clampSelection();
    adjustScrollToSelection();
    requestUpdate();
    return;
  }

  // Value adjust: portrait Left/Right, landscape Down/Up.
  const bool decrease = mi.wasReleased(landscape ? B::Down : B::Left);
  const bool increase = mi.wasReleased(landscape ? B::Up : B::Right);
  if (decrease) {
    changeSelected(-1);
    requestUpdate();
    return;
  }
  if (increase) {
    changeSelected(+1);
    requestUpdate();
    return;
  }

  // Confirm is special: the FIRST Confirm release we see is from the long
  // press that opened the drawer. Swallow it once, then accept Confirm
  // normally as "activate selected".
  if (mi.wasReleased(B::Confirm)) {
    if (!initialConfirmReleased) {
      initialConfirmReleased = true;
      return;
    }
    activateSelected();
    requestUpdate();
    return;
  }
}

void BookSettingsDrawerActivity::renderDrawer() {
  // Repaint reader page underneath, then draw the drawer panel on top.
  //
  // Three tiers of fallback for getting the reader page into the
  // framebuffer:
  //   1. Our own storeBwBuffer() snapshot from onEnter, if it succeeded.
  //   2. Otherwise, whatever backup the renderer already had — the
  //      EpubReader stores its own BW backup at the end of each page
  //      render (for the grayscale pass) and doesn't free it after
  //      restoring, so it usually sits there waiting for us.
  //   3. Last resort: do NOT clear the screen. Whatever was last
  //      rendered is what the e-ink is currently showing; clearing would
  //      diff every reader-page pixel against white on the next fast
  //      refresh and paint the panel onto a stark white background.
  //      Leaving the framebuffer alone matches whatever's already on
  //      the display so the fast-refresh diff is small.
  if (readerBufferStored) {
    renderer.restoreBwBuffer();
  } else if (renderer.hasStoredBwBuffer()) {
    renderer.restoreBwBuffer();
  }
  // else: intentionally no clearScreen() — see comment above.

  // Panel body — filled white, with the top two corners rounded so the panel
  // looks like it has rounded shoulders.
  const int panelTopY = drawerY + kTabOverlap;
  const int panelBodyH = drawerH - kTabOverlap;
  renderer.fillRoundedRect(drawerX, panelTopY, drawerW, panelBodyH, kPanelCornerRadius,
                           /*topL=*/true, /*topR=*/true, /*botL=*/false, /*botR=*/false, Color::White);

  // Panel top edge — a horizontal line between the rounded corners. The middle
  // section will be overpainted by the tab below so it appears to pass "behind"
  // the tab outline.
  renderer.drawLine(drawerX + kPanelCornerRadius, panelTopY,
                    drawerX + drawerW - kPanelCornerRadius - 1, panelTopY, 2, true);
  // Quarter-circle outlines for the panel's top corners (matches the fill).
  renderer.drawArc(kPanelCornerRadius, drawerX + kPanelCornerRadius, panelTopY + kPanelCornerRadius,
                   -1, -1, 2, true);
  renderer.drawArc(kPanelCornerRadius, drawerX + drawerW - kPanelCornerRadius - 1,
                   panelTopY + kPanelCornerRadius, 1, -1, 2, true);

  // Tab — centred above the panel's top edge. Width auto-fits the header text
  // with horizontal padding; bottom of the tab extends kTabOverlap into the
  // panel so the panel's top line falls inside the tab.
  const char* headerText = "Global Book Settings";
  const int headerTextW = renderer.getTextWidth(UI_12_FONT_ID, headerText, EpdFontFamily::BOLD);
  const int tabPadX = 18;
  const int tabW = std::min(drawerW - 40, headerTextW + 2 * tabPadX);
  const int tabX = drawerX + (drawerW - tabW) / 2;
  const int tabY = drawerY;
  const int tabBottomY = tabY + kTabHeight;

  // 1) Erase the panel's top line where it would pass through the tab.
  renderer.fillRoundedRect(tabX, tabY, tabW, kTabHeight, kTabCornerRadius,
                           /*topL=*/true, /*topR=*/true, /*botL=*/false, /*botR=*/false, Color::White);

  // 2) Tab outline — top + rounded top corners + left and right sides only.
  // Bottom side intentionally omitted so the tab visually merges with the panel.
  renderer.drawLine(tabX + kTabCornerRadius, tabY, tabX + tabW - kTabCornerRadius - 1, tabY, 2, true);
  renderer.drawArc(kTabCornerRadius, tabX + kTabCornerRadius, tabY + kTabCornerRadius, -1, -1, 2, true);
  renderer.drawArc(kTabCornerRadius, tabX + tabW - kTabCornerRadius - 1, tabY + kTabCornerRadius, 1, -1, 2, true);
  renderer.drawLine(tabX, tabY + kTabCornerRadius, tabX, tabBottomY - 1, 2, true);
  renderer.drawLine(tabX + tabW - 1, tabY + kTabCornerRadius, tabX + tabW - 1, tabBottomY - 1, 2, true);

  // Tab text — vertically centred in the upper portion of the tab so it sits
  // above the panel's top line.
  const int tabTextX = tabX + (tabW - headerTextW) / 2;
  const int tabTextY = tabY + 6;
  renderer.drawText(UI_12_FONT_ID, tabTextX, tabTextY, headerText, true, EpdFontFamily::BOLD);

  // Item list — starts a small pad below the panel's top line.
  const int listStartY = panelTopY + kListTopPad;
  const int leftPad = 12;
  const int rightPad = 12;
  // Renderer.drawText uses the same Y as the highlight rect top in the existing
  // reader-menu pattern; add a small top padding so the glyphs aren't flush
  // against the rect edge.
  const int rowTextY = 6;

  for (int i = 0; i < itemsVisible; ++i) {
    const int idx = scrollOffset + i;
    if (idx >= static_cast<int>(items.size())) break;
    const Item& item = items[idx];
    const int rowY = listStartY + i * itemHeight;
    const bool selected = (idx == selectedIndex);
    if (selected) {
      renderer.fillRect(drawerX + 1, rowY, drawerW - 2, itemHeight, true);
    }
    const char* name = I18N.get(item.nameId);
    const std::string value = item.getValueText ? item.getValueText() : std::string{};
    const bool textBlack = !selected;
    renderer.drawText(UI_12_FONT_ID, drawerX + leftPad, rowY + rowTextY, name, textBlack);
    if (!value.empty()) {
      const int valueWidth = renderer.getTextWidth(UI_12_FONT_ID, value.c_str());
      renderer.drawText(UI_12_FONT_ID, drawerX + drawerW - rightPad - valueWidth, rowY + rowTextY, value.c_str(),
                         textBlack);
    } else if (item.isAction) {
      const char* arrow = "→";
      const int aw = renderer.getTextWidth(UI_12_FONT_ID, arrow);
      renderer.drawText(UI_12_FONT_ID, drawerX + drawerW - rightPad - aw, rowY + rowTextY, arrow, textBlack);
    }
  }

  // Scroll indicator: small block on the right edge if list overflows.
  if (static_cast<int>(items.size()) > itemsVisible) {
    const int trackH = itemsVisible * itemHeight;
    const int barH = std::max(8, (trackH * itemsVisible) / static_cast<int>(items.size()));
    const int barY = listStartY + (trackH - barH) * scrollOffset /
                                       std::max(1, static_cast<int>(items.size()) - itemsVisible);
    renderer.fillRect(drawerX + drawerW - 4, barY, 2, barH, true);
  }

  // Button hints.
  const auto& mi = mappedInput;
  const auto labels = mi.mapLabels(I18N.get(StrId::STR_BACK), I18N.get(StrId::STR_TOGGLE),
                                    I18N.get(StrId::STR_DIR_UP), I18N.get(StrId::STR_DIR_DOWN));
  const int hintY = drawerY + drawerH - hintsHeight + 16;
  std::string hintLine = std::string(labels.btn3) + "/" + labels.btn4 + " · " + labels.btn2 + " · " + labels.btn1;
  renderer.drawCenteredText(SMALL_FONT_ID, hintY, hintLine.c_str(), true);
}

void BookSettingsDrawerActivity::presentFastRefresh() {
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BookSettingsDrawerActivity::render(RenderLock&&) {
  renderDrawer();
  presentFastRefresh();
}
