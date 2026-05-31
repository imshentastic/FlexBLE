# crumble-prebake — off-device EPUB cache prebake CLI

## Goal

Ship a desktop binary that pre-bakes the per-book cache state CrumBLE's
on-device XHTML pipeline writes after first open:

- `book.bin` — BookMetadataCache (spine, TOC, cover href, language,
  cumulative spine sizes).
- `sections/<spineIdx>.bin` — per-chapter pre-laid-out pages, for a
  default font / viewport preset.
- `css_rules.cache` — CssParser rule table for the same preset.
- `thumb_<W>x<H>.bmp` — cover thumbnails at common UI sizes.

The device picks up these files unchanged through the existing
`Epub::load` cache-hit path and `Section::loadSectionFile` cache-hit
path. **No firmware changes are required** — the prebake tool produces
byte-identical artifacts to what the device would generate, just off
the device.

Users with large libraries run the tool once per book on a desktop,
drop the resulting `.crosspoint/` tree onto the SD card, and read with
instant first-open / instant first-chapter-visit. Users who don't
prebake see no regression: they're on the v3.7.2-equivalent on-device
path.

## Non-goals

- `.cmb` format involvement. The `.cmb` runtime path was reverted on
  2026-05-30; see `project_cmb_pivot.md` in user memory.
- Pre-baking `.pxc` page bitmaps. Separate, more invasive feature.
- Multi-preset section files. Start with one preset (device default);
  expand later if users actually change settings often.

## Strategy: share the on-device parser/builder code

Most of the on-device EPUB pipeline (`BookMetadataCache`,
`ContainerParser`, `ContentOpfParser`, `TocNcxParser`, `TocNavParser`,
`CssParser`, `ZipFile`, `Section::createSectionFile`) is mostly portable
C++. The Arduino-coupled surface is concentrated in two places:

1. **File I/O** — Storage / HalFile / FsFile, which talks to the
   device's SD card driver. Replaceable with a host shim that wraps
   `std::fstream`.
2. **Renderer** — GfxRenderer takes a HalDisplay&, uses FreeRTOS
   mutexes, and pulls in the EPD driver. Only needed for **section
   builds** (phase 2); the renderer's text-measurement APIs themselves
   are pure math over portable `EpdFontData` structs.

For each piece of firmware code we want on the host, we either:
- Compile it as-is against a `host_shim/` directory that provides
  drop-in replacements for `<Arduino.h>`, `HalStorage.h`, `HalGPIO.h`,
  `Logging.h`, FreeRTOS primitives. The shims are minimal and
  no-op-where-possible.
- For unavoidable hardware coupling (the rendering pipeline's display
  side), expose a `TextMetricsOnly` wrapper that calls only the pure
  math APIs and stubs the rest.

Tradeoff vs reimplementing parsers from scratch: shared code drifts in
lockstep with the device. If a parser bug is fixed on device, the
prebake tool gets the fix on the next rebuild. If we reimplemented, we'd
silently produce different artifacts after every firmware change.

## Iterative scope

### Phase 1 — `book.bin` only (no renderer)

Walks EPUB ZIP, parses container.xml / content.opf / NCX or NAV,
populates `BookMetadataCache`, writes `book.bin`. Identical to what
`Epub::load`'s slow path does after `bookMetadataCache->load()` fails.

**Dependencies the host shim must support:**
- Storage (exists/open-write/open-read/remove/rename/mkdir)
- HalFile (read/write/seek/position/close)
- Arduino: `millis()`, `delay()`, basic types
- expat (already host-compatible; uses libexpat from system)

**No renderer, no fonts, no CSS parser invocation, no XHTML chapter
parsing.** Tightest possible proof-of-concept.

### Phase 2 — `sections/<n>.bin` (needs renderer)

Drag in the renderer + font code via the stub-HalDisplay /
replace-FreeRTOS approach. Each chapter is parsed via
`ChapterHtmlSlimParser` against a `TextMetricsOnly` renderer that
returns identical line-width measurements to the device, producing
identical section files.

### Phase 3 — `css_rules.cache`

Run the CssParser the same way. Probably trivial once phase 2 is done.

### Phase 4 — `thumb_<W>x<H>.bmp`

Decode + resize cover images using the device's
`JpegToBmpConverter` / `PngToBmpConverter`. Hard-coded common sizes.

## Directory layout

```
tools/crumble-prebake/
  DESIGN.md                         # this file
  build.sh                          # plain g++/clang script; mirrors test/run_cmb_roundtrip_test.sh
  src/
    main.cpp                        # CLI argument parsing + per-book dispatch
    prebake.cpp                     # high-level "prebake one EPUB" logic
    prebake.h
  host_shim/
    Arduino.h                       # millis()/delay()/String/byte/etc.
    HalStorage.h                    # Storage singleton + HalFile class backed by fstream
    HalStorage.cpp
    Logging.h                       # LOG_* macros -> stderr
    freertos_shims.h                # std::mutex replacement for SemaphoreHandle_t
    halgpio_shim.h                  # no-op for any GPIO calls (shouldn't be hit in our subset)
  test/
    run_phase1_test.sh              # round-trip: bake book.bin from a known EPUB, byte-compare against expected
    fixtures/
      small.epub                    # known small EPUB for tests
      expected_book.bin             # golden file
```

## Build

`tools/crumble-prebake/build.sh` runs:

```bash
g++ -std=c++20 -O2 -Wall -Wextra \
    -I host_shim \
    -I <repo-root>/lib/Epub \
    -I <repo-root>/lib/Epub/Epub \
    -I <repo-root>/lib/ZipFile \
    -I <repo-root>/lib/expat \
    -I <repo-root>/lib/FsHelpers \
    -I <repo-root>/lib/Serialization \
    src/*.cpp \
    host_shim/*.cpp \
    <repo-root>/lib/Epub/Epub/BookMetadataCache.cpp \
    <repo-root>/lib/Epub/Epub/parsers/ContainerParser.cpp \
    <repo-root>/lib/Epub/Epub/parsers/ContentOpfParser.cpp \
    <repo-root>/lib/Epub/Epub/parsers/TocNcxParser.cpp \
    <repo-root>/lib/Epub/Epub/parsers/TocNavParser.cpp \
    <repo-root>/lib/ZipFile/ZipFile.cpp \
    <repo-root>/lib/FsHelpers/FsHelpers.cpp \
    <repo-root>/lib/expat/xmlparse.c <repo-root>/lib/expat/xmltok.c <repo-root>/lib/expat/xmlrole.c \
    -lexpat \
    -o build/crumble-prebake
```

(Real script in `build.sh` resolves repo-root via `git rev-parse`,
gracefully handles macOS vs Linux compiler invocation, etc.)

## CLI shape (target for phase 1)

```
crumble-prebake [options] <epub> [<epub> ...]

Options:
  --output-dir <dir>     Write cache state into <dir>/.crosspoint/epub_<hash>/
                         instead of next to each input EPUB.
  --sd-mount <path>      Equivalent to --output-dir <sd-mount>; semantic alias
                         that makes the SD-card workflow self-documenting.
  --check                Skip books whose existing book.bin is fresh against the
                         EPUB's mtime.
  --verbose              Log per-step timing.
```

Phase 1 produces only `book.bin`. Phases 2/3/4 add `sections/*.bin`,
`css_rules.cache`, and `thumb_*.bmp` respectively. The CLI gains
`--book-bin-only` / `--sections-only` / `--thumbs-only` flags once
those phases land.

## Compatibility / versioning

- `BOOK_CACHE_VERSION` is in `lib/Epub/Epub/BookMetadataCache.cpp`.
  The prebake tool reads the same constant from the same source, so
  cross-version drift can't happen as long as the tool is rebuilt
  against the current firmware tree.
- `SECTION_FILE_VERSION` (phase 2) — same arrangement.
- Distribute prebake binaries paired with a firmware release: a
  prebake built against firmware v3.7.3 emits cache state v3.7.3
  understands.

## Open questions

- macOS code-signing for distributed binaries — defer to first release
  cut.
- Should the tool also generate `.pxc` page bitmaps? Separate feature;
  not in this scope.
- Concurrency — the on-device code is single-task. The prebake CLI
  could parallelize across EPUBs, but the per-EPUB pipeline stays
  single-threaded (mirrors device behaviour, reduces drift risk).
