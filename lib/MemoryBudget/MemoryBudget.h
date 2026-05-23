#pragma once

#include <Arduino.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

namespace MemoryBudget {

struct HeapSnapshot {
  uint32_t freeHeap;
  uint32_t maxAllocHeap;
};

struct HeapRequirement {
  uint32_t minFree;
  uint32_t minMaxAlloc;
};

// CrumBLE: upstream's defaults of 72 KB free / 48 KB max alloc are tuned for
// the BLE-off case. With NimBLE eating ~58 KB of heap once a remote is
// paired, those floors are unreachable, so even text-heavy chapters tripped
// the "Cannot Load chapter" abort whenever they contained any image. The
// relaxed floors mirror our parser layout threshold (32/16 KB) — image
// decode falls back gracefully to "page rendered without images" if a
// specific allocation does fail mid-decode, which is the same UX as
// upstream's pre-check skip.
constexpr uint32_t EPUB_INLINE_IMAGE_MIN_FREE = 56U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_MIN_MAX_ALLOC = 32U * 1024U;
constexpr uint32_t EPUB_INLINE_JPEG_MIN_MAX_ALLOC = 28U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_FREE = 120U * 1024U;
constexpr uint32_t EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_MAX_ALLOC = 80U * 1024U;
constexpr uint32_t OPTIONAL_EPUB_REBUILD_MIN_FREE = 96U * 1024U;
constexpr uint32_t OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC = 48U * 1024U;
constexpr uint32_t IMAGE_DECODER_HEADROOM = 16U * 1024U;

inline HeapSnapshot snapshot() { return {ESP.getFreeHeap(), ESP.getMaxAllocHeap()}; }

inline bool hasHeap(const HeapSnapshot heap, const uint32_t minFree, const uint32_t minMaxAlloc) {
  return heap.freeHeap >= minFree && heap.maxAllocHeap >= minMaxAlloc;
}

inline char asciiLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool endsWithIgnoreCase(const char* value, const char* suffix) {
  if (!value || !suffix) return false;
  const size_t valueLen = strlen(value);
  const size_t suffixLen = strlen(suffix);
  if (suffixLen > valueLen) return false;

  const char* valueTail = value + valueLen - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    if (asciiLower(valueTail[i]) != asciiLower(suffix[i])) return false;
  }
  return true;
}

inline bool isJpegSource(const char* source) {
  return endsWithIgnoreCase(source, ".jpg") || endsWithIgnoreCase(source, ".jpeg");
}

inline HeapRequirement epubInlineImageRequirementForSource(const char* source) {
  return {EPUB_INLINE_IMAGE_MIN_FREE,
          isJpegSource(source) ? EPUB_INLINE_JPEG_MIN_MAX_ALLOC : EPUB_INLINE_IMAGE_MIN_MAX_ALLOC};
}

inline bool shouldReleaseSdFontCachesForEpubInlineImage(const HeapSnapshot heap) {
  return !hasHeap(heap, EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_FREE, EPUB_INLINE_IMAGE_SD_FONT_RELEASE_MIN_MAX_ALLOC);
}

inline bool hasHeapForEpubInlineImage(const char* tag, const char* source) {
  const auto heap = snapshot();
  const auto requirement = epubInlineImageRequirementForSource(source);
  if (hasHeap(heap, requirement.minFree, requirement.minMaxAlloc)) {
    return true;
  }

  LOG_ERR(tag, "Low heap for inline image (%u free, %u max alloc, need %u/%u); suppressing %s", heap.freeHeap,
          heap.maxAllocHeap, requirement.minFree, requirement.minMaxAlloc, source ? source : "");
  return false;
}

inline bool hasHeapForOptionalEpubRebuild(const char* tag, const char* action, const int spineIndex) {
  const auto heap = snapshot();
  if (hasHeap(heap, OPTIONAL_EPUB_REBUILD_MIN_FREE, OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC)) {
    return true;
  }

  LOG_DBG(tag, "Skipping %s for spine %d: low heap (free=%u, maxAlloc=%u, need free>=%u maxAlloc>=%u)", action,
          spineIndex, heap.freeHeap, heap.maxAllocHeap, OPTIONAL_EPUB_REBUILD_MIN_FREE,
          OPTIONAL_EPUB_REBUILD_MIN_MAX_ALLOC);
  return false;
}

// Series-metadata scan (OPF parse + SeriesIndex growth/persist). It runs on
// the home screen (BLE off) and re-runs every render until complete, so an OOM
// mid-scan would crash-loop the home screen — which the user can't escape to
// disable the feature. Callers bail when this returns false; the collection
// just renders without full series collapse instead of crashing.
constexpr uint32_t SERIES_SCAN_MIN_FREE = 48U * 1024U;
constexpr uint32_t SERIES_SCAN_MIN_MAX_ALLOC = 24U * 1024U;

inline bool hasHeapForSeriesScan() { return hasHeap(snapshot(), SERIES_SCAN_MIN_FREE, SERIES_SCAN_MIN_MAX_ALLOC); }

inline bool hasHeapForImageDecoder(const char* tag, const char* decoderName, const uint32_t decoderApproxBytes) {
  const auto heap = snapshot();
  const uint32_t minFree = decoderApproxBytes + IMAGE_DECODER_HEADROOM;
  if (hasHeap(heap, minFree, decoderApproxBytes)) {
    return true;
  }

  LOG_ERR(tag, "Not enough heap for %s decoder (%u free, %u max alloc, need %u/%u)", decoderName, heap.freeHeap,
          heap.maxAllocHeap, minFree, decoderApproxBytes);
  return false;
}

}  // namespace MemoryBudget
