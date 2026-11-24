// Scooby Deck v3 — boot screen and HOME (the status screen with CORE | SETUP | TASTIC).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <MyMesh.h>
#include <target.h>
#include "Ui.h"
#include "McStore.h"
#include "ScoobyVersion.h"
#include "DualMeshConfig.h"
#include "DualMeshMt.h"

namespace sd {

// ------------------------------------------------------------------ boot
static uint32_t s_bootAt = 0;
void buildBoot(lv_obj_t* b) {
  s_bootAt = millis();
  lv_obj_t* art = label(b, SCOOBY_BANNER, FONT_S);
  lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_t* v = label(b, SCOOBY_DECK_NAME " v" SCOOBY_DECK_VERSION, FONT_M);
  lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 102);
  lv_obj_t* cr = label(b, SCOOBY_CREDITS, FONT_S, pal().dim);
  lv_obj_align(cr, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_t* l = label(b, "> BOOTING_", FONT_M);
  lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 8, -10);
}
void refreshBoot(uint32_t now) {
  if (now - s_bootAt < 2500) return;
  go(namesSetupNeeded() ? P_NAMES_FIRST : P_HOME);
}

// ------------------------------------------------------------------ home
static lv_obj_t* s_stat = nullptr;

static void coreCb(lv_event_t*)   { go(P_CORE); }
static void setupCb(lv_event_t*)  { go(P_SETUP); }
static void tasticCb(lv_event_t*) { go(P_TASTIC); }

static void bar(char* out, int cells, int pct) {
  int f = (pct * cells + 50) / 100;
  if (f > cells) f = cells;
  out[0] = '[';
  for (int i = 0; i < cells; i++) out[1 + i] = i < f ? '#' : '-';
  out[1 + cells] = ']';
  out[2 + cells] = 0;
}

static const char* onName(uint8_t on) {
  switch (on) {
    case DualMesh::ON_MESHCORE:   return "MESHCORE";
    case DualMesh::ON_MESHTASTIC: return "MESHTASTIC";
    case DualMesh::ON_TX:         return "TX";
    case DualMesh::ON_PAUSED:     return "PAUSED";
    default:                      return "-";
  }
}
static const char* modeShort(uint8_t m) {
  return m == DualMesh::MODE_MESHCORE ? "CORE" : m == DualMesh::MODE_MESHTASTIC ? "TASTIC" : "BOTH";
}

void refreshHome(uint32_t) {
  if (!s_stat) return;
  static char buf[900];
  char b1[16];
  size_t n = 0;
  auto add = [&](const char* fmt, auto... a) {
    if (n < sizeof buf) n += snprintf(buf + n, sizeof buf - n, fmt, a...);
  };
  const uint16_t mv = battMv();
  const uint8_t pct = battPct();
  bar(b1, 10, pct);
  add("[VER  ] v%s\n", SCOOBY_DECK_VERSION);
  add("[BATT ] %3u%%  %u.%02uV %s\n", pct, mv / 1000, (mv % 1000) / 10, b1);
  add("[MSGS ] CORE %u   TASTIC %u  (unread)\n", mc::unreadTotal(), DualMesh::Mt::unreadTotal());
  const DualMesh::Config& c = DualMesh::cfg();
  add("[LISTN] %-6s  RADIO ON %s\n", modeShort(c.mode), onName(radio_driver.onProfile()));
  const DualMesh::RadioProfile mp = radio_driver.mcProfile();
  add("[CORE ] %.3f BW%g SF%u CR%u\n", (double)mp.freq, (double)mp.bw, mp.sf, mp.cr);
  add("[TASTC] %.3f BW%g SF%u %s\n", (double)DualMesh::mtFreqMHz(), (double)DualMesh::mtBw(),
      DualMesh::mtSf(), DualMesh::mtPresetName());
  add("[NODES] CORE %d   TASTIC %u\n", the_mesh.getNumContacts(), DualMesh::Mt::nodeCount());
  add("[HEAP ] %uK FREE  MIN %uK\n", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024));
  const uint32_t ps = ESP.getPsramSize(), pf = ESP.getFreePsram();
  if (ps) { bar(b1, 8, (int)((ps - pf) * 100ull / ps)); add("[PSRAM] %uK/%uK %s\n", (unsigned)((ps - pf) / 1024), (unsigned)(ps / 1024), b1); }
  const DualMesh::RadioStats st = radio_driver.stats();
  add("[RX   ] MC %lu  MT %lu\n", (unsigned long)st.mcRx, (unsigned long)st.mtRx);
  add("[TX   ] MC %lu  MT %lu  FAIL %lu\n", (unsigned long)st.mcTx, (unsigned long)st.mtTx,
      (unsigned long)(st.mcTxFail + st.mtTxFail));
  if (c.mode == DualMesh::MODE_DUAL)
    add("[TDM  ] MC %lums / MT %lums\n", (unsigned long)radio_driver.homeMs(), (unsigned long)radio_driver.mtWindowMs());
  add("[NOISE] %d dBm  CRC %lu\n", (int)st.noise, (unsigned long)(st.mcCrcErr + st.mtCrcErr));
  const DualMesh::Mt::RxDiag dg = DualMesh::Mt::rxDiag();
  add("[MTRX ] PRE %lu HDR %lu DEC %lu\n", (unsigned long)st.mtPre, (unsigned long)st.mtHdr, (unsigned long)dg.decoded);
  add("[ACK  ] CORE %lu  TASTIC %lu", (unsigned long)mc::ackRx(), (unsigned long)dg.ackRx);
  lv_label_set_text(s_stat, buf);
  char h[48];
  snprintf(h, sizeof h, "> SCOOBY DECK [%s]", modeShort(c.mode));
  setHeaderText(h);
}

void buildHome(lv_obj_t* b) {
  lv_obj_t* bx = box(b, 0, 0, 320, 180);
  s_stat = label(bx, "", FONT_S);
  lv_obj_set_width(s_stat, 312);
  lv_obj_set_style_text_line_space(s_stat, 2, 0);
  lv_obj_t* core = button(b, "[ CORE ]", 0, 184, 104, 34, coreCb);
  button(b, "[ SETUP ]", 108, 184, 104, 34, setupCb);
  button(b, "[ TASTIC ]", 216, 184, 104, 34, tasticCb);
  lv_group_focus_obj(core);
  refreshHome(millis());
}

}  // namespace sd
