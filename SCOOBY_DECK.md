# Scooby Deck (LilyGO T-Deck / T-Deck Plus)

WadaMesh fork that speaks **MeshCore and Meshtastic on one SX1262** by time-slicing
the radio (dual-mesh scheduler ported from SquachWatch-CYD's T-Deck BOTH mode).

Build: `pio run -e LilyGo_TDeck_companion_radio_touch -t mergebin`
→ `.pio/build/LilyGo_TDeck_companion_radio_touch/firmware-merged.bin` (flash at 0x0).

## What's added (all behind `-D SCOOBY_DECK`)
- `src/dualmesh/DualMeshRadioWrapper.*` — WRAPPER_CLASS for MeshCore. A core-0 task owns
  the chip: MeshCore home RX + Meshtastic CAD peeks (Dual), or one profile only.
- `src/dualmesh/DualMeshConfig.*` — NVS "dualmesh": mode, Meshtastic preset/slot/freq/BW/SF/CR/
  sync/preamble/hops/power, shared SF (off/5/7/9), TDM, names, 8 channels, PKI key.
- `src/dualmesh/DualMeshMt.*` + `lora_*` — Meshtastic client (client-mute): decode, AES-128/256
  channels, PKI DMs (X25519 + AES-CCM), NodeInfo, node list, message store.
- `src/ui-touch/ScoobyDeckUi.inc` — Settings > Scooby Deck, Dashboard (boot home),
  Meshtastic chats, terminal palette. `scripts/build/patch_lvgl_scooby_flat.py` — square/flat drawing.

## Credits / licence (GPL-3.0-or-later)
- WadaMesh — ALLFATHER-BV, GPL-3.0 (base firmware and UI)
- SquachWatch-CYD — skizzophrenic, GPL-3.0 (dual-mesh scheduler, Meshtastic codec, crypto, DMs)
- MeshCore — Scott Powell / ripplebiz, MIT
- Meshtastic — Meshtastic LLC and contributors, GPL-3.0 (protocol)
