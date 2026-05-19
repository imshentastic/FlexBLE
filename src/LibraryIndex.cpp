#include "LibraryIndex.h"

#include <Arduino.h>  // millis()
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <unordered_set>

namespace {
constexpr char LIBRARY_INDEX_FILE[] = "/.crosspoint/library_index.json";
constexpr uint8_t LIBRARY_INDEX_VERSION = 1;
constexpr int MAX_WALK_DEPTH = 8;

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
  // Track whether the index file existed BEFORE we tried to load it so
  // the first-boot welcome screen can fire only on a genuine first run
  // (and not on every boot where the JSON has zero entries for some
  // other reason).
  freshFirstBoot = !Storage.exists(LIBRARY_INDEX_FILE);
  loadFromFile();
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
  if (progress) progress(70);

  // Build a hash set of currently-on-disk paths for O(N) diff.
  std::unordered_set<std::string> discoveredSet(discovered.begin(), discovered.end());

  // Remove entries whose files disappeared since last walk. Iterates
  // in reverse so erase indices stay valid.
  for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
    if (discoveredSet.find(entries[i].path) == discoveredSet.end()) {
      entries.erase(entries.begin() + i);
    }
  }
  if (progress) progress(80);

  // Pull existing paths into a set for the inverse diff (new files).
  std::unordered_set<std::string> knownSet;
  knownSet.reserve(entries.size());
  for (const auto& e : entries) knownSet.insert(e.path);

  // Each newly-discovered book gets a unique firstSeenMillis: the
  // walk's start time plus an increasing per-book offset. Without the
  // offset, an initial walk that discovers 500 books in one call would
  // give all of them the SAME millis() value — Date Added Newest/Oldest
  // would then be indistinguishable (just stable-sorted by walk order).
  // The offset preserves relative ordering AND makes future-added books
  // (later walks, after the user uploads more) cleanly sort after
  // anything from this walk.
  const uint64_t base = static_cast<uint64_t>(millis());
  uint64_t addOffset = 0;
  for (const auto& path : discovered) {
    if (knownSet.find(path) == knownSet.end()) {
      entries.push_back({path, base + addOffset});
      ++addOffset;
    }
  }
  if (progress) progress(95);

  walkPerformed = true;
  saveToFile();
  LOG_INF("LIB", "Library index now has %zu entries (walked %zu files)", entries.size(), discovered.size());
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

  String json = Storage.readFile(LIBRARY_INDEX_FILE);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto err = deserializeJson(doc, json.c_str());
  if (err) {
    LOG_ERR("LIB", "library_index.json parse error: %s", err.c_str());
    return false;
  }

  entries.clear();
  JsonArrayConst arr = doc["books"];
  if (!arr.isNull()) {
    entries.reserve(arr.size());
    for (JsonObjectConst entry : arr) {
      const std::string path = entry["path"] | std::string("");
      if (path.empty()) continue;
      LibraryEntry e;
      e.path = path;
      e.firstSeenMillis = entry["firstSeen"] | static_cast<uint64_t>(0);
      entries.push_back(std::move(e));
    }
  }
  LOG_DBG("LIB", "Loaded library index with %zu entries", entries.size());
  return true;
}

bool LibraryIndex::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["version"] = LIBRARY_INDEX_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& e : entries) {
    JsonObject entry = arr.add<JsonObject>();
    entry["path"] = e.path;
    entry["firstSeen"] = e.firstSeenMillis;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(LIBRARY_INDEX_FILE, json);
}
