<div align="center">

# CrumBLE

<img src="./docs/images/crumble/01-boot.jpg" alt="CrumBLE boot screen" width="280"/>

**A personal fork of [CrossInk Carousel](https://github.com/chintanvajariya/CrossInk-Carousel) for the Xteink X4 — adds a Bluetooth page-turner, a Collections system, an on-demand sleep-screen cycler, and a long-press quick-settings drawer inside books.**

</div>

> **Only tested on the Xteink X4.** Not yet compatible with the X3. **Back up your device before flashing.** See [Install firmware](#install-firmware) below.

---

## What CrumBLE adds

CrumBLE sits on top of CrossInk Carousel's feature set — see the [CrossInk Carousel README](https://github.com/chintanvajariya/CrossInk-Carousel#whats-different-from-crossink) and [CrossInk's docs](https://github.com/uxjulia/CrossInk) for those features. The sections below cover what's distinct to this fork.

### Bluetooth remote page-turner

Pair a BT HID remote (e.g. an [IINE GameBrick](https://www.amazon.com/dp/B0CK4DNQM4)) and use it as a wireless page-turner. From inside a book, open the reader menu → **Bluetooth** to pair. The remote's buttons inject as virtual page-turn keys with auto-reconnect on later sessions; BLE auto-disables when you exit the book to keep heap pressure off the parser.

A **BT Quick Connect** action lives in the [Global Book Settings drawer](#global-book-settings-drawer) for one-step re-connect to your last bonded remote without re-navigating the menu tree.

<p align="center">
  <img src="./docs/images/crumble/02-bt-pairing.jpg" alt="Bluetooth pairing UI" width="280"/>
</p>

### Collections

A full collections system with virtual + user-defined collections, all swipeable from the home shelf:

- **Virtual collections** computed lazily: **All Books**, **Favorites**, **Recent**, **Currently Reading**, **Finished**
- **User collections** you create and rename freely
- **Long-press Confirm** on a book → toggle membership in any collection
- **Long-press shelf header** → rename, delete, sort, add/remove books in batch
- **Add/Remove Books** multi-select picker so you can curate a whole collection in one pass
- **Per-collection sort** (A–Z / Z–A / Author / Date Added), persisted in `collections.json`
- Optional **series collapse** that folds same-series books into one spine glyph on the shelf

<p align="center">
  <img src="./docs/images/crumble/03-collections-shelf.jpg" alt="Collections shelf with series collapse" width="280"/>
  <img src="./docs/images/crumble/04-add-remove-books.jpg" alt="Add / Remove Books picker" width="280"/>
</p>

### On-demand sleep-screen cycling

A new display setting — **Tap Power While Asleep to Cycle** — lets you flip through your `/.sleep` images without fully waking the device. A brief power-button tap picks a fresh random image and re-enters deep sleep. Off by default (each cycle costs a boot + e-ink half-refresh worth of battery); pinned sleep images are skipped in cycle mode.

`.png` sleep images (with transparency) are also supported in **Custom** mode now, not just Page Overlay. Transparent regions compose over a snapshot of the last reader page, so a translucent PNG sleep screen reveals the book underneath.

<p align="center">
  <img src="./docs/images/crumble/05-sleep-cycle.jpg" alt="Sleep screen cycling" width="280"/>
</p>

### Global Book Settings drawer

Long-press the menu button inside a book to pop up a bottom-drawer quick-settings panel. Every reader setting — font, size, hyphenation, bionic, line spacing, paragraph alignment, image rendering — is one tap away with e-ink fast refresh so toggles feel snappy. Closing the drawer re-flows the page only if a setting actually changed; a no-op visit skips the re-layout.

Architecture adapted from [inx by Dave Allie](https://github.com/obijuankenobiii/inx) (MIT).

<p align="center">
  <img src="./docs/images/crumble/06-book-settings-drawer.jpg" alt="Global Book Settings drawer" width="280"/>
</p>

---

## Other improvements

- **Reading time accuracy** — deep-sleep commit path flushes the active session so power-off never loses minutes. The 10-second minimum-session floor was dropped; very short sessions count too. Idempotent re-commit prevents double-counting. Ported from [aalu's reading-stats fix](https://github.com/aaludon/crosspoint-reader-aalu) (MIT).
- **Carousel ghosting fixes** on the Lyra Flow theme — max-size cover-slot clear before each paint, thinned selection border (4 px → 2 px), and dropped the always-on inner frame so successive scrolls don't leave outline residue.
- **Remember last position** — switching between the shelf and the bottom menu row restores your previous position on each side instead of jumping to index 0.
- **PackBits-compressed BW backup** for the grayscale AA pass — a single 16–32 KB bounded buffer replaces the chunked 12 × 4 KB lazy allocation, dropping the fragmentation pressure that made grayscale fail when BLE was active.
- **Auto-retry on chapter-layout abort** — if the parser trips the low-heap floor with BLE consuming its ~58 KB share, CrumBLE silently drops BLE, retries the layout with the recovered headroom, and lets the existing auto-reconnect logic re-pair on your next remote press.

For the full changelog, see [CHANGELOG.md](./CHANGELOG.md).

---

## Lineage

```
CrossPoint Reader  →  CrossInk (uxjulia)  →  CrossInk Carousel (chintanvajariya)  →  CrumBLE
```

- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** — the further-upstream foundation. Most reader features (fonts, BookSettings, sleep screens, web UI) come from here.
- **[CrossInk](https://github.com/uxjulia/CrossInk)** — uxjulia's fork. UI polish, localization, additional reader fonts.
- **[CrossInk Carousel](https://github.com/chintanvajariya/CrossInk-Carousel)** — chintanvajariya's fork. Adds the Flow theme (3D book carousel), 3×3 Recent Books grid, and the multi-book Reading Stats redesign.
- **CrumBLE** — this fork. Adds BLE, Collections, sleep-screen cycling, and the in-book quick-settings drawer.

CrumBLE is named after — and themed around — the chocolate-chip cookie boot logo. The "BLE" remained from the previous fork name (FlexBLE) because that's still the headline feature.

---

## Install firmware

1. Download the `crumble-firmware-*.bin` for your variant from the [Releases](https://github.com/imshentastic/CrumBLE/releases) page (`tiny` = standard build with BLE; `xlarge` = no BLE, larger fonts; `no_emoji` = all font sizes, no emoji rendering).
2. Connect your Xteink X4 via USB-C and wake / unlock the device.
3. Go to https://crosspointreader.com/#flash-tools, select your device, choose **Custom .bin**, pick the file you downloaded, and click **Flash**.

To revert to official Xteink firmware, flash the latest stock build from the same page.

---

## USB-locked devices

Some Xteink units sold through third-party stores (e.g. AliExpress) ship with USB flashing locked from the factory. If your device is locked, you'll need the **Xteink Unlocker** at https://crosspointreader.com/#unlock-tool before you can flash CrumBLE.

**You do not need the unlocker if you bought directly from xteink.com** — those units aren't locked.

**Critical warning:** The unlocker officially supports only CrossPoint and CrossInk firmwares. Flashing a non-supported firmware on a USB-locked device can permanently brick it or leave it stuck on that firmware with no recovery path. **CrumBLE does support OTA**, but verify the boot logo appears after first flash before assuming you have an out. If in doubt, flash CrossInk first, confirm OTA works, then OTA-upgrade to CrumBLE.

---

## Build from source

```bash
# Tiny build (BLE-capable, default partition)
pio run -e tiny

# Flash to a connected device
pio run -e tiny -t upload

# Simulator (desktop, for UI iteration)
pio run -e simulator
.pio/build/simulator/program
```

See [docs/contributing/getting-started.md](./docs/contributing/getting-started.md) for the full development setup.

---

## License

Inherits the upstream MIT license. Third-party code attribution lives in [CHANGELOG.md](./CHANGELOG.md) per-feature.
