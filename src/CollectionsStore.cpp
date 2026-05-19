#include "CollectionsStore.h"

#include <Arduino.h>  // millis() for createCollection
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "LibraryIndex.h"

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

std::vector<std::string> CollectionsStore::resolveBookPaths(const std::string& collectionId) const {
  const Collection* c = findCollection(collectionId);
  if (c == nullptr) return {};
  if (!c->isVirtual) {
    return c->bookPaths;  // copy is intentional — caller may want to iterate concurrently.
  }
  // Virtual collection — pull live data from LibraryIndex. The first
  // call this session lazily kicks off the SD walk; subsequent calls
  // are in-memory.
  LibraryIndex::getInstance().ensureWalked();
  if (collectionId == RECENTLY_ADDED_ID) {
    return LibraryIndex::getInstance().getRecentlyAddedPaths(18);
  }
  if (collectionId == ALL_BOOKS_ID) {
    return LibraryIndex::getInstance().getAllBookPaths();
  }
  return {};
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
  collections.push_back({id, name, {}});
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
    JsonArray books = entry["books"].to<JsonArray>();
    for (const auto& path : c.bookPaths) books.add(path);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(COLLECTIONS_FILE, json);
}
