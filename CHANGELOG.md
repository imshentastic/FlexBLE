# Changelog

## [v1.2.10]

### Added
- Added the Lyra Carousel home theme.
- Added a `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- Added EPUB `<hr>` rendering so horizontal rules display as visible separators instead of being ignored.
- Added EPUB heap diagnostics around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds to make low-memory crashes easier to trace.
- Added a per-session auto page turn interval picker with values from 5 to 120 seconds.
- Added reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Added a file-browser Home/Back long-press action for toggling hidden files and folders.
- Added simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed
- Temporarily hid the Lyra Carousel theme option unless `CROSSINK_ENABLE_LYRA_CAROUSEL=1` is set at build time, and migrated existing disabled selections back to Lyra while the carousel remains experimental.
- Use the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allow the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed
- Fixed a simulator crash when opening Reader Options.
- Fixed RoundedRaff home-menu navigation so Settings remains reachable when the inline Continue Reading row is visible.
- Fixed RoundedRaff keyboard and button-hint rendering so number-row symbols and UTF-8 labels no longer overlap or disappear.
- Fixed WiFi scan/connect screens so users can back out while a scan or connection attempt is in progress.
- Fixed folder delete long-press timing so deletion triggers after the hold delay instead of on release.
- Fixed missing-glyph rendering so compact UI fonts show a visible replacement symbol even when they do not include `U+FFFD`.
- Fixed KOReader Sync authentication handling with better validation and clearer diagnostics when a server or proxy returns non-JSON content.
- Fixed EPUB redaction and whitespace rendering by preserving whitespace-only XHTML text nodes and rendering simple black CSS backgrounds for inline spans.
- Fixed EPUB list bullets so they stay attached to the first paragraph in `<li><p>...</p></li>` list items.
- Fixed EPUB image scaling, low-memory image fallback, and thumbnail generation so image-heavy books are less likely to crash or reuse stale dimensions.
- Fixed EPUB section rebuilds so image-heavy chapters use less temporary memory while laying out text after inline images.
- Fixed EPUB low-memory stability by skipping optional silent next-chapter indexing and sleep-page cache rebuilds when heap is already tight.
- Fixed EPUB image handling so shared memory budgets suppress inline images and decoder work earlier under heap pressure.
- Fixed EPUB cache validation so Crossink rebuilds `book.bin`, `sections/*.bin`, and CSS rule caches written by other CrossPoint forks instead of treating matching version numbers as compatible.
- Fixed EPUB CSS loading and page-cache handling so low-memory CSS parsing, truncated SD writes, invalid serialized strings, and bad temp-cache promotion fail safely.
- Fixed reader prewarm behavior by skipping image decoding, keeping mixed-style font glyphs cached together, and avoiding section rebuilds for render-quality-only option changes.
- Fixed concurrent render/storage crashes by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Fixed Recent Books, EPUB/XTC thumbnail caches, Lyra Carousel snapshots, and deleted-folder metadata so they stay in sync when cache files change or are removed.
- Fixed XTC covers in the Recent Books grid so they fill cover slots instead of appearing letterboxed when the first page has a different aspect ratio.
- Fixed Lyra Carousel cache handling so grid-sized thumbnails are not reused as carousel covers and SD snapshots are skipped after low-RAM frame-cache fallback.
- Fixed simulator build configuration so SDL2 and simulator-provided network/OTA shims compile cleanly.

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
