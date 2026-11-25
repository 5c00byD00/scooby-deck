// Scooby Deck v3 — Meshtastic favourite nodes (our own list; the v3 Meshtastic stack has
// no NodeDB is_favorite). MeshCore favourites are the contact flag bit 0 (ContactInfo.flags).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#pragma once
#include <stdint.h>

namespace sd {
bool    mtFav(uint32_t num);
void    mtFavSet(uint32_t num, bool on);   // saved at once (/dm "mtfav3")
void    mtFavClearAll();
uint8_t mtFavCount();
}
