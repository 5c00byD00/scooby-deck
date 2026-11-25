// Scooby Deck v3 — Meshtastic favourite nodes (see Favs.h).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <string.h>
#include "Favs.h"
#include "DualMeshStore.h"
#include "DualMeshMt.h"

namespace sd {

static const uint8_t kMax = 32;
static uint32_t s_fav[kMax];
static uint8_t s_n = 0;
static bool s_loaded = false;

static void load() {
  if (s_loaded) return;
  s_loaded = true;
  const long n = DualMesh::Store::get("mtfav3", s_fav, sizeof s_fav);
  s_n = n > 0 ? (uint8_t)(n / sizeof(uint32_t)) : 0;
}
static void save() { DualMesh::Store::put("mtfav3", s_fav, s_n * sizeof(uint32_t)); }

bool mtFav(uint32_t num) {
  load();
  for (uint8_t i = 0; i < s_n; i++) if (s_fav[i] == num) return true;
  return false;
}
void mtFavSet(uint32_t num, bool on) {
  load();
  for (uint8_t i = 0; i < s_n; i++) {
    if (s_fav[i] != num) continue;
    if (!on) { s_fav[i] = s_fav[--s_n]; save(); }
    return;
  }
  if (!on || s_n >= kMax) return;
  s_fav[s_n++] = num;
  save();
}
void mtFavClearAll() { load(); s_n = 0; save(); }
uint8_t mtFavCount() { load(); return s_n; }

}  // namespace sd

// The Meshtastic node table asks this before it evicts a node to make room.
namespace DualMesh { namespace Mt {
bool isFavourite(uint32_t num) { return sd::mtFav(num); }
} }
