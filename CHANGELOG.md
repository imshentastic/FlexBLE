# Changelog

## [v1.2.10]

### Added
- Add Lyra Carousel theme
- Add a `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid
- Add real EPUB `<hr>` rendering so horizontal rules now display as visible separators instead of being ignored
- Add a per-session auto page turn interval picker so EPUB readers can choose any value from 5 to 120 seconds instead of only the old preset list
- Add ability to render block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation in reader fonts (PR #104)
- Add long-press action on the "Home/Back" button within the file-browser to toggle hidden files and folders
- Add a simulator-only `S` key shortcut for testing sleep and wake behavior

### Changed
- Use the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open

### Fixed
- Render simple black CSS backgrounds for whitespace-only EPUB inline spans, including NBSP redaction bars that previously appeared as only trailing punctuation
- Preserve whitespace-only XHTML text nodes during browser EPUB optimization so redaction spans keep their intended width
- Keep EPUB list bullets attached to the first paragraph in `<li><p>...</p></li>` list items
- Keep EPUB and XTC thumbnail caches, Recent Books covers, carousel snapshots, and deleted-folder metadata in sync when cache files change or are removed
- Harden EPUB image scaling, low-memory image fallback, and thumbnail generation so image-heavy books are less likely to crash or reuse stale dimensions
- Make EPUB CSS loading more resilient by reducing parser heap fragmentation, growing rule storage in guarded chunks, keeping selector lookups sorted, and logging parse-failure context
- Harden EPUB section and page-cache reads/writes so truncated SD writes, invalid serialized strings, and bad temp-cache promotion fail safely
- Prevent concurrent render/storage crashes by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup
- Make reader prewarm lighter by skipping image decoding, keeping mixed-style font glyphs cached together, and avoiding section rebuilds for render-quality-only option changes
- Keep the Recent Books grid from saving grid-sized cover thumbnails as reusable cover paths so Lyra Carousel does not render tiny covers

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
