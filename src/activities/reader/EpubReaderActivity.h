#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <optional>
#include <string>

#include "BookReadingStats.h"
#include "BookmarkStore.h"
#include "EpubReaderMenuActivity.h"
#include "GlobalReadingStats.h"
#include "activities/Activity.h"

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  unsigned long sessionStartMs = 0UL;
  // Wall-clock anchor for the current "session segment" — reset by
  // commitReadingSession every time it banks elapsed time so we don't
  // double-count across deep-sleep / shutdown commits.
  unsigned long sessionSegmentStartMs = 0UL;
  // Cumulative session ms already banked into stats this opening. Used
  // only to gate sessionCount (the +1 happens once per session ≥ 60s,
  // even if multiple commits add up to >60s).
  unsigned long totalSessionMsThisOpen = 0UL;
  bool sessionCountedThisOpen = false;
  // Wall-clock anchor for the periodic incremental save. Reading
  // sessions that crash before onExit (e.g. brown-out, hard hang) used
  // to lose ALL elapsed time. Now we flush every kIncrementalSaveMs
  // milliseconds during loop() so worst-case loss is bounded.
  unsigned long lastIncrementalSaveMs = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  bool longPressMenuHandled = false;
  bool longPowerButtonHandled = false;
  bool sideButtonLongPressHandled = false;
  bool frontButtonLongPressHandled = false;
  int pageLoadRetryCount = 0;
  // CrumBLE: if a chapter layout aborts under heap pressure and BLE is
  // currently consuming its ~58 KB share, retry the layout once with BLE
  // disabled. Flag gates the retry so we don't loop forever if the
  // chapter genuinely can't be parsed.
  bool layoutBleRetryAttempted = false;
  // CrumBLE: when we proactively drop BLE around a heavy re-layout (drawer
  // settings change, or the reactive chapter-abort retry), set this so the
  // next successful section build re-enables BLE via requestEnableLater().
  // Without this, the user is stuck without their remote until they go to
  // Reader Menu -> Bluetooth to re-enable manually — checkAutoReconnect()
  // refuses to do anything while _enabled is false.
  bool bleAutoReEnableAfterReindex = false;
  // CrumBLE: set once per book open when a page can't render with a BLE remote
  // connected (image decode or glyphs starved by NimBLE's ~58 KB). We drop
  // Bluetooth so the full heap renders the page (images AND text), and show the
  // explanatory alert only once. Image-heavy books are simply unreadable with a
  // remote attached on this chip; the user reads with device buttons.
  bool btDisabledForMemoryThisBook = false;
  // Post-connect grace tracking for the auto-drop above. NimBLE's connect
  // handshake briefly spikes heap pressure; a single render in that window can
  // starve even on books that read fine with BLE. We ignore starvation until
  // kBtConnectGraceMs after the remote came up, so the auto-drop only fires on
  // books that stay unrenderable past the transient.
  unsigned long btEnabledAtMs = 0UL;
  bool btWasEnabled = false;
  // Length of the post-connect grace window during which a starved render is
  // attributed to NimBLE's transient connect-spike rather than a genuinely
  // unrenderable book. Shared by render() (auto-drop gate) and loop() (the #48
  // post-grace re-render).
  static constexpr unsigned long kBtConnectGraceMs = 4000;
  // #48: set when a render was suppressed because it starved inside the connect
  // grace window (half-drawn glyphs). loop() fires exactly one re-render once the
  // grace window expires (or BLE drops) -- never a tight in-grace retry loop,
  // which previously tripped the auto-drop on books that stay connected.
  bool pendingGraceReRender = false;
  // BT No Images Quick Connect: latched true once the bonded remote actually
  // links while image suppression is armed. Lets loop() tell a genuine link drop
  // (controller powered off / out of range -- stack stays enabled, isConnected
  // goes false) apart from the brief pre-link handshake window, so we restore
  // images on the drop but not during the initial connect.
  bool btNoImgLinkSeen = false;
  enum class BookmarkFeedbackType : uint8_t {
    Added,
    Removed,
    LimitReached,
  };
  bool pendingBookmarkFeedback = false;
  BookmarkFeedbackType bookmarkFeedbackType = BookmarkFeedbackType::Added;
  unsigned long bookmarkFeedbackShowTime = 0UL;
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
  bool pendingTiltPageTurnFeedback = false;
  bool tiltPageTurnFeedbackEnabled = false;
  unsigned long tiltPageTurnFeedbackShowTime = 0UL;
  // True if the previous render drew a feedback toast. Used to force a clean
  // half-refresh on the toast's dismiss frame only, so the toast box is fully
  // erased instead of ghosting ("failing to disappear") on a fast refresh.
  bool toastShownLastRender = false;
  int completionTriggerSpineIndex = -1;
  float completionTriggerSpineProgress = 1.0f;
  bool completionPromptQueued = false;
  bool completionPromptShown = false;
  bool completionTriggerSeenBelow = false;
  bool lastAtOrPastCompletionTrigger = false;

  // Tracks whether this book is currently removed from Recent Books by the
  // removeReadBooksFromRecents feature (set at End-of-Book, cleared if paged back in).
  bool recentsEntryRemoved = false;
  // Set when the reader is left at end-of-book and SETTINGS.moveFinishedToReadFolder is on.
  // Consumed in onExit() to relocate the finished book into /Read/.
  bool pendingReadFolderMove = false;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  // Banks elapsed time from `sessionSegmentStartMs` into stats.bin +
  // GlobalReadingStats and resets the anchor so subsequent calls don't
  // double-count. Idempotent: a 0-ms segment is a no-op. Called from
  // onExit, onBeforeDeepSleep, and the incremental save tick.
  void commitReadingSession();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  void openFileTransfer();
  void openAutoPageTurnIntervalPicker(bool ignoreInitialConfirmRelease = false);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void reindexCurrentSection();
  void executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action);
  bool consumeLongPowerButtonRelease();
  bool consumeLongPowerButtonHold();
  bool executeShortPowerButtonAction();
  bool executeLongPowerButtonAction();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void executeLongPressMenuAction();
  void pageTurn(bool isForwardTurn);
  float getCurrentBookProgressPercent() const;
  void initializeCompletionPromptTrigger();
  bool isAtOrPastCompletionTrigger() const;
  void queueCompletionPromptIfNeeded();
  void setBookCompleted(bool isCompleted);
  void showCompletedFeedback(bool isCompleted);
  void showTiltPageTurnFeedback(bool enabled);

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}
  void onEnter() override;
  void onExit() override;
  // Banks the current reading session into stats before the device
  // powers off. Without this, time read since the last commit was
  // lost — onExit only fires on explicit activity transitions, and
  // hardware deep-sleep skips that path. Idempotent with onExit.
  void onBeforeDeepSleep() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return automaticPageTurnActive; }
  bool isReaderActivity() const override { return true; }
  bool canSnapshotForSleepOverlay() const override { return true; }
  std::string getCurrentBookPath() const override { return epub ? epub->getPath() : std::string{}; }
  void setAutoPageTurnIntervalSeconds(uint16_t seconds);
  uint16_t getAutoPageTurnIntervalSeconds() const;

  // Renders the last saved page to the frame buffer without flushing to display.
  // Used by SleepActivity to prepare the background for the overlay sleep mode.
  // Returns false if the page cannot be loaded (missing cache / file error).
  static bool drawCurrentPageToBuffer(const std::string& filePath, GfxRenderer& renderer);
  ScreenshotInfo getScreenshotInfo() const override;
};
