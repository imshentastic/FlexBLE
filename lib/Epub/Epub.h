#pragma once

#include <Print.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;

class Epub {
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Stable cache path based on filepath
  std::string cachePath;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;
  // CrumBLE: series captured from the most recent parseContentOpf
  // pass. Not persisted to book.bin (cache version compatibility),
  // so getSeriesName/Index returns empty if load() reused the on-disk
  // cache. SeriesIndex caches across sessions instead.
  std::string lastSeriesName;
  std::string lastSeriesIndex;

  void migrateLegacyCachePath(const std::string& cacheDir) const;
  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  void parseCssFiles() const;
  // CrumBLE #134: cold-open fast path. If a .cmb sidecar lives next
  // to the EPUB, populates `bookMetadataCache` from it (no ZIP +
  // OPF + CSS + TOC parse) and returns true. Returns false on any
  // failure -- caller falls through to the slow path. Side-effects
  // mirror the slow path's: bookMetadataCache is loaded, cssFiles
  // is set, sections cache is invalidated if !skipLoadingCss.
  bool tryLoadFromCmb(bool skipLoadingCss);
  // Legacy sibling layout used in early CrumBLE builds: `foo.epub` ->
  // `foo.cmb` next to the original. Visible to anyone mounting the SD
  // card, which is confusing. Kept ONLY so migrateLegacyCmbSidecar()
  // can find old files and move them into the per-book cache dir.
  // Returns empty when the input has no detectable extension.
  static std::string legacyCmbSiblingPath(const std::string& epubPath);
  // If a legacy sibling `foo.cmb` exists, move it into the per-book
  // cache directory (or delete it as an orphan if the cache copy
  // already exists). One-time per book; subsequent loads see no
  // sibling and the check is a single Storage.exists call. Called
  // from the top of Epub::load before any cache work runs.
  void migrateLegacyCmbSidecar();

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir);
  ~Epub() = default;
  // Path to the .cmb sidecar inside this book's cache directory. Lives
  // alongside book.bin / cover.bmp / thumbs so users mounting their SD
  // card never see a stray sidecar next to their epub files. Empty
  // when cachePath is unset (should not happen post-construction).
  std::string getCmbPath() const;
  static std::string cachePathForFilePath(const std::string& filepath, const std::string& cacheDir);
  std::string& getBasePath() { return contentBasePath; }
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  // CrumBLE series detection (ported from aalu, MIT-licensed by Dave
  // Allie 2025; original repo: dawsonfi/aalu). Both values are
  // populated by parseContentOpf — empty when the OPF didn't declare
  // series, OR when load() short-circuited from the on-disk cache
  // (book.bin doesn't persist series fields; SeriesIndex handles
  // cross-session caching instead).
  const std::string& getSeriesName() const { return lastSeriesName; }
  const std::string& getSeriesIndex() const { return lastSeriesIndex; }
  // Lightweight OPF parse that captures ONLY series metadata into the
  // lastSeriesName/lastSeriesIndex fields. Doesn't touch book.bin so
  // it's safe to run on already-cached books without invalidating the
  // spine/TOC cache. Used by the lazy series-enrichment pass — the
  // caller reads getSeriesName()/getSeriesIndex() afterwards and
  // persists into SeriesIndex.  Returns true if the OPF was readable
  // (regardless of whether series info was found).
  bool extractSeriesFromOpf();
  std::string getCoverBmpPath(bool cropped = false) const;
  bool generateCoverBmp(bool cropped = false) const;
  std::string getThumbBmpPath() const;
  // Deprecated compatibility wrapper; forwards to getThumbBmpPath(0, height).
  [[deprecated("use getThumbBmpPath(int width, int height)")]]
  std::string getThumbBmpPath(int height) const;
  // Returns the thumbnail cache path. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  std::string getThumbBmpPath(int width, int height) const;
  // Returns a Minimal-style adaptive thumbnail path. Normal cover ratios fill
  // the requested box; unusual ratios are contained inside the box.
  std::string getAdaptiveThumbBmpPath(int width, int height) const;
  // Deprecated compatibility wrapper; forwards to generateThumbBmp(0, height).
  [[deprecated("use generateThumbBmp(int width, int height)")]]
  bool generateThumbBmp(int height) const;
  // Writes a thumbnail BMP to cache. width <= 0 derives the default 3:5
  // (width:height) thumbnail width from height; height <= 0 uses the default
  // thumbnail height.
  // Returns false on missing cache/cover, unsupported image format, or conversion failure.
  bool generateThumbBmp(int width, int height) const;
  // Like generateThumbBmp, but does NOT build the full spine/TOC index
  // (book.bin) when the book hasn't been opened yet. Instead it parses only
  // content.opf to locate the cover image. Used by home/shelf cover loading
  // where scrolling past many never-opened books (e.g. the Recently Added
  // collection) would otherwise freeze the UI on full EPUB indexing — most
  // of which is wasted when the book turns out to have no extractable cover.
  // Returns false (in OPF-parse time) when there is no usable cover so the
  // caller can render a placeholder cheaply. Non-const because it (re)sets
  // the metadata cache and may run the OPF parse.
  bool generateThumbBmpNoIndex(int width, int height);
  // Writes a thumbnail that can either crop-to-fill or contain unusual cover
  // ratios, depending on the source image dimensions.
  bool generateAdaptiveThumbBmp(int width, int height) const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  // CrumBLE #134: look up an item's local-file-header offset in the
  // EPUB ZIP. Used by the .cmb converter to record image refs --
  // stored as offsets instead of paths so the reader can pull image
  // bytes from the EPUB without walking the central directory at
  // display time. Path normalised the same way as readItemContents*.
  bool getZipLocalHeaderOffset(const std::string& itemHref, uint32_t* offset) const;
  // CrumBLE: companion to getZipLocalHeaderOffset for the reader side.
  // Streams the entry's inflated bytes to `out`, seeking to
  // `localHeaderOffset` directly instead of walking the central
  // directory. Used by ChapterCmbSlimBuilder when rendering image
  // blocks (the .cmb image-ref table carries the offset).
  bool readItemContentsToStreamAtOffset(uint32_t localHeaderOffset, Print& out, size_t chunkSize) const;
  // Recover the entry's filename from its local file header. Used to
  // pick the right image decoder by extension once we've pulled bytes
  // out at a known offset (the .cmb image-ref table doesn't store
  // the filename to save space).
  bool getZipEntryFilenameAtOffset(uint32_t localHeaderOffset, std::string* filename) const;
  // CrumBLE #134: accessors for metadata fields the .cmb converter
  // needs to capture into the .cmb v3 metadata blob. Cover href and
  // text-reference href live inside the BookMetadataCache; the CSS
  // files vector lives on Epub itself. Pure additions; no
  // behavioural change for existing callers.
  const std::string& getCoverItemHref() const;
  const std::string& getTextReferenceHref() const;
  const std::vector<std::string>& getCssFiles() const { return cssFiles; }
  // CrumBLE #134: opportunistic .cmb writer. If no .cmb sidecar
  // exists next to the EPUB, converts the currently-loaded book and
  // writes one alongside the .epub. Subsequent opens (or post-
  // cache-clear reopens) pick it up via the .cmb fast path in
  // load(). Caller is expected to call this AFTER a successful
  // load() -- the conversion path depends on bookMetadataCache
  // being populated.
  //
  // Best-effort: returns true if a .cmb file exists (already there
  // OR just written), false on any failure. On failure, partial
  // output is removed so the next call retries cleanly.
  //
  // Synchronous and can take several seconds on big books (one
  // expat pass per chapter). Called automatically at the end of
  // load() so the next open hits the fast path; reader / utility
  // code can call it explicitly if they want to force a refresh.
  bool ensureCmbExists();
  // CrumBLE: true if the item is STORED (uncompressed) in the EPUB zip. A STORED
  // chapter needs no 32 KB DEFLATE window to cold-load, so the reader can build
  // it in place while BLE is connected instead of dropping/re-enabling BLE
  // (which re-fragments the heap). Returns false if the method can't be read.
  bool isItemStored(const std::string& itemHref) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  int resolveHrefToSpineIndex(const std::string& href) const;

 private:
  bool generateThumbBmpInternal(int width, int height, bool adaptiveContain) const;
  // Shared cover-image -> 1-bit BMP conversion used by both the cached
  // (generateThumbBmpInternal) and no-index (generateThumbBmpNoIndex)
  // thumbnail paths. coverImageHref must already be resolved against the
  // EPUB's content base path. adaptiveContain selects crop-to-fill vs
  // contain-unusual-ratios behaviour. Returns false for empty/unsupported
  // covers.
  bool convertCoverToThumbBmp(const std::string& coverImageHref, const std::string& thumbPath, int width,
                              int height, bool adaptiveContain = false) const;
};
