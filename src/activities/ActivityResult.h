#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

struct WifiResult {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct KeyboardResult {
  std::string text;
};

struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  bool settingsChanged = false;
  // CrumBLE: BookSettingsDrawer's Bluetooth entry sets this when the user
  // has no bonded remote and we need to launch the BT settings UI for
  // first-time pairing. The reader's drawer result handler picks it up.
  bool requestBluetoothFlow = false;
  // CrumBLE: BluetoothSettingsActivity sets this when it auto-exited after
  // a successful connect (exitOnSuccessfulConnect=true). The EpubReaderMenu
  // result handler treats it as a signal to also finish itself so the user
  // lands directly back in the book instead of in the reader menu.
  bool autoExitParent = false;
};

struct ChapterResult {
  int spineIndex = 0;
};

struct PercentResult {
  int percent = 0;
};

struct IntervalResult {
  uint32_t value = 0;
};

struct PageResult {
  uint32_t page = 0;
};

struct SyncResult {
  int spineIndex = 0;
  int page = 0;
};

enum class NetworkMode;

struct NetworkModeResult {
  NetworkMode mode;
};

struct FootnoteResult {
  std::string href;
};

struct BookmarkResult {
  uint16_t spineIndex = 0;
  float progress = 0.0f;
};

struct FileBrowserActionResult {
  int action = -1;
};

// CrumBLE Collections sort picker — value matches CollectionSort enum.
struct SortPickerResult {
  int sortMode = 0;  // 0 = Manual, 1 = TitleAlpha, 2 = DateAddedDesc, 3 = DateAddedAsc
};

struct FilePathResult {
  std::string path;
};

using ResultVariant = std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult,
                                   IntervalResult, PageResult, SyncResult, NetworkModeResult, FootnoteResult,
                                   BookmarkResult, FileBrowserActionResult, FilePathResult, SortPickerResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
