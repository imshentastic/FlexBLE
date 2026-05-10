# Changelog

## [Unreleased]

### Added
- New "Tap Power While Asleep to Cycle" display setting. When on, a brief power-button tap from sleep picks a fresh random image from `/.sleep` and re-enters deep sleep, instead of waking. Off by default — each cycle costs a boot + e-ink half-refresh worth of battery. Pinned sleep images are skipped in cycle mode (always random)
- BLE HID page-turner support. From inside a book, open the reader menu and select **Bluetooth** to pair a Bluetooth remote (e.g. IINE GameBrick) and use its buttons as virtual page-turn keys. Reader-session-only: BLE auto-disables when you exit the book. Pairing is remembered across reboots; enable Bluetooth in a later reader session to auto-reconnect.

### Changed
- Default `tiny` build now omits the Teensy (8px) font variant to make room for the NimBLE-Arduino BLE stack. Re-enable by removing `OMIT_TEENSY_FONT` from `platformio.ini` if you don't need Bluetooth.

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