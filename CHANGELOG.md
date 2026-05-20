# Changelog

## [Unreleased]

## [crumble-v2.0.0] - 2026-05-20
**Rebrand: FlexBLE → CrumBle.** The fork's identity changed; major bump to mark v1.x as the FlexBLE era and v2.x as the CrumBle era. No upstream version sync (still on CrossInk 1.2.11.1).

### Changed (rebrand)
- Project renamed FlexBLE → CrumBle across all source files, build flags, i18n strings, and boot/sleep screens. `FLEXBLE_VERSION` → `CRUMBLE_VERSION`, `STR_FLEXBLE` → `STR_CRUMBLE`, `flexble_version` → `crumble_version` in `platformio.ini`.
- New boot/sleep logo: chocolate-chip cookie with a bite taken out, replacing the previous triangle mark. 1-bit silhouette at 120×120 traced from brand reference; generator preserved at `src/images/gen_logo.py`.
- Local folder renamed `FlexBLE/` → `CrumBle/`. GitHub repo renamed to match.

### Added (Collections)
- **Collections system** (Phases 1–3): user-defined book collections backed by `collections.json` on SD. Virtual collections (All Books, Favorites, Recent, Currently Reading, Finished) computed lazily. Long-press Confirm on a book in the shelf to add/remove from collections; long-press on a shelf header for collection-level actions.
- **Rename + Delete user collections** from the shelf-header action menu (rejected for virtual collections; Favorites refuses delete).
- **+ New Collection** from any header (including locked virtual ones) — quick entry point for creating a new collection without first navigating to a user collection.
- **Add/Remove Books** multi-select picker (`AddBooksToCollectionActivity`). Pick books to toggle membership in the active user collection in a single batch.
- **Per-collection settings** persisted in `collections.json`: `sortMode` (Title / Author / Date Added) and `collapseSeries` (on/off).
- **Sort menu** (Sort A–Z / Z–A / Author / Date Added) per collection, accessed via long-press on the shelf header. New `SortPickerActivity`.

### Added (Series)
- Opt-in **series detection** via EPUB metadata (`calibre:series` and OPF series fields). New `SeriesIndex` cache, `ContentOpfParser` series support.
- **Shelf series collapse**: books in the same series collapse to a single spine glyph. Tap-through opens a mini-picker (`SeriesMiniPickerActivity`) listing the books in the series.
- **Book metadata viewer** (`BookMetadataViewerActivity`) shows author, series, language, publisher pulled from EPUB OPF.
- Series detection defaulted **off** in settings; toggling on triggers a one-time scan of the library to populate `SeriesIndex`.

### Added (UX)
- **Remember last shelf position** when switching to the bottom menu icon row and back.
- **Remember last menu row position** when switching between menu and shelf.
- **Carousel ghosting fixes** on the Lyra Flow theme: max-size slot clear before each cover paint, removed always-on inner frame, thinned selection border 4 px → 2 px.
- **Progress-bar ghost-text fix**: footer pre-clear widened to full page width so adjacent books with different aspect ratios don't leak text.
- **End-of-book navigation** and **going-home popup** improvements (see commits for details).
- Header arrows enlarged on the shelf; cover-ghost outline removed during paint to prevent corner-hook artifacts.

### Fixed (Reader stats)
- **Never lose reading time** on power-off: deep-sleep commit path now flushes the active session segment; idempotent re-commit via `sessionSegmentStartMs` prevents double-counting; the 10 s floor was dropped so very short sessions count. Ported from aalu's reading-stats fix (MIT, attributed).

## [crumble-v1.0.0] - 2026-05-17
First stable CrumBle release post-upstream-merge. CrumBle now uses its own semver (`crumble_version`) independent of upstream CrossInk's versioning. The `crossink_version` field continues to track the upstream sync point (currently 1.2.11.1).

### Added (CrumBle fork)
- BLE HID page-turner support. From inside a book, open the reader menu and select **Bluetooth** to pair a Bluetooth remote (e.g. IINE GameBrick) and use its buttons as virtual page-turn keys. Reader-session-only: BLE auto-disables when you exit the book. Pairing is remembered across reboots; enable Bluetooth in a later reader session to auto-reconnect.
- **BT Quick Connect** action in the Global Book Settings drawer. From inside a book, long-press the menu button → scroll to "BT Quick Connect" → activates BLE and reconnects to the last bonded remote (or launches the pairing UI if no remote is saved). Closes the drawer back to the book on success.
- After a successful BT connection from the in-book Bluetooth menu, CrumBle now auto-pops both the BT settings and the reader menu so the user lands straight back in the book — no manual back-tapping required.
- New "Tap Power While Asleep to Cycle" display setting. When on, a brief power-button tap from sleep picks a fresh random image from `/.sleep` and re-enters deep sleep, instead of waking. Off by default — each cycle costs a boot + e-ink half-refresh worth of battery. Pinned sleep images are skipped in cycle mode (always random).
- `.png` sleep images (including transparency) are now accepted in **Custom** Sleep Screen mode, not just Page Overlay. Transparent regions compose over the last reader page so a transparent PNG sleep screen reveals book text underneath. On the deep-sleep cycle path (tap-power-while-asleep), the last reader page is restored from a small SD-cached snapshot written when leaving a book.
- **Global Book Settings** drawer. Long-press the menu button in a book to pop up a bottom-drawer-style quick-settings panel with a "Global Book Settings" tab on top. Lists every reader setting (font, layout, hyphenation, bionic, images, etc.) and uses e-ink fast refresh so toggling is snappy. Closing the drawer re-flows the page immediately when a setting was actually changed; a no-op visit skips the re-layout. Triggered via the existing `longPressMenuAction` setting (default flipped to the new "Book Settings" option). Architecture adapted from [inx by Dave Allie](https://github.com/obijuankenobiii/inx) (MIT).
- **PackBits-compressed BW backup** for the grayscale anti-aliasing pass. Replaces the chunked 12 × 4 KB lazy allocation with a single 16-32 KB worst-case bounded buffer. Typical reader pages compress to 2-5 KB, dramatically reducing the fragmentation pressure that caused grayscale to fail when BLE was active.
- Auto-retry on chapter-layout abort: if the parser trips the low-heap floor while BLE is consuming its ~58 KB share, CrumBle silently drops BLE, retries the layout (now with the extra headroom), and lets the existing auto-reconnect logic re-establish the link on the user's next remote button press.

### Changed (CrumBle fork)
- Bluetooth main menu now uses Up/Down labels and binds both side U/D and front L/R to navigation, matching Reader Options and Controls Options conventions.
- LYRA_FLOW (5-book carousel) preserved as a first-class theme option through the upstream merge. Front Prev/Next iterates within the carousel only; side Up/Down jumps to the menu list and iterates through it, with the carousel staying pinned to the last selected book.
- Reader setting labels dropped the redundant "Reader" prefix ("Font Family", "Font Size", "Line Spacing", "Screen Margin", "Paragraph Alignment") so they fit better in the drawer.
- Default `tiny` build now omits the Teensy (8px) font variant to make room for the NimBLE-Arduino BLE stack. Re-enable by removing `OMIT_TEENSY_FONT` from `platformio.ini` if you don't need Bluetooth.
- EPUB layout heap thresholds relaxed for BLE-paired sessions: text-layout floor 48 → 16 KB free, image-extraction floor 72 → 56 KB free. Brings parser within reach of the heap remaining after NimBLE allocations; failures still degrade gracefully (image extraction falls back to "page rendered without images"; layout aborts trigger the new BLE-disable-and-retry path).
- BookSettingsDrawer hint area enlarged so the bottom button labels render fully (were clipped against the screen edge at the previous height).
- Settings → System now displays `CrumBle x.y.z (CrossInk x.y.z.z)` so both the fork and upstream sync version are visible.
- Merged upstream CrossInk v1.2.10 and v1.2.11/v1.2.11.1 (see entries below for what those releases brought).

### Fixed (CrumBle fork)
- BLE page-turner could hang at chapter boundaries when a release HID frame was dropped (NimBLE task starvation during the heavy section-load render is a common trigger). Subsequent presses were silently filtered out by both the BLE-side `activeInjectedButton` gate and HalGPIO's same-state short-circuit, requiring a book exit/re-enter to recover. Added two backstops: a 2 s max-hold auto-release on virtual buttons (suppressed release edge so no phantom page turn fires), and a force-release of a stuck injection in Game Brick's same-key re-press promotion path.
- BluetoothSettings used `wasPressed(Confirm)` for action triggers instead of `wasReleased`, causing the Confirm release to leak through pop transitions and re-launch the BT menu from the reader menu underneath. All five action handlers now consistently use `wasReleased`.
- Reader exit during BLE reconnect: tapping Back impatiently during the 2-3 s connect freeze used to leak the release through to the reader's home-on-back-release handler, exiting the book and auto-disabling BLE. The next Back release is now suppressed when `checkAutoReconnect` blocked for >500 ms.
- Flow theme home carousel input grammar fixed across the merge — front Prev/Next stays within the carousel, side U/D moves through menu items without dragging the carousel along.

## [v1.2.11.1] - 2026-05-15

### Changed
- Removed Medium font size from `xlarge` build to get it below the size limit

### Fixed
- Included Lyra Carousel by activating the build flag `DCROSSINK_ENABLE_LYRA_CAROUSEL=1`

## [v1.2.11] - 2026-05-14

### Added
- Added new personal theme: "Minimal"
- Added a custom sleep timer picker so `Time to Sleep` can be set from 1 to 30 minutes instead of cycling fixed presets.
- Added an in-reader Controls shortcut so you can customize your buttons without leaving the book.
- Added bookmark cleanup shortcuts: hold Select on a bookmark to delete it, or hold Open on a book in Bookmarks to clear that book's bookmark list.
- Added a confirmation message after deleting a book's cache from the reader or File Browser.
- Added a File Browser long-press action for deleting an EPUB or XTC book's cache
- Added a File Browser long-press action for marking EPUB books as finished or unfinished.

### Changed
- Hardened deep sleep entry by shutting WiFi down before waiting for the power button to be released.
- Raised the web file-transfer filename limit from 100 to 150 bytes so longer uploaded filenames are preserved.
- Made the in-reader Reader Options menu include the same Reader settings and actions as Settings > Reader.

### Fixed
- Fixed inline EPUB images disappearing in landscape when their bottom edge slightly overlaps the screen margin.
- Reduced unnecessary low-memory image suppression for JPEG-heavy EPUB chapters and added CSS heap diagnostics during chapter rebuilds.
- Allowed wider inline JPEG images in EPUBs to render when they still fit the total pixel and heap safety limits.
- Fixed the SD-card font picker reopening immediately after selecting a font from Settings > Reader > Font Family.
- Fixed in-reader font-size changes for SD card fonts not working
- Fixed in-reader SD-card font changes not always rebuilding the current EPUB page layout.

## [v1.2.10] - 2026-05-11

### Added
- Added a `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- Added more flexible reader controls, including orientation-aware front/side button settings, nav-only or all-button front inversion, tilt page turn shortcuts, and side-button long-press rotation actions.
- Added a per-session auto page turn interval picker with values from 5 to 120 seconds.
- Added a file-browser Home/Back long-press action for toggling hidden files and folders.
- Added EPUB rendering and diagnostics improvements, including visible `<hr>` separators and heap logs around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds.
- Added reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Added simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed
- Reduced Controls settings section spacing so the grouped controls fit better on X3 screens.
- Made front reader long-press actions trigger when the hold delay is reached while normal page turns still trigger on release.

- Used the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allowed the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed
- Fixed reader button and shortcut behavior, including X3 power-button wake filtering, folder delete long-press timing, and WiFi scan/connect screens that could not be exited while work was in progress.
- Fixed RoundedRaff home-menu, keyboard, and button-hint rendering issues so Settings remains reachable and compact labels no longer overlap or disappear.
- Fixed font and glyph handling by reducing persistent SD-card font advance-cache memory, releasing optional font caches before image extraction only when heap is tight, and showing a visible replacement symbol when compact UI fonts lack `U+FFFD`.
- Fixed KOReader Sync authentication diagnostics and an in-reader sync crash, including clearer handling when a server or proxy returns non-JSON content.
- Fixed EPUB text rendering for redactions, whitespace-only XHTML text nodes, simple black CSS span backgrounds, list bullets in `<li><p>...</p></li>` items, and very long base64-like text runs.
- Fixed EPUB image, thumbnail, and section-rebuild stability so image-heavy chapters use less temporary memory, scale images more reliably, avoid stale dimensions, and suppress optional image work earlier under heap pressure.
- Fixed EPUB low-memory and cache safety by skipping optional next-chapter indexing and sleep-page cache rebuilds when heap is tight, failing safely with a malformed-book warning and Home exit path, rebuilding incompatible fork-written caches, and handling low-memory CSS parsing, truncated SD writes, invalid serialized strings, and failed temp-cache promotion.
- Fixed a Home crash after clearing reading cache by skipping optional EPUB thumbnail rebuilds when the source EPUB cache is missing.
- Fixed reader prewarm behavior by skipping image decoding, keeping mixed-style font glyphs cached together, and avoiding section rebuilds for render-quality-only option changes.
- Fixed concurrent render/storage crashes by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Fixed Recent Books, EPUB/XTC thumbnail caches, deleted-folder metadata, and XTC cover scaling so cached book data stays in sync and grid covers fill their slots correctly.
- Fixed simulator build configuration so SDL2 and simulator-provided network/OTA shims compile cleanly.

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
