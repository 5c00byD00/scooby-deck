# Scooby Deck v3 (in development)

Our own firmware for the **LilyGO T-Deck Plus** that listens to **MeshCore** and
**Meshtastic** on one radio. v3 is written from scratch on top of:

- **upstream MeshCore `companion-v1.18.0`**, unmodified, as the `external/MeshCore` submodule
  (attached through its `platformio.local.ini` hook, the way Faro does it);
- **our own code**: the Meshtastic stack, the dual-mesh time-division radio
  (`DualMeshRadioWrapper`), the SPIFFS settings store and the terminal UI;
- **Faro's T-Deck drivers** (Apache-2.0): display, keyboard, trackball, touch, backlight, battery.

No WadaMesh UI, no phone BLE link, no Wi-Fi, no audio, no maps, no GPS (for now).

## Where v3 fits

- **v2** (`v2.0.0-beta`, the Latest release) is the **proof of concept**: a stripped-down
  WadaMesh that shows dual-mesh MeshCore + Meshtastic listening works on one radio. It's
  usable today and is maintained for bug fixes.
- **v3** (this branch, `nightly-v3`) is the **real Scooby Deck**: built from the ground up on
  upstream MeshCore, much smaller (~0.9 MB merged image vs ~2.9 MB), and shaped by community
  feedback. It becomes the main release once it's solid.

**Which should I flash?** v2 if you want something that works now
([Latest release](https://github.com/5c00byD00/scooby-deck/releases/latest)); the v3 nightly
if you want to test what's next and send feedback
([`nightly-v3`](https://github.com/5c00byD00/scooby-deck/releases/tag/nightly-v3)).
Either way, flash the **merged bin at 0x0**. Each v3 nightly is a fresh wipe-and-flash.

## Build

```sh
git clone --recursive -b v3 https://github.com/5c00byD00/scooby-deck.git
cd scooby-deck
scripts/link_env.sh
pio run -d external/MeshCore -e scooby_tdeck -t mergebin
# external/MeshCore/.pio/build/scooby_tdeck/firmware-merged.bin  -> erase, then flash at 0x0
```

Every push to `v3` refreshes the rolling **`nightly-v3`** pre-release
(`.github/workflows/nightly-v3.yml`). It is never marked Latest and carries only the merged
image: each nightly is meant as a fresh wipe-and-flash (erase, then write at 0x0).

## Layout

| Path | What |
|---|---|
| `external/MeshCore` | upstream MeshCore (submodule, untouched) |
| `envs/scooby.ini` | our PlatformIO env (symlinked into the submodule) |
| `app/variant` | T-Deck board objects (radio wrapper = `DualMeshRadioWrapper`) |
| `app/hal` | Faro's hardware drivers |
| `app/dualmesh` | Meshtastic stack, dual-mesh radio, settings store |
| `app/ui` | terminal UI (LVGL 9.3, unscii fonts, green on black) |
| `app/fonts` | unscii-16 (8x16) and its 2x digits, made with lv_font_conv |

## Keys

Not typing: **C** = CORE, **T** = TASTIC, **P** = SETUP, **H** = HOME, **B** / Backspace = back,
**Space** = sleep + lock. Trackball: roll to move, click to select, hold = back.
Lock screen: hold the trackball 3 s. Charging screen (SETUP > BATTERY): click the trackball to leave.

## Licence

GPL-3.0 (see `LICENSE`). Borrowed code keeps its own headers; see `NOTICE`.
