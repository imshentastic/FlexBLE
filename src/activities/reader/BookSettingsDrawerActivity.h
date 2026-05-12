#pragma once

// Bottom-drawer-style quick settings menu shown in the reader on long-press of
// the menu button. Architecture and partial-refresh approach are adapted from
// inx (MIT, Copyright 2025 Dave Allie):
//   https://github.com/obijuankenobiii/inx — src/activity/reader/Epub/SettingsDrawer.{h,cpp}
//
// The drawer overlays the bottom ~60% of the screen (or right half in
// landscape) on top of the live reader page. It uses FAST_REFRESH so toggling
// items doesn't sweep the panel — only the changed pixels in the drawer
// region get repainted. The reader page underneath is preserved by storing
// the framebuffer on entry and restoring it before every drawer redraw.

#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "../settings/SettingsActivity.h"

class BookSettingsDrawerActivity final : public Activity {
 public:
  explicit BookSettingsDrawerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  // A single row in the drawer.
  //   - Setting-bound rows wrap a SettingInfo for value-getter/toggler.
  //   - The two BLE rows are special and use the activate callback.
  struct Item {
    StrId nameId;
    bool isAction = false;            // true: Confirm activates; false: Confirm toggles/cycles value
    const SettingInfo* settingInfo = nullptr;  // non-null for setting-bound rows
    std::function<std::string()> getValueText;
    std::function<void(int delta)> change;
    std::function<void()> activate;
  };

  void buildItems();
  void layoutDrawer();
  void renderDrawer();
  void presentFastRefresh();
  void clampSelection();
  void adjustScrollToSelection();
  void changeSelected(int delta);
  void activateSelected();

  std::vector<Item> items;
  int selectedIndex = 0;
  int scrollOffset = 0;

  // Long-press Confirm pushes this activity while the button is still held.
  // We must swallow that release before accepting user input.
  bool initialConfirmReleased = false;

  // Set when the user actually changes a setting-bound row, so the result
  // handler in EpubReaderActivity can skip the section.reset() (which
  // triggers a re-layout) when the user just glanced at the drawer.
  bool settingsChanged = false;

  // True once the entry framebuffer (live reader page) has been stashed in
  // the renderer's internal BW buffer via storeBwBuffer(). We re-blit that
  // buffer before each drawer redraw so the reader page survives partial
  // refreshes and dismiss.
  bool readerBufferStored = false;

  // Drawer geometry, recomputed in layoutDrawer() from current orientation.
  int drawerX = 0;
  int drawerY = 0;
  int drawerW = 0;
  int drawerH = 0;
  int itemHeight = 40;
  int itemsVisible = 8;
  int hintsHeight = 28;

  // Tab + panel decoration constants. The tab sits centred above the panel's
  // top edge, with its bottom extending TAB_OVERLAP_PX into the panel so the
  // panel's top horizontal line appears to pass "behind" it.
  static constexpr int kPanelCornerRadius = 12;
  static constexpr int kTabHeight = 36;
  static constexpr int kTabOverlap = 16;          // tab bottom is at drawerY + kTabOverlap (panel top line)
  static constexpr int kTabCornerRadius = 8;
  // Space between the panel's top line and the first row, sized so the
  // selection highlight on the first row never reaches up into the tab.
  static constexpr int kListTopPad = 40;
};
