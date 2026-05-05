# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),

## [Unreleased]
### Changed
- Update the Lyra Carousel theme to use Lyra Flow-style cover rendering, move the active book title above the cover, and show per-book reading stats below while keeping the carousel's existing home menu layout
- Reduce Lyra Carousel from 5 visible recent books to 3 so the on-screen carousel matches its 3-frame RAM cache and navigation stays more responsive

### Added
- Prevent a crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
- Clean up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Improve simple EPUB tables by buffering them into multi-column grid fragments instead of rendering each cell as an unrelated paragraph

### Fixed
- Fix Lyra carousel navigation lag by caching per-book stats and progress once on home entry instead of re-reading from SD on every button press
- Fix Lyra carousel first-navigation hitch by pre-rendering the prev and next frames at startup alongside the current book
- Reduce Lyra carousel cover-render heap churn by reusing bitmap row scratch buffers instead of allocating them on every draw
- Fix Home cover thumbnail generation failing for EPUBs with missing metadata cache by allowing the Home loading path to rebuild the EPUB cache before generating missing thumbs
- Fix Lyra Carousel returning from the reader feeling frozen by showing Home before rebuilding the carousel snapshot cache, reusing the last valid SD snapshot when it still matches the current recent-books list, and showing a progress popup only when a full snapshot rebuild is actually needed
- Fix Lyra Carousel losing its saved SD snapshot fast path after the deferred warmup changes by restoring snapshot reuse on Home entry and rebuilding the snapshot after warmup when the visible carousel set fits in RAM
- Fix Lyra Carousel still repainting the full bottom menu row on cached-frame navigation by baking the static menu strip into cached frames and only overlaying the active menu selection at render time
- Fix Lyra Carousel still live-rendering the third book in degraded 2/3 RAM-slot mode by writing the full 3-book SD snapshot even when only part of the carousel fits in RAM
- Fix reopening an existing book still reshuffling the Home carousel by refreshing recent-book metadata in place instead of moving already-known books to the front
- Fix Lyra Carousel generic fallback covers by overlaying the book title inside the placeholder cover instead of showing only a blank generic icon
- Fix a crash when opening EPUB chapters that continue with normal text after a buffered table
- Fix a crash when using `Go to %` in EPUBs by serializing the jump calculation with other reader cache access
- Fix OTA update checks after the streaming release parser merge by keeping variant-aware firmware asset matching
- Fix the Lyra carousel simulator build by syncing its theme override signatures with the shared theme API
- Fix missing Reading Stats and Bookmarks icons in the Lyra carousel home menu
- Fix Lyra carousel home navigation so dynamically-added menu items stay reachable and trigger the correct action
- Fix Lyra carousel settings lists so section headers render correctly instead of being treated like normal selectable rows
