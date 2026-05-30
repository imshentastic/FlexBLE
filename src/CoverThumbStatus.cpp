#include "CoverThumbStatus.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

namespace {

// Mirrors the Epub/Xtc on-disk layout. The cache dir for any book the
// firmware has ever seen is /.crosspoint/<format>_<hash>; the marker
// is a sibling of stats.bin / thumb_*.bmp inside that dir.
constexpr char kCacheDir[] = "/.crosspoint";
// CrumBLE #133 follow-up: suffix bumped to _v2 so markers written by
// earlier builds (which set them on transient heap-OOM failures during
// 16-book sequential gens at 4x4 / 220x320 at 2x2) get silently
// ignored. The old .marker files leak as zero-byte files on SD --
// acceptable; isMarkedFailed only checks the new path. Books that
// genuinely lack a cover get re-marked under _v2 on first attempt
// with this build, then stop retrying as the marker system intends.
constexpr char kMarkerSuffix[] = "/thumb_failed_v2.marker";

std::string markerPathForBook(const std::string& bookPath) {
  if (FsHelpers::hasEpubExtension(bookPath)) {
    return Epub::cachePathForFilePath(bookPath, kCacheDir) + kMarkerSuffix;
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    // Mirrors Xtc(filepath, cacheDir) constructor's path derivation. Kept
    // inline rather than adding Xtc::cachePathForFilePath() to avoid
    // touching the Xtc class for a single call site.
    return std::string(kCacheDir) + "/xtc_" + std::to_string(std::hash<std::string>{}(bookPath)) + kMarkerSuffix;
  }
  // TXT / Markdown have no cover thumbnail concept; the markers system
  // never applies to them. Return empty so exists() short-circuits to
  // false in callers.
  return "";
}

}  // namespace

namespace CoverThumbStatus {

bool isMarkedFailed(const std::string& bookPath) {
  const std::string marker = markerPathForBook(bookPath);
  if (marker.empty()) return false;
  return Storage.exists(marker.c_str());
}

void markFailed(const std::string& bookPath) {
  const std::string marker = markerPathForBook(bookPath);
  if (marker.empty()) return;
  // Make sure the cache dir exists -- a failure that triggers BEFORE any
  // successful cache write would otherwise leave the marker un-creatable.
  // Truncate the suffix to get the parent dir.
  const std::string parent = marker.substr(0, marker.size() - sizeof(kMarkerSuffix) + 1);
  Storage.mkdir(parent.c_str());
  if (!Storage.writeFile(marker.c_str(), String(""))) {
    LOG_ERR("CTS", "Failed to write thumb-failed marker for %s", bookPath.c_str());
    return;
  }
  LOG_INF("CTS", "Marked thumb generation failed for %s", bookPath.c_str());
}

void clearFailed(const std::string& bookPath) {
  const std::string marker = markerPathForBook(bookPath);
  if (marker.empty()) return;
  if (!Storage.exists(marker.c_str())) return;
  Storage.remove(marker.c_str());
}

}  // namespace CoverThumbStatus
