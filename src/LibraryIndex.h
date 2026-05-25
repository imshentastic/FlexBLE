#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// CrumBLE — persistent index of every book file the device has seen on
// SD, with a first-seen timestamp per entry. Backs the auto-managed
// virtual collections "All Books" and "Recently Added" (sorted by
// firstSeen DESC). Lives at /.crosspoint/library_index.json so a full SD
// walk only needs to happen once (initial discovery) plus on explicit
// rescan; subsequent boots load the cached index in milliseconds.
//
// Memory shape: ~80 bytes per entry (path string + uint64). 500-book
// library ≈ 40 KB — well within budget on the ESP32-C3.

struct LibraryEntry {
  std::string path;
  // Sort key for "Recently Added" / "Date Added" (DESC = newest first). Despite
  // the legacy name, this is a persistent monotonic "first seen by the device"
  // counter, NOT a wall-clock time: millis() resets each boot, and file
  // timestamps are unreliable on this device (WiFi transfers stamp a default
  // date when the clock is unset). New books -- and same-name files whose size
  // changed (a replaced copy) -- get the next counter value so they sort newest.
  uint64_t firstSeenMillis = 0;
  // File size in bytes at last index (capped to uint32). Lets a rescan notice a
  // same-path file was replaced and re-date it. 0 = unknown (legacy entry).
  uint32_t fileSize = 0;
};

class LibraryIndex {
  static LibraryIndex instance;
  std::vector<LibraryEntry> entries;
  bool jsonLoaded = false;  // /.crosspoint/library_index.json has been read
  bool walkPerformed = false;  // an SD walk has run this session (refresh or initial)
  bool freshFirstBoot = false;  // begin() ran with no index file on SD — used to gate welcome UI

  LibraryIndex() = default;

 public:
  static LibraryIndex& getInstance() { return instance; }

  // Loads /.crosspoint/library_index.json if it exists. Idempotent.
  // Does NOT trigger an SD walk — that's deferred to ensureWalked() so
  // boot stays fast.
  void begin();

  // True when begin() ran and found no existing index file. main.cpp
  // checks this to drive the first-boot welcome screen — the SD walk
  // gets framed as "indexing your library" rather than an unexplained
  // pause on a later L/R cycle.
  bool wasFreshFirstBoot() const { return freshFirstBoot; }

  // If we haven't done an SD walk this session, do one now. Adds new
  // book files to the index with current millis() as firstSeen and
  // removes entries whose underlying file disappeared. Progress callback
  // is invoked periodically with 0..100 — wire to GUI.fillPopupProgress
  // for a visible loading bar during the first walk.
  void ensureWalked(const std::function<void(int /*progress0to100*/)>& progress = nullptr);

  // Force a rescan even if walkPerformed is already true (e.g. user
  // triggered "Rescan library" manually).
  void rescan(const std::function<void(int)>& progress = nullptr);

  // Marks the index as needing a fresh walk on next access. Used by
  // file-server / hotspot exit paths where books may have just been
  // uploaded over WiFi — the walk itself is still lazy (deferred to
  // next virtual-collection access) so onExit stays snappy.
  void markStale() { walkPerformed = false; }

  // CrumBLE: free the in-RAM entry vector to reclaim heap. The index can be
  // tens of KB for a large library (~80 bytes/entry), which is dead weight
  // while the WiFi web server runs — file transfer is desperately heap-bound
  // (~25 KB free), so releasing it here roughly doubles available headroom.
  // The next ensureWalked()/begin() repopulates it from the on-disk JSON plus
  // an SD walk; web-server exit paths already markStale() so the rebuild is
  // automatic. Only call when nothing is actively reading the index.
  void releaseMemory() {
    std::vector<LibraryEntry>().swap(entries);  // free capacity, not just size
    jsonLoaded = false;
    walkPerformed = false;
  }

  // Read accessors. Both produce a fresh vector copy — the underlying
  // index is small enough that this is cheap.
  std::vector<std::string> getAllBookPaths() const;        // sorted by path (case-insensitive)
  std::vector<std::string> getRecentlyAddedPaths(int maxCount) const;  // newest first
  // Returns the firstSeenMillis recorded for `path`. Used by
  // CollectionsStore::resolveBookPaths to sort user collections by
  // Date Added without duplicating the timestamp into the
  // collection's stored payload. Returns 0 if the path isn't in the
  // index (e.g. user added a book to Favorites that hasn't been
  // walked yet — the unknown books sort to the end / start depending
  // on Asc/Desc).
  uint64_t getFirstSeen(const std::string& path) const;

  // Bookkeeping after a destructive action elsewhere (e.g. file delete).
  // The next ensureWalked() would pick this up, but for instant UI
  // refresh callers can drop the entry directly.
  void forgetPath(const std::string& path);

 private:
  bool loadFromFile();
  bool saveToFile() const;
  // Recursive descent. depth caps at 8 to avoid runaway nesting. Skips
  // dot-prefixed directories (".crosspoint", ".Trashes", etc.). For each book
  // file found, appends its path to outPaths and its size (bytes) to outSizes at
  // the same index.
  void walkRecursive(const std::string& dirPath, int depth, std::vector<std::string>& outPaths,
                     std::vector<uint32_t>& outSizes) const;
  // True if the file extension marks it as one of our supported book
  // formats (.epub / .xtc / .txt / .md / .markdown).
  static bool isBookPath(const std::string& path);
};
