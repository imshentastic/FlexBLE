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

## Recently fixed
- **Book-1 false-positive (FIXED, `2ee659dc`).** The "Bluetooth turned off" alert fired right after BT connected on book 1 (which reads fine). Cause: NimBLE's connect handshake briefly spikes heap pressure, starving a single render. Fix: a 4 s post-connect grace window (`btEnabledAtMs`) — starvation is ignored until the transient passes; past the window, persistent starvation still drops BT, so book 2 is unaffected.
- **First-index cover cap (`9f730af1`).** On `wasFreshFirstBoot()`, cover generation is capped at 24 so a large library can't OOM during first-time setup (SD walk + fragmented heap). Capped books render blank and generate on the next boot. Small libraries unaffected.

## Remaining R9 validation
- Confirm on device: book 1 no longer false-triggers; book 2 still auto-drops on its genuinely-heavy pages; first boot on a large library stays stable.
- **SD font selection** — still untested.
- Then consider cutting v3.0.0.

## First-use crash reports — analysis
Series detection is OFF by default and gated; `LibraryIndex::rescan` stores file paths only and saves via the streaming writer (the 2.1.3 fix, already on this branch). There is no automatic Series/metadata scan on first use — the known boot-loop was the LibraryIndex OOM, already fixed. The cover cap above is the added belt-and-suspenders for first-time setup.
