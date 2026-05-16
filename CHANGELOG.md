# Changelog

## [Unreleased]

### Added
- New "Tap Power While Asleep to Cycle" display setting. When on, a brief power-button tap from sleep picks a fresh random image from `/.sleep` and re-enters deep sleep, instead of waking. Off by default — each cycle costs a boot + e-ink half-refresh worth of battery. Pinned sleep images are skipped in cycle mode (always random)
- BLE HID page-turner support. From inside a book, open the reader menu and select **Bluetooth** to pair a Bluetooth remote (e.g. IINE GameBrick) and use its buttons as virtual page-turn keys. Reader-session-only: BLE auto-disables when you exit the book. Pairing is remembered across reboots; enable Bluetooth in a later reader session to auto-reconnect.
- `.png` sleep images (including transparency) are now accepted in **Custom** Sleep Screen mode, not just Page Overlay. Transparent regions compose over the last reader page so a transparent PNG sleep screen reveals book text underneath. On the deep-sleep cycle path (tap-power-while-asleep), the last reader page is restored from a small SD-cached snapshot written when leaving a book.
- **Global Book Settings** drawer. Long-press the menu button in a book to pop up a bottom-drawer-style quick-settings panel with a "Global Book Settings" tab on top. Lists every reader setting (font, layout, hyphenation, bionic, images, etc.) and uses e-ink fast refresh so toggling is snappy. Closing the drawer re-flows the page immediately when a setting was actually changed; a no-op visit skips the re-layout. Triggered via the existing `longPressMenuAction` setting (default flipped to the new "Book Settings" option). Architecture adapted from [inx by Dave Allie](https://github.com/obijuankenobiii/inx) (MIT).

### Changed
- Reader setting labels dropped the redundant "Reader" prefix ("Font Family", "Font Size", "Line Spacing", "Screen Margin", "Paragraph Alignment") so they fit better in the drawer.

### Changed
- Default `tiny` build now omits the Teensy (8px) font variant to make room for the NimBLE-Arduino BLE stack. Re-enable by removing `OMIT_TEENSY_FONT` from `platformio.ini` if you don't need Bluetooth.

### Fixed
- BLE page-turner could hang at chapter boundaries when a release HID frame was dropped (NimBLE task starvation during the heavy section-load render is a common trigger). Subsequent presses were silently filtered out by both the BLE-side `activeInjectedButton` gate and HalGPIO's same-state short-circuit, requiring a book exit/re-enter to recover. Added two backstops: a 2 s max-hold auto-release on virtual buttons (suppressed release edge so no phantom page turn fires), and a force-release of a stuck injection in Game Brick's same-key re-press promotion path.

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Added
- Short-press power button acts as confirm within the in-reader menus

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid