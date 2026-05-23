# CrumBLE-on-CrossInk-1.3 rebase — status / handoff

Branch: `rebase/crumble-on-1.3`  (HEAD `895ff4f2` at pause)
Pre-v1.3 baseline for comparison: `fix/large-library-indexing` (= "v2.1.3", local-only) and `main` (= v2.1.2).

## Where we are
- Full rebase reconciled and **builds clean** for `env:tiny` (Flash ~81%, partition table enlarged to 7.5 MB app slots).
- Boots cleanly on the X4: BLE infra + bonded device restore, streaming LibraryIndex, Flow home, Collections/Series.
- **Books 1 & 3 read great with the BLE remote.** Book 2 (image-heavy) handled (see below).
- Section page-layout cache bumped v36→v37 (one-time regenerate per book; benign).

## Commits on top of the rebase reconciliation
- `95155db8` R5–R8: reconcile final conflict files + copy missing fork sources (tiny builds clean)
- `ff290c6a` fix(font): FontDecompressor OOM guard (turns the picture-book panic into a graceful skip)
- `895ff4f2` feat(reader): **auto-drop Bluetooth for books that can't render with it connected**
  - Detection: real OOM only — `FontDecompressor::consumeOom()` (glyph alloc guard) + `ImageBlock` decode failure → `GfxRenderer::markRenderStarved()`. Reader checks `takeRenderStarved()` after the main page render; if BLE is up + starved → `requestDisableLater()`, one-time per-book alert (`STR_BT_LOWMEM_*`), re-render with full heap (images AND text show).
- Tag `backup/reactive-image-hacks` (`fe553b9d`): abandoned approach (per-page image suppression + heap pre-check). Superseded; kept only for reference.

## Key finding (answering "compare to pre-v1.3")
The reader render path is byte-identical to v2.1.2; `MemoryBudget.h` identical. The v2.1.2 **Images setting (Display/Placeholder/Suppress)** + build-time auto-suppress survived the rebase intact. The earlier regressions were caused by a redundant render-time suppression layer I added that fought the native mechanism — now removed.

## KNOWN BUG to investigate next
**Book 1 false-positive.** The "Bluetooth turned off" alert fired **right after BT connected** on book 1, which renders fine and should be unaffected. Book 2 behaved correctly (worked a few pages, then alert + BT off on the genuinely-too-heavy page).

Hypothesis: right after NimBLE init grabs ~58 KB, the heap is momentarily at its tightest; the first page render can trip the FontDecompressor OOM guard on a single borderline font-group alloc → `markRenderStarved()` → false trigger. We treat a **single** transient glyph skip as "starved."

Fix ideas (next session):
- Debounce: require a *meaningful* failure, not one glyph — e.g. count OOM glyph-skips per render and only drop BLE above a threshold, and/or count image-decode failures separately (an image failure is unambiguous; a single glyph skip is not).
- Grace period: ignore starvation on the first render(s) immediately after BLE connect, or retry the render once before deciding.
- Re-check `maxAlloc` right before deciding, to confirm it's a sustained shortage not a transient spike.

## Remaining R9 validation
- Fix the book-1 false positive (above).
- **SD font selection** — still untested.
- Then consider cutting v3.0.0.
