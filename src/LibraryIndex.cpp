#include "LibraryIndex.h"

#include <Arduino.h>  // millis()
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace {
constexpr char LIBRARY_INDEX_FILE[] = "/.crosspoint/library_index.json";
// On-disk format marker (first line). The file is a streamed, line-based
// format — NOT JSON, despite the historical .json filename — written and
// read one entry at a time so we never hold a second full in-RAM copy of
// every path (a JsonDocument + serialized String). That doubled peak memory
// and could OOM mid-save on large libraries, which left the index file
// unwritten and boot-looped the "indexing your library" screen. Each entry
// line is "<firstSeenMillis>\t<path>"; tab/newline can't appear in FAT/exFAT
// filenames so no escaping is needed. An older single-blob JSON index starts
// with '{' (≠ the marker's 'C'), so loadFromFile rejects it and a rescan
// rewrites it in this format.
constexpr char LIBRARY_INDEX_HEADER[] = "CRUMBLE-LIBIDX v1";
constexpr int MAX_WALK_DEPTH = 8;

// Buffered line reader over a HalFile/FsFile. Reads in chunks (one HAL call
// per ~256 bytes instead of per byte) and splits on '\n'. Over-long lines
// (e.g. an old single-line JSON index) are truncated at kMaxLine so a corrupt
// or legacy file can't balloon a single std::string — we only need enough to
// reject it via the header check.
class LineReader {
  FsFile& file;
  char buf[256];
  int len = 0;
  int pos = 0;
  static constexpr size_t kMaxLine = 1024;

 public:
  explicit LineReader(FsFile& f) : file(f) {}
  // Reads the next line into `out` (without the trailing newline). Returns
  // false only at end-of-file with nothing read.
  bool next(std::string& out) {
    out.clear();
    bool any = false;
    for (;;) {
      if (pos >= len) {
        len = file.read(buf, sizeof(buf));
        pos = 0;
        if (len <= 0) break;  // EOF or error
      }
      const char c = buf[pos++];
      any = true;
      if (c == '\n') return true;
      if (c == '\r') continue;  // tolerate CRLF
      if (out.size() < kMaxLine) out.push_back(c);
    }
    return any;
  }
};

bool iLess(const std::string& a, const std::string& b) {
  // Case-insensitive less-than for the default "All Books" sort order.
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca != cb) return ca < cb;
  }
  return a.size() < b.size();
}
}  // namespace

LibraryIndex LibraryIndex::instance;

bool LibraryIndex::isBookPath(const std::string& path) {
  if (!(FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path) ||
        FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path))) {
    return false;
  }
  // Blacklist: system files that happen to share a book extension. Match
  // by basename (case-insensitive) so a user file with a similar name in
  // a subdirectory still gets indexed.
  const size_t lastSlash = path.find_last_of('/');
  const std::string basename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
  std::string lower = basename;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // Firmware diagnostics — crash dump written by the panic handler.
  if (lower == "crash_report.txt") return false;
  // Reserved for future system files. Extend rather than scatter checks
  // throughout the walker.
  return true;
}

void LibraryIndex::begin() {
  if (jsonLoaded) return;
  // freshFirstBoot drives the welcome "indexing your library" screen + the
  // initial SD walk in main.cpp. We fire it whenever there's no *usable*
  // index: a genuine first run, a wiped cache, OR an upgrade from the old
  // single-blob JSON format (loadFromFile rejects that and returns false).
  // Keying off load success rather than mere file existence means the walk
  // self-heals a corrupt/legacy index instead of leaving collections empty.
  const bool loaded = loadFromFile();
  freshFirstBoot = !loaded;
  jsonLoaded = true;
}

void LibraryIndex::ensureWalked(const std::function<void(int)>& progress) {
  if (walkPerformed) return;
  rescan(progress);
}

void LibraryIndex::rescan(const std::function<void(int)>& progress) {
  // Walk the whole SD, accumulating every book file path we see.
  // walkRecursive is read-only — we apply the diff to `entries`
  // afterwards so callers see a consistent state during the walk.
  std::vector<std::string> discovered;
  if (progress) progress(5);
  walkRecursive("/", 0, discovered);
  const size_t walkedCount = discovered.size();
  if (progress) progress(60);

  // Each newly-discovered book gets a unique firstSeenMillis: the walk's
  // start time plus an increasing per-book offset. Without the offset, an
  // initial walk that discovers 500 books in one call would give all of them
  // the SAME millis() value — Date Added Newest/Oldest would then be
  // indistinguishable (just stable-sorted by walk order). The offset
  // preserves relative ordering AND makes future-added books (later walks,
  // after the user uploads more) cleanly sort after anything from this walk.
  const uint64_t base = static_cast<uint64_t>(millis());
  uint64_t addOffset = 0;

  if (entries.empty()) {
    // Fresh index (first boot / wiped cache / upgrade from old format):
    // every discovered file is new, so we can skip the membership-set diff
    // entirely — that path holds two extra full copies of every path (a
    // discovered-set + a known-set), which is the memory blowup that
    // OOM-boot-looped large libraries. MOVE each path straight into the
    // entry list so we never hold two copies of the same string at once.
    entries.reserve(discovered.size());
    for (auto& path : discovered) {
      entries.push_back({std::move(path), base + addOffset});
      ++addOffset;
    }
  } else {
    // Incremental refresh: diff the walk against the existing index. Each
    // lookup set is scoped so only one heavy structure is alive at a time.
    {
      // Remove entries whose file disappeared. Iterate in reverse so erase
      // indices stay valid.
      std::unordered_set<std::string> discoveredSet(discovered.begin(), discovered.end());
      for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
        if (discoveredSet.find(entries[i].path) == discoveredSet.end()) {
          entries.erase(entries.begin() + i);
        }
      }
    }  // discoveredSet freed here
    if (progress) progress(75);

    // Add newly-discovered files. Build the known-set from the (now pruned)
    // entries, then MOVE new paths in; already-known paths stay in
    // `discovered` and are freed below.
    std::unordered_set<std::string> knownSet;
    knownSet.reserve(entries.size());
    for (const auto& e : entries) knownSet.insert(e.path);
    for (auto& path : discovered) {
      if (knownSet.find(path) == knownSet.end()) {
        entries.push_back({std::move(path), base + addOffset});
        ++addOffset;
      }
    }
  }
  if (progress) progress(90);

  // Free the walk's path list before saving so the (streaming) writer runs
  // with maximum heap headroom.
  discovered.clear();
  discovered.shrink_to_fit();

  walkPerformed = true;
  saveToFile();
  LOG_INF("LIB", "Library index now has %zu entries (walked %zu files)", entries.size(), walkedCount);
  if (progress) progress(100);
}

std::vector<std::string> LibraryIndex::getAllBookPaths() const {
  std::vector<std::string> out;
  out.reserve(entries.size());
  for (const auto& e : entries) out.push_back(e.path);
  std::sort(out.begin(), out.end(), iLess);
  return out;
}

std::vector<std::string> LibraryIndex::getRecentlyAddedPaths(int maxCount) const {
  // Copy + sort the entries by firstSeen DESC, then materialize the
  // top-N paths. We sort the whole vector rather than partial_sort
  // because N is small (vector size is bounded by physical books) and
  // the simpler code is easier to reason about.
  std::vector<LibraryEntry> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
            [](const LibraryEntry& a, const LibraryEntry& b) { return a.firstSeenMillis > b.firstSeenMillis; });
  std::vector<std::string> out;
  const int n = std::min(maxCount, static_cast<int>(sorted.size()));
  out.reserve(n);
  for (int i = 0; i < n; ++i) out.push_back(sorted[i].path);
  return out;
}

uint64_t LibraryIndex::getFirstSeen(const std::string& path) const {
  for (const auto& e : entries) {
    if (e.path == path) return e.firstSeenMillis;
  }
  return 0;
}

void LibraryIndex::forgetPath(const std::string& path) {
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&](const LibraryEntry& e) { return e.path == path; });
  if (it == entries.end()) return;
  entries.erase(it);
  saveToFile();
}

void LibraryIndex::walkRecursive(const std::string& dirPath, int depth, std::vector<std::string>& outPaths) const {
  if (depth > MAX_WALK_DEPTH) {
    LOG_DBG("LIB", "walk depth cap reached at %s", dirPath.c_str());
    return;
  }
  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char nameBuf[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(nameBuf, sizeof(nameBuf));
    const std::string name(nameBuf);
    if (name.empty() || name[0] == '.') {
      // Skip dot-prefixed names — hidden / system directories like
      // ".crosspoint", ".Trashes", ".Spotlight-V100". The user's library
      // shouldn't recurse into our own cache.
      entry.close();
      continue;
    }
    std::string childPath = dirPath;
    if (!childPath.empty() && childPath.back() != '/') childPath += '/';
    childPath += name;

    if (entry.isDirectory()) {
      entry.close();  // close before recursing so we don't pile open file handles.
      walkRecursive(childPath, depth + 1, outPaths);
    } else {
      if (isBookPath(childPath)) {
        outPaths.push_back(childPath);
      }
      entry.close();
    }
  }
  dir.close();
}

bool LibraryIndex::loadFromFile() {
  if (!Storage.exists(LIBRARY_INDEX_FILE)) return false;

  FsFile file;
  if (!Storage.openFileForRead("LIB", LIBRARY_INDEX_FILE, file)) return false;

  // Stream the file line by line so we never hold the whole thing (plus a
  // JsonDocument) in RAM. The first line must be our format marker; an older
  // single-blob JSON index won't match (it starts with '{'), so we reject it
  // and let a rescan rewrite it — without ever slurping the huge old file.
  entries.clear();
  LineReader reader(file);
  std::string line;
  bool headerOk = false;
  size_t lineNo = 0;
  while (reader.next(line)) {
    if (lineNo++ == 0) {
      headerOk = (line == LIBRARY_INDEX_HEADER);
      if (!headerOk) break;
      continue;
    }
    if (line.empty()) continue;
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    // strtoull stops at the tab, so parsing the whole c_str is safe.
    const uint64_t firstSeen = strtoull(line.c_str(), nullptr, 10);
    std::string path = line.substr(tab + 1);
    if (path.empty()) continue;
    entries.push_back({std::move(path), firstSeen});
  }
  file.close();

  if (!headerOk) {
    entries.clear();  // legacy/corrupt file — treat as no index, force a rescan
    return false;
  }
  LOG_DBG("LIB", "Loaded library index with %zu entries", entries.size());
  return true;
}

bool LibraryIndex::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  FsFile file;
  if (!Storage.openFileForWrite("LIB", LIBRARY_INDEX_FILE, file)) {
    LOG_ERR("LIB", "Could not open library index for writing");
    return false;
  }
  // Stream entries one line at a time ("<firstSeen>\t<path>") so we never
  // build a JsonDocument + serialized String copy of the whole index in RAM
  // (the OOM-on-save that boot-looped large libraries).
  file.print(LIBRARY_INDEX_HEADER);
  file.print("\n");
  char numbuf[24];
  for (const auto& e : entries) {
    snprintf(numbuf, sizeof(numbuf), "%llu", static_cast<unsigned long long>(e.firstSeenMillis));
    file.print(numbuf);
    file.print("\t");
    file.print(e.path.c_str());
    file.print("\n");
  }
  file.close();
  return true;
}
