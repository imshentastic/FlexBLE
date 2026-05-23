# X3 support — readiness audit (branch: feat/x3-support)

Goal for this pass: **boot + usable on the Xteink X3**, with **no X3 on hand to test**.

## TL;DR
CrumBLE v3.0.0 is **already runtime-capable on the X3** — the existing
`crumble-firmware.bin` IS the X3 binary (one firmware, device detected at boot).
No separate build, and the audit below found **no X3 crash/boot risks in our
additions**. The Flow layout was a suspected cosmetic gap; it has now been
**verified correct on the X3 panel in the simulator** (see "Simulator
validation" below) — the shelf/carousel are adaptive and render consistently
with the X4, so no layout code changes were needed. The only firmware changes on
this branch are **test-tooling**; remaining unknowns are **hardware-only**
behaviors (IMU/RTC, real e-ink refresh, heap under BLE) that still need a real
X3.

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

## Simulator validation (this branch)
The simulator now builds the X3 panel, so the Flow layout could be eyeballed at
792×528 without hardware:
- **Two sim envs:** `[env:simulator]` (X4, 800×480) and `[env:simulator_x3]`
  (X3, 792×528) extend a shared `[simulator_base]`. Build either with
  `pio run -e simulator{_x3}` or `scripts/run_simulator_smoke_test.py --device x4|x3`.
- **Headless smoke test** (`SimulatorSmokeTest.cpp`) walks Home → File Browser →
  Recent Books → Settings → Reader Options → Reader Menu → Sleep → Reader (with
  page-turn/menu/options input) → **populated Home**, dumping each screen to a
  24-bit BMP when `CROSSINK_SIMULATOR_DUMP_DIR` is set. Passes on both panels.
- **Multi-cover shelf:** `--shelf` synthesizes 6 cover-bearing EPUBs (reusing the
  JPEG fixture's images) and seeds a `Favorites` collection, so the densest
  layout — the cover grid — renders real thumbnails.

**Result:** every screen fits 792×528 with no clipping; the populated Flow
shelf/carousel renders real covers and is **visually consistent with the X4**.
The grid is genuinely adaptive (`LyraFlowTheme.cpp` derives `visibleCells` from
`rect.width` and centers via `rowStartX`); the X3's extra ~48 px portrait width
is absorbed as margin/peek, not clipping. **No layout code changes were needed.**

## Remaining unknowns (hardware-only; sim can't cover these)
- **Carousel/cover proportions** look correct in the sim, but final "feel" on a
  real X3 e-ink panel (contrast, ghosting, refresh) is unverified.
- **Default theme is LYRA_FLOW.** A fresh X3 boots into Flow; the sim shows it
  renders fine, so defaulting X3 to another theme is now optional, not a fix.
- **Heap under load** (framebuffer ~4 KB larger than X4) during BLE + image pages
  is a runtime-memory question the native sim (1 MB "heap") can't represent.

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
2. If it boots + reads, ship "X3 supported" — the Flow layout is sim-verified at
   792×528, so the earlier "polish WIP" caveat no longer applies.
3. Before merging this branch to main, the sim defaults to X4 (`[env:simulator]`);
   use `--device x3` for X3 dumps. Keep the X4/X3 env split.
