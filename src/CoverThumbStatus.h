#pragma once
#include <string>

// CrumBLE: persistent "do not retry" marker for cover-thumbnail generation
// that has failed on a given book. Some EPUBs ship a cover image we can't
// decode on-device (PNG with palette quirks, malformed JPEGs, OPF that
// points at a missing path, etc.). Without this gate, every visit to the
// home carousel or a collection grid re-attempts generation, briefly
// flashes a Loading popup, fails again, and finally renders the
// placeholder — turning what should be a snappy scroll into a stutter.
//
// The marker is a zero-byte file at <book cachePath>/thumb_failed.marker
// so it persists across reboots (lives on SD card alongside the book's
// other cache state). It's cleared whenever a generation attempt
// succeeds (e.g. a future build improves decode, or the user replaces
// the book file with a fixed copy and we walk the new cachePath).
namespace CoverThumbStatus {

// True when we've previously failed to generate a cover thumbnail for
// the book at `bookPath`. Call before any generateThumbBmp*() to skip
// the doomed-to-fail attempt and fall through to the placeholder render.
bool isMarkedFailed(const std::string& bookPath);

// Persist the "give up" marker so future visits stop retrying. Call
// after a generateThumbBmp*() returns false.
void markFailed(const std::string& bookPath);

// Drop the marker (idempotent). Call after a successful regeneration in
// case the book had been previously marked but is now decodable (build
// improvement, or user replaced the file). Cheap no-op when the marker
// doesn't exist.
void clearFailed(const std::string& bookPath);

}  // namespace CoverThumbStatus
