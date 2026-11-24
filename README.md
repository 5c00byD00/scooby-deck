# Scooby Deck v3 (in development)

Our own firmware for the **LilyGO T-Deck Plus** that listens to **MeshCore** and
**Meshtastic** on one radio. v3 is written from scratch on top of:

- **upstream MeshCore `companion-v1.18.0`**, unmodified, as the `external/MeshCore` submodule
  (attached through its `platformio.local.ini` hook, the way Faro does it);
- **our own code**: the Meshtastic stack, the dual-mesh time-division radio
  (`DualMeshRadioWrapper`), the SPIFFS settings store and the terminal UI;
- **Faro's T-Deck drivers** (Apache-2.0): display, keyboard, trackball, touch, backlight, battery.

No WadaMesh UI, no phone BLE link, no Wi-Fi, no audio, no maps, no GPS (for now).
The `v2` branch (WadaMesh-based) stays the testable build until v3 catches up.

## Build

```sh
git clone --recursive -b v3 https://github.com/5c00byD00/scooby-deck.git
cd scooby-deck
scripts/link_env.sh
pio run -d external/MeshCore -e scooby_tdeck -t mergebin
# external/MeshCore/.pio/build/scooby_tdeck/firmware-merged.bin  -> flash at 0x0
# external/MeshCore/.pio/build/scooby_tdeck/firmware.bin         -> flash at 0x10000
```

Every push to `v3` refreshes the rolling **`nightly-v3`** pre-release
(`.github/workflows/nightly-v3.yml`). It is never marked Latest.

## Layout

| Path | What |
|---|---|
| `external/MeshCore` | upstream MeshCore (submodule, untouched) |
| `envs/scooby.ini` | our PlatformIO env (symlinked into the submodule) |
| `app/variant` | T-Deck board objects (radio wrapper = `DualMeshRadioWrapper`) |
| `app/hal` | Faro's hardware drivers |
| `app/dualmesh` | Meshtastic stack, dual-mesh radio, settings store |
| `app/ui` | terminal UI (LVGL 9.3, unscii fonts, green on black) |

## Keys

Not typing: **C** = CORE, **T** = TASTIC, **P** = SETUP, **H** = HOME, **B** / Backspace = back,
**Space** = sleep + lock. Trackball: roll to move, click to select, hold = back.
Lock screen: hold the trackball 3 s.

## Licence

GPL-3.0 (see `LICENSE`). Borrowed code keeps its own headers; see `NOTICE`.
