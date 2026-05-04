# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),

## [Unreleased]
### Changed
- Update the Lyra Carousel theme to use Lyra Flow-style cover rendering, move the active book title above the cover, and show per-book reading stats below while keeping the carousel's existing home menu layout

### Added
- Prevent a crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
- Clean up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Improve simple EPUB tables by buffering them into multi-column grid fragments instead of rendering each cell as an unrelated paragraph

### Fixed
- Fix a crash when opening EPUB chapters that continue with normal text after a buffered table
- Fix a crash when using `Go to %` in EPUBs by serializing the jump calculation with other reader cache access
- Fix OTA update checks after the streaming release parser merge by keeping variant-aware firmware asset matching
- Fix the Lyra carousel simulator build by syncing its theme override signatures with the shared theme API
- Fix missing Reading Stats and Bookmarks icons in the Lyra carousel home menu
- Fix Lyra carousel home navigation so dynamically-added menu items stay reachable and trigger the correct action
- Fix Lyra carousel settings lists so section headers render correctly instead of being treated like normal selectable rows
