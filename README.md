# Scooby Deck

Custom firmware for the **LilyGO T-Deck Plus** (also runs on the T-Deck) that sends and
receives on **both MeshCore and Meshtastic with one SX1262 radio**. It is a fork of
[WadaMesh](README.wadamesh.md) (ALLFATHER-BV), with the dual-mesh time-slicing from
SquachWatch-CYD (skizzophrenic).

> **Status:** v0.1.1 is an early build. Its Meshtastic RX fix came from code review
> and **hasn't been tested on hardware yet**. Please report what the dashboard's
> `[MTRX]` counters show (see below).

## Features
- **Mesh modes:** Dual (default: MeshCore listens full-time, with short Meshtastic listens),
  MeshCore-only, or Meshtastic-only. Saved, and applied at once.
- **Radio defaults**
  - MeshCore: 910.525 MHz, BW 62.5 kHz, SF7, CR 4/5, sync 0x12.
  - Meshtastic: MediumSlow, slot 48 = 913.875 MHz (US), sync 0x2B, default key (AQ==).
  - Meshtastic presets and custom BW/SF/CR, slot or frequency, sync word, preamble,
    hop limit and TX power are all editable in **Settings > Mesh > Radio & Mesh**.
- **Shared spreading factor:** Off, SF5, SF7 or SF9 on both meshes. Every peer must use the
  same SF, so leave it **Off** for normal networks.
- **Meshtastic client:** the node never rebroadcasts other nodes' packets (client-mute).
  - Up to 8 channels (name + PSK as hex, base64 or a default-key index).
  - Direct messages (PKI), NodeInfo announces, a node list and chats.
  - Sends wait for a clear channel and back off if it's busy.
- **Names:** long name (up to 39 characters) and short name (up to 4). The long name can sync
  to the MeshCore node name.
- **Dashboard** (the home screen at boot):
  - Battery, GPS, unread counts, mode and radio profiles.
  - Heap/PSRAM, CPU, RX/TX counters.
  - The `[MTRX]` Meshtastic receive diagnostics.
- **Getting around:**
  - **B** opens or closes the dashboard and **M** the Meshtastic chats, from any screen.
  - Back (Q / Esc / the status-bar chevron) closes them.
  - App drawer tiles: *Dashboard* and *Mtastic*.
  - **Settings > Scooby Deck** has the channels, names and theme.
- **Retro green terminal theme** across the whole UI, square and flat (can be switched off).

## Flashing
Download `scooby-deck-tdeckplus-vX.Y.Z-merged.bin` from the
[Releases](../../releases) page. It is a **merged image: flash it at offset `0x0`**.

- **ESP Terminator / any web ESP flasher:** connect the T-Deck over USB-C. If it isn't
  detected, hold the trackball while pressing reset to enter download mode. Pick the
  `.bin`, set the address to **0x0**, and flash.
- **Arduino app (esptool):** with the board on USB:
  ```
  esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 scooby-deck-tdeckplus-v0.1.1-merged.bin
  ```
- Check the download against the SHA-256 in the release notes.
- Press reset after flashing.

Build it yourself (PlatformIO):
```
pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin
# -> .pio/build/LilyGo_TDeck_companion_radio_touch/firmware-merged.bin
```

## Test notes (v0.1.1): Shared SF Off, MediumSlow slot 48
Set the T-Deck and the Meshtastic node the same way:

| Setting | Value |
|---|---|
| Region | US |
| LoRa preset | MediumSlow (BW 250, SF10, CR 4/5) |
| Frequency slot | 48 = 913.875 MHz (override frequency 0) |
| Primary channel | name `MediumSlow` or blank, PSK default `AQ==` |
| Shared SF (T-Deck) | **Off** (any other value moves Meshtastic off SF10) |
| Other | TX enabled, no ham mode, IQ not inverted |

On the dashboard, check `[MT ] 913.875 BW250 SF10 MediumSlow` and `MY HASH 0x18`. Then send
from the other node, first in Dual mode, then in Meshtastic-only
(Settings > Mesh > Radio & Mesh > Radio time-sharing).

### `[MTRX]` counter guide
| Counter | Meaning |
|---|---|
| `PRE` / `HDR` | A Meshtastic preamble / a valid header was heard |
| `HDRERR` | Header error |
| `CRC` | Frame received with a CRC error (dropped) |
| `RAW` | Frames passed to the Meshtastic decoder |
| `OK` | Decoded with a channel key (or our key, for DMs) |
| `NOKEY` | No channel matches this channel hash, and no key opened it |
| `BADDEC` | A channel hash matched, but decryption didn't give a valid packet |
| `FALLBK` | Opened by a key whose hash didn't match (name mismatch, same key) |
| `PKI?` | A DM for another node, or the sender's key is unknown |
| `DUP` | Relayed copies of a frame already seen |
| `CADMISS` | Channel-activity hit with no preamble after it (noise) |
| `LAST` | Age, RSSI, SNR and length of the last Meshtastic frame |
| `FROM` / `HASH` / `PORT` | Sender, channel hash and port of the last frame |
| `MY HASH` | Our primary channel hash (MediumSlow + AQ== = 0x18) |
| `REARM` / `LEVEL-IRQ` | RX watchdog re-arms / IRQs caught by level polling |

How to read them:
- `PRE` and `HDR` stay at 0 near a sending node: radio-level problem.
- `CRC` rising: signal or corruption.
- `NOKEY` / `BADDEC` rising: channel name or key mismatch.
- `OK` rising but no chat: UI problem.

The USB serial log (115200 baud) prints one `[mt]` / `[dualmesh]` line per frame.

## Changes
See [SCOOBY_DECK.md](SCOOBY_DECK.md) for the source layout and the per-version change notes.

## License and credits
GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE). Under the GPL, anyone who
gets the firmware is entitled to this source.

- **WadaMesh**: ALLFATHER-BV and the wadamesh contributors, GPL-3.0 (base firmware and UI)
- **SquachWatch-CYD**: skizzophrenic, GPL-3.0 (dual-mesh scheduler, Meshtastic codec, crypto, DMs)
- **MeshCore**: Scott Powell / ripplebiz, MIT
- **Meshtastic**: Meshtastic LLC and contributors, GPL-3.0 (protocol)

No warranty. Check your local radio rules before transmitting.
