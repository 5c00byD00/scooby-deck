# Scooby Deck (LilyGO T-Deck / T-Deck Plus)

WadaMesh fork that speaks **MeshCore and Meshtastic on one SX1262** by time-slicing
the radio (dual-mesh scheduler ported from SquachWatch-CYD's T-Deck BOTH mode).

Build: `pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin`
→ `.pio/build/LilyGo_TDeck_companion_radio_touch/firmware-merged.bin` (flash at 0x0), and
`firmware.bin` (app only, flash at 0x10000).

## What's added (all behind `-D SCOOBY_DECK`)
- `src/dualmesh/DualMeshRadioWrapper.*` — WRAPPER_CLASS for MeshCore. A core-0 task owns
  the chip: MeshCore home RX + Meshtastic CAD peeks (Dual), or one profile only.
- `src/dualmesh/DualMeshStore.*` — atomic CRC-checked blob files on SPIFFS (`/dm_*.bin`), v1.2.0.
- `src/dualmesh/DualMeshConfig.*` — SPIFFS `/dm_cfg.bin` (NVS "dualmesh" before v1.2.0): mode, Meshtastic preset/slot/freq/BW/SF/CR/
  sync/preamble/hops/power, shared SF (off/5/7/9), TDM, names, 8 channels, PKI key.
- `src/dualmesh/DualMeshMt.*` + `lora_*` — Meshtastic client (client-mute): decode, AES-128/256
  channels, PKI DMs (X25519 + AES-CCM), NodeInfo, node list, message store.
- `src/ui-touch/ScoobyDeckUi.inc` — Settings > Scooby Deck, Dashboard (boot home),
  Meshtastic page (home / last heard / node popup / DMs / edit channels / chats), terminal palette. `scripts/build/patch_lvgl_scooby_flat.py` — square/flat drawing.

## Credits / licence (GPL-3.0-or-later)
- WadaMesh — ALLFATHER-BV, GPL-3.0 (base firmware and UI)
- SquachWatch-CYD — skizzophrenic, GPL-3.0 (dual-mesh scheduler, Meshtastic codec, crypto, DMs)
- MeshCore — Scott Powell / ripplebiz, MIT
- Meshtastic — Meshtastic LLC and contributors, GPL-3.0 (protocol)

## v0.1.1 (2026-11-23)

Meshtastic RX and navigation fixes. Found by code review against SquachWatch-CYD
and RadioLib 7.8.1; built, not yet bench-tested on hardware.

Radio (src/dualmesh/DualMeshRadioWrapper.cpp):
- IRQs are serviced on the DIO1 level, polled every 4 ms, not only on its rising
  edge. v0.1.0 could leave a latched flag holding DIO1 high with no new edge, so
  the receiver went deaf (for good in Meshtastic-only / MeshCore-only).
- The Meshtastic look is a CAD that exits straight into RX (CAD_GOTO_RX, with the
  RX IRQs routed to DIO1), as in SquachWatch's homeAndPeek(). v0.1.0 went
  CAD -> standby -> SPI -> startReceive and lost preamble on every hit.
- A silent CAD hit is dropped after 6 symbols + 6 ms (SquachWatch: 4 + 6).
- Partial events clear exactly the flags read; a stuck frame is re-armed when its
  header deadline passes; single-profile modes re-arm RX after 20 s of silence.
- Image calibration only runs on a real band change (not after every config
  change / AGC reset).

Decode (src/dualmesh/lora_meshtastic.cpp): when no channel hash matches, every
enabled key and then the default key (AQ==) is tried, and a result is accepted
only if the Data starts with the portnum tag and parses cleanly. So a primary
channel named blank/"MediumSlow"/"LongFast" on the default key still decodes.

Diagnostics: the dashboard shows [MTRX] lines with preambles, headers, header
errors, CRC errors, raw frames, decoded, no-key, bad-decrypt, fallback, CAD misses,
last RSSI/SNR/len, last sender/hash/port and our primary hash. The serial log
(115200) prints one line per Meshtastic frame.

UI:
- Settings > Mesh > Radio & Mesh now has the Meshtastic section (mode, shared SF,
  preset/custom BW/SF/CR, slot/frequency, sync, preamble, hops, TX power, TDM)
  under WadaMesh's MeshCore radio cards, with links to the dashboard, the chats and
  Settings > Scooby Deck (channels and names).
- Keys: B opens/closes the dashboard, M opens/closes the Meshtastic chats, from any
  screen (when no text field is being edited and the key isn't bound to navigation).
- The dashboard and the chats are registered in WadaMesh's popup registry, so Back
  (Q / Esc / the status-bar chevron) closes them and tab swipes are blocked under them.
- Terminal theme: the accent colour is bright green, so black text on the accent
  stays readable.

## v0.1.2-alpha.1 (2026-11-23), test build, not yet tested on hardware

Bench result that drove it (v0.1.1, Dual, Heltec 1-2 m away on MediumSlow slot 48):
CAD 388/687 hits, but only PRE 2 / HDR 1 on the Meshtastic profile, and the only
frame decoded was our own NodeInfo relayed back by the Heltec (RSSI -27). So the
radio and decoder work, but the 2-symbol CAD gate (peak SF+16) was the problem:
it fired on noise on more than half the looks and the real preambles almost never
got into RX.

- Dual mode no longer gates Meshtastic on CAD. It alternates two plain RX windows
  (MeshCore 31 ms / Meshtastic 40 ms at MediumSlow, computed from the preambles) and
  lets the SX1262 preamble detector decide; a window that hears a preamble stays for
  the header and frame. Window math: every 65.5 ms Meshtastic preamble gets >= ~28 ms
  (7 symbols) inside an MT window, every MeshCore SF7 preamble >= ~19 ms (9 symbols).
  The old CAD looks are still there: set "TDM look mode" to 2/4/8.
- Our own packets relayed back by a neighbour are dropped and counted (SELF).
- Meshtastic-only mode is continuous RX on the Meshtastic profile and never uses CAD
  to receive (CAD is only the listen-before-talk check before sending).
- Dashboard: `[TDM ] RX WINDOWS MC x ms / MT y ms`, `MT-WIN` count, SELF counter;
  CADMISS only shows in CAD-look mode.

## v0.1.2-alpha.2 (2026-11-24), test build, not yet tested on hardware

Bench (alpha.1, Dual): PRE 9 / HDR 9 / OK 9, DMs both ways, and the Heltec
showed a delivery ACK on DMs. Only channel (broadcast) messages from the Heltec
show "failed to deliver to mesh".

Why: that is Meshtastic's normal result for a broadcast nobody relays. A sender
gets an *implicit* ACK for a broadcast only by hearing another node rebroadcast it
(firmware ReliableRouter::shouldFilterReceived). Scooby Deck is CLIENT_MUTE:
FloodingRouter::isRebroadcaster() is false for CLIENT_MUTE, and ReliableRouter
only sends explicit ACKs for want_ack packets addressed to us (DMs), never for
broadcasts. So with no other relaying node nearby, channel messages show
"failed to deliver" on the sender even though the T-Deck received them.
That matches stock firmware for a mute client; it isn't a fault.

- Role: NodeInfo advertises CLIENT_MUTE (User.role = 1), as before. No rebroadcast.
- DM ACKs: unchanged (they already work).
- Dashboard: `[MTRX] ACKTX` (Routing ACK/NAKs we sent for DMs to us) and `ACKRX`
  (ACKs for our DMs, plus implicit ACKs: our channel messages heard relayed).
- Chats: our channel messages show `[RELAYED]` when a neighbour's rebroadcast was
  heard (Meshtastic's implicit ACK); DMs keep `[ACK]` / `[FAIL]`.

## v1.2.0-beta (2026-11-24), beta, built locally, not yet tested on hardware

### Settings survive reflashing
The merged image covers 0x0-0x3BC630 and fills 0x9000-0xE000 (NVS) with 0xFF, so every
0x0 flash wiped NVS namespace `dualmesh`: the Meshtastic PKI private key (`mt_priv`), names,
channels, mode and radio. The node number comes from the MAC, so it stayed, but peers saw a
new public key and DMs broke until they relearned it.

- New `DualMeshStore`: one file per blob on the SPIFFS partition (0xC90000-0xFF0000, which no
  flash image touches). Format `SDM1 | len | crc32 | payload`. Write `.tmp`, read it back and
  check the CRC, remove the old file, rename. A load with no valid `.bin` uses a valid `.tmp`.
  SPIFFS is mounted with the same call as `main.cpp` (`SPIFFS.begin(false)`, never formats);
  whichever runs first wins.
- `/dm_cfg.bin`: the whole `Config` (mode, preset/BW/SF/CR, slot/freq, sync, preamble, hops,
  power, announce, shared SF, TDM, long/short name, 8 channels, `mt_priv`).
- `/dm_names.bin`, `/dm_keys.bin`: learned Meshtastic names and peer public keys (was NVS
  `names` / `keys`, up to 8 KB each).
- `/dm_term.bin`: the terminal theme switch.
- Load order: file, else the old NVS blob (then written to the file once), else defaults (the
  PKI key is then generated and saved). NVS is written only if SPIFFS refuses a save, and the
  file is retried every 60 s. A file that exists but could not be read at boot is never
  overwritten.
- WadaMesh / MeshCore were already safe: identity, `/new_prefs`, contacts, channels, history
  and the touch settings (`SdNvsPrefs` → `/prefs/*.kv`) all live on SPIFFS. NVS only holds
  copies and counters (`mc_id` spare identity copy, `mcboot` boot counters, `touch` boot-logo
  rotation and the "store on SD" mirror), which are rebuilt from SPIFFS at boot.

### Meshtastic page overhaul (mockups: 320x240, unscii-8)
- Home: one-row status bar (`appPageBeginSlim`), a scrolling column with a scroll bar:
  LAST HEARD box (3 newest nodes; tap opens the list) and CHANNELS (tap opens the chat;
  `(n)` new messages black-on-green, else the stored count dim). [ DMs ] / [ EDIT ] pinned.
- Last Heard: all nodes, `name !id age snr Hn`. Tap: node popup (long/short name, ID, HW,
  role, heard, SNR, RSSI, hops, position, battery, PKI) with [ DM ] / [ CLOSE ]. Hold: confirm
  and remove from the list (it returns when heard again).
- DMs: conversations newest first, with age, unread `(n)` and the last message. Tap opens,
  hold confirms and deletes the conversation.
- Edit: slots 0-7. Tap selects, hold edits name + PSK. [ ADD ] fills the selected empty slot
  or the first free one; [ REMOVE ] removes the selected slot after a confirm; slot 0 can't
  be removed.
- Back (bar chevron / M): popup → list → home → close. DMs opened from Last Heard go back
  there. Lists refresh at most every 2 s, keep their scroll position, and never rebuild under a
  finger or an open popup. Popups sit on the top layer so trackball focus stays inside them.
- Node table now keeps HW model + role (NodeInfo), position (Position) and battery (device
  Telemetry). Messages carry an unread flag, so DMs count per conversation.
