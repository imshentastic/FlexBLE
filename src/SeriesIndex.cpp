#include "SeriesIndex.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>

namespace {
constexpr char SERIES_INDEX_FILE[] = "/.crosspoint/series_index.json";
constexpr uint8_t SERIES_INDEX_VERSION = 1;
}  // namespace

SeriesIndex SeriesIndex::instance;

void SeriesIndex::begin() {
  if (jsonLoaded) return;
  loadFromFile();
  jsonLoaded = true;
}

void SeriesIndex::record(const std::string& path, const std::string& name, const std::string& index) {
  if (path.empty()) return;
  auto it = byPath.find(path);
  if (it != byPath.end() && it->second.name == name && it->second.index == index) {
    return;  // no-op: already recorded with same values, skip disk write.
  }
  SeriesEntry e;
  e.path = path;
  e.name = name;
  e.index = index;
  byPath[path] = std::move(e);
  saveToFile();
}

bool SeriesIndex::hasBeenChecked(const std::string& path) const {
  return byPath.find(path) != byPath.end();
}

const SeriesEntry* SeriesIndex::find(const std::string& path) const {
  auto it = byPath.find(path);
  if (it == byPath.end()) return nullptr;
  return &it->second;
}

void SeriesIndex::forgetPath(const std::string& path) {
  if (byPath.erase(path) > 0) {
    saveToFile();
  }
}

std::string SeriesIndex::seriesKey(const std::string& rawName) {
  // lowercase + collapse internal whitespace + trim. Matches aalu's
  // normalisation so a series called "The Foundation" survives
  // capitalization or stray-double-space drift across books.
  std::string out;
  out.reserve(rawName.size());
  bool prevSpace = true;  // start-of-string trims leading space
  for (char raw : rawName) {
    const auto ch = static_cast<unsigned char>(raw);
    if (std::isspace(ch)) {
      if (!prevSpace) {
        out.push_back(' ');
        prevSpace = true;
      }
    } else {
      out.push_back(static_cast<char>(std::tolower(ch)));
      prevSpace = false;
    }
  }
  // Trim a single trailing space if present (the loop adds one when a
  // run of whitespace is followed by end-of-string).
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool SeriesIndex::indexLess(const std::string& a, const std::string& b) {
  // Parse leading numeric prefix. Allows "1", "1.5", "10", and falls
  // back to lexicographic for unparseable strings like "VII".
  auto parsePrefix = [](const std::string& s, double& outVal) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;  // didn't consume any chars
    outVal = v;
    return true;
  };
  double va = 0.0;
  double vb = 0.0;
  const bool pa = parsePrefix(a, va);
  const bool pb = parsePrefix(b, vb);
  if (pa && pb) {
    if (va != vb) return va < vb;
    return a < b;  // tie-break by raw string (handles "1" vs "1.0")
  }
  if (pa != pb) return pa;  // numeric-prefix entries sort before unparseable
  return a < b;
}

bool SeriesIndex::loadFromFile() {
  if (!Storage.exists(SERIES_INDEX_FILE)) return false;
  String json = Storage.readFile(SERIES_INDEX_FILE);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto err = deserializeJson(doc, json.c_str());
  if (err) {
    LOG_ERR("SER", "series_index.json parse error: %s", err.c_str());
    return false;
  }
  byPath.clear();
  JsonArrayConst arr = doc["books"];
  if (!arr.isNull()) {
    byPath.reserve(arr.size());
    for (JsonObjectConst entry : arr) {
      const std::string path = entry["path"] | std::string("");
      if (path.empty()) continue;
      SeriesEntry e;
      e.path = path;
      e.name = entry["name"] | std::string("");
      e.index = entry["index"] | std::string("");
      byPath[path] = std::move(e);
    }
  }
  LOG_DBG("SER", "Loaded series index with %zu entries", byPath.size());
  return true;
}

bool SeriesIndex::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["version"] = SERIES_INDEX_VERSION;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& kv : byPath) {
    JsonObject entry = arr.add<JsonObject>();
    entry["path"] = kv.second.path;
    entry["name"] = kv.second.name;
    entry["index"] = kv.second.index;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(SERIES_INDEX_FILE, json);
}
