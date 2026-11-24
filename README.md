# Scooby Deck

Custom firmware for the **LilyGO T-Deck Plus** (also runs on the T-Deck) that sends and
receives on **both MeshCore and Meshtastic with one SX1262 radio**. It is a fork of
[WadaMesh](README.wadamesh.md) (ALLFATHER-BV), with the dual-mesh time-slicing from
SquachWatch-CYD (skizzophrenic).

> **Status:** early firmware. **v1.2.0-beta** is the current beta (pre-release); **v0.1.1** is
> the latest stable release. Test builds are published as **pre-releases** (`vX.Y.Z-alpha.N`,
> `vX.Y.Z-beta`), and the rolling
> **`nightly`** pre-release always has the newest `main` build at a fixed URL:
> https://github.com/5c00byD00/scooby-deck/releases/download/nightly/scooby-deck-tdeckplus-nightly-merged.bin
> Pre-release and nightly builds may not have been tested on hardware. Please report what the
> dashboard's `[MTRX]` counters show (see below).

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
  - **Meshtastic page (v1.2.0):** a *Last Heard* box (the 3 newest nodes; tap for the full list)
    above a *Channels* list with new-message counters, with **[ DMs ]** and **[ EDIT ]** pinned
    at the bottom.
    - *Last Heard:* tap a node for its details (names, ID, hardware, role, SNR/RSSI, hops,
      position, battery) and **[ DM ]**; hold to remove it from the list.
    - *DMs:* your conversations with the newest message; tap to open, hold to delete.
    - *Edit:* the 8 channel slots. Tap to select; **[ ADD ]** takes a name + PSK; **[ REMOVE ]**
      removes the selected slot (not the primary, slot 0); hold a slot to edit it.
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
- **Settings survive a reflash (v1.2.0):** the Meshtastic key, names, channels, mode and radio
  settings are kept in files on the internal SPIFFS partition (see *Where settings live* below).

## Flashing
> **v2 nightly (bleeding edge):** every push to the `v2` branch is built automatically into the
> [`nightly-v2` pre-release](https://github.com/5c00byD00/scooby-deck/releases/tag/nightly-v2)
> (`scooby-deck-tdeckplus-nightly-v2-merged.bin` at 0x0, `-app.bin` at 0x10000). Untested; for daily use take the Latest release.
> v2 is **2.0.0-dev**: a slim rebuild (HOME dashboard with CORE / SETUP / TASTIC, LongFast by default); see [SCOOBY_DECK.md](SCOOBY_DECK.md#v200-dev-branch-v2-nightly-v2-basic-v2-not-yet-tested-on-hardware) and `docs/screenshots/v2/`.

Each release has two files:

| File | Flash at | Use it for |
|---|---|---|
| `scooby-deck-tdeckplus-vX.Y.Z-merged.bin` | **0x0** | First install, or recovering a board. Bootloader + partition table + app. Erases NVS (0x9000-0xE000). |
| `scooby-deck-tdeckplus-vX.Y.Z-app.bin` | **0x10000** | Updating a board that already runs Scooby Deck / WadaMesh with this partition table. Only the app slot is written; NVS and SPIFFS are untouched. |

Neither file reaches the SPIFFS partition at 0xC90000, so your settings, contacts and
history are kept either way (from v1.2.0 on, the Meshtastic settings too).

- **ESP Terminator / any web ESP flasher:** connect the T-Deck over USB-C. If it isn't
  detected, hold the trackball while pressing reset to enter download mode. Pick the
  `.bin`, set the address to **0x0**, and flash.
- **Arduino app (esptool):** with the board on USB:
  ```
  esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 scooby-deck-tdeckplus-v1.2.0-beta-merged.bin
  # or, to update only the app:
  esptool.py --chip esp32s3 --baud 921600 write_flash 0x10000 scooby-deck-tdeckplus-v1.2.0-beta-app.bin
  ```
- The app-only file boots from slot app0. A board that was updated over the air into app1 needs
  the merged image (or an OTA) instead.
- Check the download against the SHA-256 in the release notes.
- Press reset after flashing.

Build it yourself (PlatformIO):
```
pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin
# -> .pio/build/LilyGo_TDeck_companion_radio_touch/firmware-merged.bin  (0x0)
# -> .pio/build/LilyGo_TDeck_companion_radio_touch/firmware.bin         (0x10000)
```

## Where settings live
| What | Where | Survives a 0x0 flash? |
|---|---|---|
| Meshtastic settings: PKI key (`mt_priv`), long/short name, 8 channels, mesh mode, shared SF, preset/radio, TDM | SPIFFS `/dm_cfg.bin` (v1.2.0+) | yes |
| Learned Meshtastic names and peer public keys | SPIFFS `/dm_names.bin`, `/dm_keys.bin` (v1.2.0+) | yes |
| Terminal theme switch | SPIFFS `/dm_term.bin` (v1.2.0+) | yes |
| MeshCore identity (node key) | SPIFFS identity file (NVS `mc_id` keeps a spare copy) | yes |
| MeshCore prefs, contacts, channels, chat history | SPIFFS `/new_prefs`, `/contacts3`, `/channels2`, ... | yes |
| WadaMesh touch settings, MQTT, keyboard rotation | SPIFFS `/prefs/*.kv` | yes |
| Boot counters, boot-logo rotation, "store on SD" mirror | NVS (`mcboot`, `touch`) | no, rebuilt at boot |

Each `/dm_*.bin` is written to a `.tmp` first, checked (CRC-32), then renamed over the old
file, so a reset mid-save keeps the previous copy. The first boot of v1.2.0 copies anything
still in the old NVS namespace `dualmesh` into these files.

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
| `SELF` | Our own packets relayed back by a neighbour (dropped; proves the link both ways) |
| `ACKTX` / `ACKRX` | Routing ACKs we sent for DMs to us / ACKs for our DMs plus implicit ACKs (our channel messages heard relayed) |
| `MT-WIN` / `[TDM ]` | Dual mode: Meshtastic RX windows opened, and the MC / MT window lengths |
| `CADMISS` | Only in the old CAD-look mode: a CAD hit with no preamble after it (noise) |
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

## Meshtastic role and delivery status
Scooby Deck is a **CLIENT_MUTE** Meshtastic node: it never rebroadcasts.
- **DMs** get a Routing ACK, so the sender sees them delivered.
- **Channel messages** are broadcasts. A Meshtastic sender only marks a broadcast delivered when it hears
  another node relay it (an implicit ACK). With no other relaying node nearby, the sender shows
  *"failed to deliver to mesh"* even though the T-Deck received the message. This is normal for a
  mute client, the same as on stock firmware.

## Changes
**v1.2.0-beta:** settings on SPIFFS (survive reflashing), new Meshtastic page (Last Heard,
Channels, DMs, Edit), app-only update file. See [SCOOBY_DECK.md](SCOOBY_DECK.md) for the source layout and the per-version change notes.

## License and credits
GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE). Under the GPL, anyone who
gets the firmware is entitled to this source.

- **WadaMesh**: ALLFATHER-BV and the wadamesh contributors, GPL-3.0 (base firmware and UI)
- **SquachWatch-CYD**: skizzophrenic, GPL-3.0 (dual-mesh scheduler, Meshtastic codec, crypto, DMs)
- **MeshCore**: Scott Powell / ripplebiz, MIT
- **Meshtastic**: Meshtastic LLC and contributors, GPL-3.0 (protocol)

No warranty. Check your local radio rules before transmitting.
