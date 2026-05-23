# X3 support — readiness audit (branch: feat/x3-support)

Goal for this pass: **boot + usable on the Xteink X3**, with **no X3 on hand to test**.

## TL;DR
CrumBLE v3.0.0 is **already runtime-capable on the X3** — the existing
`crumble-firmware.bin` IS the X3 binary (one firmware, device detected at boot).
No separate build, and the audit below found **no X3 crash/boot risks in our
additions**. The remaining gaps are **cosmetic** (Flow theme proportions tuned
for the X4) and need a real X3 to tune. So this branch is an audit + validation
plan, not code changes — making speculative layout edits without an X3 would
risk the tested X4 build for no verifiable gain.

## What's already handled (CrossInk 1.3 base + our dynamic sizing)
- **Device detection:** `HalGPIO::detectDeviceTypeWithFingerprint()` at boot →
  `gpio.deviceIsX3()`. Single binary, runtime-selected.
- **Display:** SDK `setDisplayX3()` (792×528 vs X4 800×480), X3 refresh handling,
  X3 initial-full-sync. Framebuffers are `frameBuffer0/1[MAX_BUFFER_SIZE]`
  (52272 = max of both), so the X3's larger buffer fits — no overflow.
- **Flash/partition:** both target 16 MB; the v3.0.0 7.5 MB app slots fit the X3.
- **Hardware:** X3 IMU (QMI8658 tilt-turn) + DS3231 RTC clock are gated on
  `deviceIsX3()` and handled by the 1.3 base.
- **CrossInk themes** (Classic/Lyra) already adapt to X3 layout.

## Audit of OUR additions (no X3-breaking assumptions found)
- No hardcoded `48000` / framebuffer sizes anywhere in `src/` or our libs.
- `storeCoverBuffer()` allocates via `renderer.getBufferSize()` (dynamic) and
  `saveSleepFrameBuffer()`/`loadSleepFrameBuffer()` use the live buffer size.
- `LyraFlowTheme` lays out from `renderer.getScreenWidth()/getScreenHeight()`.
- BLE, Collections, Series, sleep-cycling, reader: all device-agnostic.

## Known COSMETIC gaps (out of scope here; need a real X3 to tune)
- **Flow shelf grid** (`LyraFlowTheme.cpp` ~448): fixed `kCellWidth=100`,
  `kCellGap=16`, `kSidePad=16` sized for "480 exactly" → on X3's 528-wide
  portrait the row sits with ~48 px extra (centered or right-margin depending
  on positioning). Renders + navigable, just not full-width.
- **Carousel cover sizes** (`kCenterThumbW/H`, side covers): X4-tuned; fine on
  X3 (smaller than screen) but not proportioned for it.
- **Default theme is LYRA_FLOW** (X4-tuned). A fresh X3 boots into Flow. Option:
  default X3 to an X3-aware theme (Lyra) — small, low-risk behavior change for
  X3 only, but it's a product call (and still unverified without hardware).

## Validation checklist (run on a real X3 before claiming support)
1. Boots; serial shows `Hardware detect: X3`; no panic.
2. Reader renders text correctly at 792×528 (the core function).
3. Flow home: carousel + shelf navigable; covers generate (Recent + Collections).
4. BLE pair + page-turn; Collections create/add/remove; series stacking.
5. Sleep-screen cycle + PNG; QuickResume.
6. X3-specific: IMU tilt-turn, RTC clock in status bar, recovery-mode key combo.
7. Watch heap on X3 (framebuffer is ~4 KB larger than X4) under BLE + image pages.

## Recommended next steps
1. Get the existing `crumble-firmware.bin` onto a real X3 and run the checklist.
2. If it boots + reads, ship "X3 supported (Flow layout is X4-tuned; polish WIP)".
3. Then tune the Flow shelf grid / cover sizes for 792×528 (the "full polish" pass).
