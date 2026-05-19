#pragma once
#include <string>
#include <vector>

// FlexBLE Collections — user-defined tag groups that live on top of the
// filesystem. Books can belong to zero or more collections regardless of where
// they sit on the SD card. Phase 1 ships with a single hardcoded "Favorites"
// collection; later phases add a picker, custom collections, and series
// support. See spec in PR for the long-term shape.
//
// Storage: /.crosspoint/collections.json. In-memory the entire model is a
// vector of Collection records — typically <5 KB total — so all accesses
// are O(N) over a tiny N and run during the render path without SD I/O.

struct Collection {
  std::string id;    // stable machine identifier (e.g. "favorites")
  std::string name;  // user-facing label
  std::vector<std::string> bookPaths;
};

class CollectionsStore {
  static CollectionsStore instance;

  std::vector<Collection> collections;
  std::string activeId;

 public:
  // Default collection id created on first boot when no JSON exists.
  static constexpr const char* FAVORITES_ID = "favorites";
  static constexpr const char* FAVORITES_NAME = "Favorites";

  ~CollectionsStore() = default;

  static CollectionsStore& getInstance() { return instance; }

  // Loads collections.json from SD; seeds default "Favorites" if missing.
  // Idempotent — safe to call once at boot from main.cpp.
  void begin();

  // Mutators (auto-save after each).
  // Returns true if the book is now IN the collection after toggling.
  bool toggleBookInCollection(const std::string& collectionId, const std::string& bookPath);
  void setActiveId(const std::string& id);
  // Removes the given book from every collection it appears in (used when
  // the book file is being deleted off the SD card). Auto-saves at the
  // end if any change was made. Returns the number of collections that
  // had to be modified — informational only.
  int removeBookFromAllCollections(const std::string& bookPath);

  // Read-only accessors.
  bool isBookInCollection(const std::string& collectionId, const std::string& bookPath) const;
  const Collection* findCollection(const std::string& collectionId) const;
  const std::vector<Collection>& getCollections() const { return collections; }
  const std::string& getActiveId() const { return activeId; }
  const Collection* getActiveCollection() const { return findCollection(activeId); }

 private:
  CollectionsStore() = default;
  bool loadFromFile();
  bool saveToFile() const;
  // Ensures FAVORITES_ID exists in the collections vector. Used after load
  // when the file was missing/empty/corrupt, and on first boot.
  void seedDefaults();
};
