#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// CrumBLE — per-book series metadata cache, persistent across reboots.
// Populated lazily (when a collection with collapse-series enabled is
// rendered, we parse OPFs for any of its books we haven't seen yet) so
// the first SD walk stays fast.
//
// Ported in spirit from dawsonfi/aalu (MIT, Copyright (c) 2025 Dave
// Allie) — same idea (per-book {seriesName, seriesIndex}, JSON cache,
// case-folded "series key" for grouping). Renamed and rewritten to fit
// CrumBLE's existing CollectionsStore / LibraryIndex architecture.
//
// Storage shape:
//   /.crosspoint/series_index.json
//   { "version": 1, "books": [ {"path":"…", "name":"Foundation",
//                                "index":"2"}, … ] }
//
// `seriesIndex` is stored as a string (matches aalu) so "1", "1.5",
// "VII", or "Vol. 3" round-trip exactly for display. Sorting uses a
// parsed-numeric path with a string-fallback comparator.

struct SeriesEntry {
  std::string path;   // absolute book path
  std::string name;   // raw series name as it appeared in OPF
  std::string index;  // raw series_index / group-position (string)
};

class SeriesIndex {
  static SeriesIndex instance;
  // path → entry. unordered_map for O(1) by-path lookup during shelf
  // resolution (which is on the hot path).
  std::unordered_map<std::string, SeriesEntry> byPath;
  bool jsonLoaded = false;

  SeriesIndex() = default;

 public:
  static SeriesIndex& getInstance() { return instance; }

  // Loads /.crosspoint/series_index.json. Idempotent. Doesn't walk SD.
  void begin();

  // Records the series info parsed from a single book's OPF. Empty
  // values are honored — recording {path, "", ""} marks the book as
  // "checked, no series" so enrichment doesn't re-parse it on every
  // visit. Persists on each call. (Cheap: small JSON file.)
  void record(const std::string& path, const std::string& name, const std::string& index);

  // True if we've already attempted enrichment for this path (whether
  // we found series info or not).
  bool hasBeenChecked(const std::string& path) const;

  // Returns the entry for `path` or nullptr if we haven't checked it
  // yet. Caller can read .name (empty = no series) and .index.
  const SeriesEntry* find(const std::string& path) const;

  // Removes the entry — used when a book is deleted from disk.
  void forgetPath(const std::string& path);

  // Canonicalizes a series name into a stable key for grouping:
  //   - lowercase
  //   - collapses internal whitespace runs to single space
  //   - trim
  // Tolerates capitalization drift (e.g. "Foundation" vs "FOUNDATION").
  static std::string seriesKey(const std::string& rawName);

  // Compares two series indices for sorting WITHIN a series. Parses
  // leading numeric portion of each (handles "1", "1.5", "10");
  // ties / unparseable indices fall back to lexicographic compare.
  static bool indexLess(const std::string& a, const std::string& b);

 private:
  bool loadFromFile();
  bool saveToFile() const;
};
