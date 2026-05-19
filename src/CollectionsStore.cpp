#include "CollectionsStore.h"

#include <Arduino.h>  // millis() for createCollection
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <unordered_map>

#include "LibraryIndex.h"
#include "RecentBooksStore.h"
#include "SeriesIndex.h"

#include <unordered_set>

namespace {
constexpr char COLLECTIONS_FILE[] = "/.crosspoint/collections.json";
constexpr uint8_t COLLECTIONS_FILE_VERSION = 1;
}  // namespace

CollectionsStore CollectionsStore::instance;

void CollectionsStore::begin() {
  if (!loadFromFile()) {
    LOG_DBG("CLN", "No collections.json found, seeding defaults");
    collections.clear();
    seedDefaults();
    saveToFile();
  }
  // Defensive: even after a successful load, make sure Favorites still
  // exists. A user could have hand-edited the JSON and removed it.
  if (findCollection(FAVORITES_ID) == nullptr) {
    seedDefaults();
    saveToFile();
  }
  // Always re-pin the virtual collections at the end of the list so they
  // appear AFTER user-created ones in the L/R cycle. We re-create them
  // every begin() because they're not persisted (their book lists come
  // from LibraryIndex at access time, not from collections.json).
  auto seedVirtual = [this](const char* id, const char* name) {
    if (findCollection(id) == nullptr) {
      Collection v;
      v.id = id;
      v.name = name;
      v.isVirtual = true;
      collections.push_back(std::move(v));
    }
  };
  seedVirtual(RECENTLY_ADDED_ID, RECENTLY_ADDED_NAME);
  seedVirtual(ALL_BOOKS_ID, ALL_BOOKS_NAME);

  if (activeId.empty() || findCollection(activeId) == nullptr) {
    activeId = FAVORITES_ID;
  }
}

void CollectionsStore::seedDefaults() {
  if (findCollection(FAVORITES_ID) == nullptr) {
    collections.push_back({FAVORITES_ID, FAVORITES_NAME, {}});
  }
  if (activeId.empty()) {
    activeId = FAVORITES_ID;
  }
}

const Collection* CollectionsStore::findCollection(const std::string& collectionId) const {
  for (const auto& c : collections) {
    if (c.id == collectionId) return &c;
  }
  return nullptr;
}

bool CollectionsStore::isBookInCollection(const std::string& collectionId, const std::string& bookPath) const {
  const Collection* c = findCollection(collectionId);
  if (!c) return false;
  // For user collections we have an exact in-memory list. For virtuals
  // we'd have to scan LibraryIndex, which is expensive — and the picker
  // (the only realistic caller) hides virtuals anyway. So virtual
  // membership lookup returns false here, which is the safe default.
  if (c->isVirtual) return false;
  return std::find(c->bookPaths.begin(), c->bookPaths.end(), bookPath) != c->bookPaths.end();
}

namespace {
// Case-insensitive less-than over filenames (basename). Used by both
// TitleAlpha sort here and the LibraryIndex's All-Books default.
bool basenameLess(const std::string& a, const std::string& b) {
  const size_t sa = a.find_last_of('/');
  const size_t sb = b.find_last_of('/');
  const char* aBase = a.c_str() + (sa == std::string::npos ? 0 : sa + 1);
  const char* bBase = b.c_str() + (sb == std::string::npos ? 0 : sb + 1);
  while (*aBase && *bBase) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(*aBase)));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(*bBase)));
    if (ca != cb) return ca < cb;
    ++aBase;
    ++bBase;
  }
  return *aBase == 0 && *bBase != 0;
}

void applySort(std::vector<std::string>& paths, CollectionSort mode) {
  switch (mode) {
    case CollectionSort::Manual:
      // No-op — caller already produced the manual order (user
      // collection's stored bookPaths, or virtual's natural order).
      return;
    case CollectionSort::TitleAlpha:
      std::sort(paths.begin(), paths.end(), basenameLess);
      return;
    case CollectionSort::TitleAlphaDesc:
      std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) { return basenameLess(b, a); });
      return;
    case CollectionSort::DateAddedDesc:
    case CollectionSort::DateAddedAsc: {
      // Cache lookups: for each path, resolve firstSeenMillis once
      // and stash into a parallel vector. Sort indices, then permute
      // paths. Avoids O(N^2) LibraryIndex lookups inside the
      // comparator.
      const auto& idx = LibraryIndex::getInstance();
      std::vector<uint64_t> times(paths.size());
      for (size_t i = 0; i < paths.size(); ++i) times[i] = idx.getFirstSeen(paths[i]);
      std::vector<size_t> order(paths.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = i;
      const bool desc = mode == CollectionSort::DateAddedDesc;
      std::sort(order.begin(), order.end(),
                [&](size_t a, size_t b) { return desc ? times[a] > times[b] : times[a] < times[b]; });
      std::vector<std::string> sorted;
      sorted.reserve(paths.size());
      for (size_t i : order) sorted.push_back(std::move(paths[i]));
      paths = std::move(sorted);
      return;
    }
    case CollectionSort::DateLastReadDesc: {
      // Build a map of path → position in RECENT_BOOKS (0 = most
      // recently read). Books not in RECENT_BOOKS get a sentinel
      // position past the end so they sort to the back of the list.
      // Result: most-recently-opened books bubble to the top, never-
      // opened books form a tail at the bottom (relative order among
      // tail is the input order).
      const auto& recents = RECENT_BOOKS.getBooks();
      std::unordered_map<std::string, size_t> posByPath;
      posByPath.reserve(recents.size());
      for (size_t i = 0; i < recents.size(); ++i) posByPath[recents[i].path] = i;
      const size_t sentinel = recents.size() + 1;
      std::vector<size_t> rank(paths.size(), sentinel);
      for (size_t i = 0; i < paths.size(); ++i) {
        auto it = posByPath.find(paths[i]);
        if (it != posByPath.end()) rank[i] = it->second;
      }
      std::vector<size_t> order(paths.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = i;
      // Stable sort by rank ASC (so RECENT_BOOKS[0] comes first). Ties
      // (both unread) preserve input order.
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rank[a] < rank[b]; });
      std::vector<std::string> sorted;
      sorted.reserve(paths.size());
      for (size_t i : order) sorted.push_back(std::move(paths[i]));
      paths = std::move(sorted);
      return;
    }
  }
}
}  // namespace

std::vector<std::string> CollectionsStore::resolveBookPaths(const std::string& collectionId) const {
  const Collection* c = findCollection(collectionId);
  if (c == nullptr) return {};
  std::vector<std::string> paths;
  if (!c->isVirtual) {
    paths = c->bookPaths;  // start from stored manual order.
  } else {
    // Virtual collection — pull live data from LibraryIndex. The first
    // call this session lazily kicks off the SD walk; subsequent calls
    // are in-memory.
    LibraryIndex::getInstance().ensureWalked();
    if (collectionId == RECENTLY_ADDED_ID) {
      // Recently Added is INTRINSICALLY ordered by firstSeen DESC —
      // user can't override this. Always return top-18 newest.
      return LibraryIndex::getInstance().getRecentlyAddedPaths(18);
    }
    if (collectionId == ALL_BOOKS_ID) {
      // Defaults to TitleAlpha (already sorted by getAllBookPaths()).
      // The sortMode override lets user re-sort by Date Added if they
      // prefer.
      paths = LibraryIndex::getInstance().getAllBookPaths();
    }
  }
  // Apply user-chosen sort. Manual mode is a no-op so the stored
  // order survives for user collections.
  applySort(paths, c->sortMode);
  return paths;
}

int CollectionsStore::countBooksInCollection(const std::string& collectionId) const {
  const Collection* c = findCollection(collectionId);
  if (c == nullptr) return 0;
  if (!c->isVirtual) return static_cast<int>(c->bookPaths.size());
  return static_cast<int>(resolveBookPaths(collectionId).size());
}

bool CollectionsStore::toggleBookInCollection(const std::string& collectionId, const std::string& bookPath) {
  for (auto& c : collections) {
    if (c.id != collectionId) continue;
    if (c.isVirtual) {
      LOG_ERR("CLN", "Refusing to toggle membership on virtual collection: %s", collectionId.c_str());
      return false;
    }
    auto it = std::find(c.bookPaths.begin(), c.bookPaths.end(), bookPath);
    bool nowIn;
    if (it != c.bookPaths.end()) {
      c.bookPaths.erase(it);
      nowIn = false;
    } else {
      c.bookPaths.push_back(bookPath);
      nowIn = true;
    }
    saveToFile();
    LOG_DBG("CLN", "%s %s in %s (size=%zu)", nowIn ? "Added" : "Removed", bookPath.c_str(), collectionId.c_str(),
            c.bookPaths.size());
    return nowIn;
  }
  LOG_ERR("CLN", "Toggle requested for unknown collection: %s", collectionId.c_str());
  return false;
}

std::string CollectionsStore::createCollection(const std::string& name) {
  if (name.empty()) {
    LOG_ERR("CLN", "createCollection refused: empty name");
    return {};
  }
  // ID derived from millis() — guaranteed unique on any human-paced
  // create cadence, and avoids needing a stable hash of the (possibly
  // user-edited) name. Prefix "c_" so ids never collide with the seeded
  // FAVORITES_ID (which is plain "favorites").
  std::string id = "c_" + std::to_string(millis());
  // Defensive: if two creates happen in the same millisecond (e.g. a
  // batch import), suffix with the existing count to disambiguate.
  while (findCollection(id) != nullptr) {
    id += "_x";
  }
  Collection c;
  c.id = id;
  c.name = name;
  collections.push_back(std::move(c));
  saveToFile();
  LOG_INF("CLN", "Created collection: %s (id=%s)", name.c_str(), id.c_str());
  return id;
}

int CollectionsStore::removeBookFromAllCollections(const std::string& bookPath) {
  int touched = 0;
  for (auto& c : collections) {
    auto it = std::find(c.bookPaths.begin(), c.bookPaths.end(), bookPath);
    if (it != c.bookPaths.end()) {
      c.bookPaths.erase(it);
      touched++;
    }
  }
  if (touched > 0) {
    saveToFile();
    LOG_DBG("CLN", "Removed %s from %d collection(s)", bookPath.c_str(), touched);
  }
  return touched;
}

void CollectionsStore::setSortMode(const std::string& collectionId, CollectionSort mode) {
  for (auto& c : collections) {
    if (c.id != collectionId) continue;
    if (c.isVirtual && mode == CollectionSort::Manual) {
      LOG_ERR("CLN", "Refusing Manual sort on virtual collection: %s", collectionId.c_str());
      return;
    }
    if (c.sortMode == mode) return;
    c.sortMode = mode;
    // Only persist for user collections — virtuals reseed every begin()
    // and would lose their sortMode anyway. (Could persist a separate
    // map in future if we wanted virtual-collection prefs to survive
    // reboots; deferred.)
    if (!c.isVirtual) saveToFile();
    LOG_DBG("CLN", "Set sort mode for %s to %u", collectionId.c_str(), static_cast<unsigned>(mode));
    return;
  }
  LOG_ERR("CLN", "setSortMode: unknown collection %s", collectionId.c_str());
}

void CollectionsStore::setCollapseSeries(const std::string& collectionId, bool on) {
  for (auto& c : collections) {
    if (c.id != collectionId) continue;
    if (c.collapseSeries == on) return;
    c.collapseSeries = on;
    if (!c.isVirtual) saveToFile();
    LOG_DBG("CLN", "Set collapseSeries for %s to %d", collectionId.c_str(), on ? 1 : 0);
    return;
  }
  LOG_ERR("CLN", "setCollapseSeries: unknown collection %s", collectionId.c_str());
}

std::vector<ShelfEntry> CollectionsStore::resolveShelfEntries(const std::string& collectionId) const {
  const Collection* c = findCollection(collectionId);
  if (c == nullptr) return {};
  const std::vector<std::string> paths = resolveBookPaths(collectionId);

  // Fast path: collapse disabled or empty list — 1:1 wrap into single-
  // book entries. Same shape, no SeriesIndex lookups.
  if (!c->collapseSeries || paths.empty()) {
    std::vector<ShelfEntry> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
      ShelfEntry e;
      e.firstPath = p;
      e.memberPaths.push_back(p);
      out.push_back(std::move(e));
    }
    return out;
  }

  // Collapse path. Walk paths in their sorted order. The FIRST time we
  // see a series-key, collect every same-key book in the collection
  // (might not be consecutive after sort), sort that group by series
  // index ASC, and emit as one ShelfEntry. Series with only one member
  // present in this collection stay as single-book entries (the user's
  // bar is "2+ books" for grouping, per their spec).
  std::unordered_set<std::string> seenKeys;
  std::vector<ShelfEntry> out;
  out.reserve(paths.size());
  auto& seriesIdx = SeriesIndex::getInstance();
  for (const auto& p : paths) {
    const SeriesEntry* se = seriesIdx.find(p);
    if (se == nullptr || se->name.empty()) {
      ShelfEntry e;
      e.firstPath = p;
      e.memberPaths.push_back(p);
      out.push_back(std::move(e));
      continue;
    }
    const std::string key = SeriesIndex::seriesKey(se->name);
    if (seenKeys.count(key)) continue;  // grouped already; subsequent members suppressed.
    seenKeys.insert(key);

    // Collect every book in this collection that maps to the same key.
    std::vector<std::string> members;
    for (const auto& q : paths) {
      const SeriesEntry* qse = seriesIdx.find(q);
      if (qse != nullptr && !qse->name.empty() && SeriesIndex::seriesKey(qse->name) == key) {
        members.push_back(q);
      }
    }
    if (members.size() < 2) {
      // Singleton: render as a normal cell (no spine). Use the path we
      // were already iterating on so output order isn't disturbed.
      ShelfEntry e;
      e.firstPath = p;
      e.memberPaths.push_back(p);
      out.push_back(std::move(e));
      continue;
    }
    // Sort by series index ASC (numeric-aware comparator).
    std::sort(members.begin(), members.end(), [&](const std::string& a, const std::string& b) {
      const SeriesEntry* ea = seriesIdx.find(a);
      const SeriesEntry* eb = seriesIdx.find(b);
      return SeriesIndex::indexLess(ea ? ea->index : "", eb ? eb->index : "");
    });
    ShelfEntry e;
    e.firstPath = members.front();
    e.seriesName = se->name;
    e.memberPaths = std::move(members);
    out.push_back(std::move(e));
  }
  return out;
}

void CollectionsStore::setActiveId(const std::string& id) {
  if (findCollection(id) == nullptr) {
    LOG_ERR("CLN", "Refusing to set active collection to unknown id: %s", id.c_str());
    return;
  }
  if (activeId != id) {
    activeId = id;
    saveToFile();
  }
}

bool CollectionsStore::loadFromFile() {
  if (!Storage.exists(COLLECTIONS_FILE)) return false;

  String json = Storage.readFile(COLLECTIONS_FILE);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto err = deserializeJson(doc, json.c_str());
  if (err) {
    LOG_ERR("CLN", "collections.json parse error: %s", err.c_str());
    return false;
  }

  collections.clear();
  activeId = doc["active"] | std::string(FAVORITES_ID);

  JsonArrayConst arr = doc["collections"];
  if (!arr.isNull()) {
    for (JsonObjectConst entry : arr) {
      Collection c;
      c.id = entry["id"] | std::string("");
      c.name = entry["name"] | std::string("");
      if (c.id.empty()) continue;
      // Backwards compat: missing "sort" key (older JSON) defaults to
      // Manual — same as before sort-mode existed.
      const unsigned sortRaw = entry["sort"] | 0u;
      if (sortRaw <= static_cast<unsigned>(CollectionSort::DateLastReadDesc)) {
        c.sortMode = static_cast<CollectionSort>(sortRaw);
      }
      // collapseSeries defaults to true (matches struct default). Older
      // JSON without the key picks up the default automatically.
      c.collapseSeries = entry["collapseSeries"] | true;
      JsonArrayConst books = entry["books"];
      if (!books.isNull()) {
        c.bookPaths.reserve(books.size());
        for (JsonVariantConst path : books) {
          const std::string p = path | std::string("");
          if (!p.empty()) c.bookPaths.push_back(p);
        }
      }
      collections.push_back(std::move(c));
    }
  }

  LOG_DBG("CLN", "Loaded %zu collection(s); active=%s", collections.size(), activeId.c_str());
  return true;
}

bool CollectionsStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["version"] = COLLECTIONS_FILE_VERSION;
  doc["active"] = activeId;
  JsonArray arr = doc["collections"].to<JsonArray>();
  for (const auto& c : collections) {
    if (c.isVirtual) continue;  // virtuals are rebuilt every begin() — don't waste SD space persisting them.
    JsonObject entry = arr.add<JsonObject>();
    entry["id"] = c.id;
    entry["name"] = c.name;
    entry["sort"] = static_cast<unsigned>(c.sortMode);
    entry["collapseSeries"] = c.collapseSeries;
    JsonArray books = entry["books"].to<JsonArray>();
    for (const auto& path : c.bookPaths) books.add(path);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(COLLECTIONS_FILE, json);
}
