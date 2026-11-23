// ============================================================================
// Scooby Deck UI (included into UITask.cpp; only with -D SCOOBY_DECK).
//   * Settings > Scooby Deck: mesh mode, Meshtastic radio, shared SF, TDM,
//     names, 8 Meshtastic channels, terminal theme, credits.
//   * Dashboard app page (also shown after the boot splash).
//   * Meshtastic chats app page: per-channel group chats, DMs, nodes.
//   * Retro terminal palette (applyTerminalTheme) + flat LVGL drawing.
// GPL-3.0-or-later, like WadaMesh and SquachWatch-CYD which it builds on.
// ============================================================================
#if defined(SCOOBY_DECK)
#include "../dualmesh/DualMeshConfig.h"
#include "../dualmesh/DualMeshMt.h"
#include "../dualmesh/ScoobyVersion.h"
#include <esp_freertos_hooks.h>
#include <Preferences.h>

extern "C" volatile uint8_t lv_scooby_flat;   // patched into lv_draw_rect.c (scripts/build/patch_lvgl_scooby_flat.py)

// ---- terminal look ---------------------------------------------------------
static constexpr uint32_t TERM_GREEN = 0x00FF41, TERM_MID = 0x00B32D, TERM_DIM = 0x00802A,
                          TERM_DARK = 0x003311, TERM_AMBER = 0xFFB000, TERM_RED = 0xFF4040;
static int8_t s_sd_term = -1;
static bool scoobyTermOn() {
  if (s_sd_term < 0) {
    Preferences p;
    s_sd_term = 1;
    if (p.begin("dualmesh", true)) { s_sd_term = p.getUChar("term", 1) ? 1 : 0; p.end(); }
  }
  return s_sd_term == 1;
}
static void scoobySetTerm(bool on) {
  s_sd_term = on ? 1 : 0;
  Preferences p;
  if (p.begin("dualmesh", false)) { p.putUChar("term", on ? 1 : 0); p.end(); }
}
static void applyTerminalTheme() {
  if (!scoobyTermOn()) { lv_scooby_flat = 0; return; }
  s_theme_day = false; s_theme_high_contrast = false;
  s_look_more = false; s_look_rainbow = false;
  s_bg_kind = 0xFF; s_aur_on = false; s_glass_on = false; s_glass_tiles = false;
  COLOR_BG = 0x000000; COLOR_PANEL = 0x000000; COLOR_TEXT = TERM_GREEN; COLOR_SUB = TERM_MID;
  COLOR_SENT_BG = 0x002B0B; COLOR_RECV_BG = 0x000F05; COLOR_MENTION = 0x7CFF9E; COLOR_MENTION_BG = TERM_DARK;
  COLOR_STATUS_OK = 0x00C832; COLOR_STATUS_OK_PRESSED = 0x009926; COLOR_STATUS_WARN = TERM_AMBER;
  COLOR_STATUS_DANGER = 0xD02020; COLOR_STATUS_DANGER_PRESSED = 0x901818;
  COLOR_STATUS_OK_TEXT = TERM_GREEN; COLOR_STATUS_WARN_TEXT = TERM_AMBER; COLOR_STATUS_DANGER_TEXT = TERM_RED;
  COLOR_STATUS_INFO = 0x33FF99;
  COLOR_BORDER = TERM_DIM; COLOR_FIELD = 0x000A03; COLOR_CONTROL = 0x001A08;
  COLOR_CONTROL_DISABLED = 0x000A03; COLOR_CONTROL_PRESSED = TERM_DARK;
  COLOR_ACCENT_SURFACE = 0x002208; COLOR_ACCENT_BORDER = TERM_GREEN;
  COLOR_CHART_GRID = TERM_DARK; COLOR_CHART_TICK = TERM_DIM; COLOR_RAISED = 0x000F05;
  COLOR_SECONDARY_ACTION = 0x004415; COLOR_TRACK = 0x002B0B; COLOR_CHART_BG = 0x000000;
  COLOR_TERTIARY = 0x008F24; COLOR_HAIR = TERM_DARK; COLOR_GLOW = TERM_GREEN;
  COLOR_ON_GLOW = 0x000000; COLOR_ON_ACCENT = 0x000000;
  COLOR_ON_STATUS_OK = 0x000000; COLOR_ON_STATUS_DANGER = 0x000000; COLOR_ON_STATUS_INFO = 0x000000;
  COLOR_CHAT_TEXT = TERM_GREEN; COLOR_CHAT_META = TERM_MID; COLOR_CHAT_LINK = 0x7CFF9E;
  COLOR_CHAT_SENT_BG = 0x002B0B; COLOR_CHAT_RECV_BG = 0x000F05; COLOR_CHAT_MENTION_BG = TERM_DARK;
  for (uint32_t& h : COLOR_HUE) h = TERM_GREEN;
  COLOR_HUE[HUE_RADIO] = TERM_AMBER;
  COLOR_LINK = TERM_MID;
  // Black text sits on the accent (selected rows, active tabs): make the accent
  // a bright green so it reads, never black-on-teal/black-on-dark.
  COLOR_ACCENT = 0x00C832; COLOR_ACCENT_PRESS = 0x008F24;
  lv_scooby_flat = 1;
}

// ---- CPU load from the idle hooks ------------------------------------------
static volatile uint32_t s_sd_idle[2] = {0, 0};
static bool sdIdle0() { s_sd_idle[0]++; return true; }   // once per tick spent idle
static bool sdIdle1() { s_sd_idle[1]++; return true; }
static uint8_t s_sd_cpu = 0;
static uint32_t s_sd_cpu_idle0 = 0, s_sd_cpu_idle1 = 0, s_sd_cpu_tick = 0;
static void scoobyCpuBegin() {
  static bool on = false;
  if (on) return;
  on = true;
  esp_register_freertos_idle_hook_for_cpu(sdIdle0, 0);
  esp_register_freertos_idle_hook_for_cpu(sdIdle1, 1);
  s_sd_cpu_tick = xTaskGetTickCount();
}
static void scoobyCpuSample() {
  const uint32_t now = xTaskGetTickCount();
  const uint32_t dt = now - s_sd_cpu_tick;
  if (dt < 200) return;
  const uint32_t i0 = s_sd_idle[0] - s_sd_cpu_idle0, i1 = s_sd_idle[1] - s_sd_cpu_idle1;
  s_sd_cpu_idle0 = s_sd_idle[0]; s_sd_cpu_idle1 = s_sd_idle[1]; s_sd_cpu_tick = now;
  uint32_t idle = (i0 + i1) * 100u / (2u * dt);
  if (idle > 100) idle = 100;
  s_sd_cpu = (uint8_t)(100 - idle);
}

static void sdBar(char* out, int pct, int width) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  const int fill = (pct * width + 50) / 100;
  out[0] = '[';
  for (int i = 0; i < width; i++) out[1 + i] = i < fill ? '#' : '-';
  out[1 + width] = ']';
  out[2 + width] = 0;
}

static void sdStyleTermLabel(lv_obj_t* l, const lv_font_t* f, uint32_t color) {
  lv_obj_set_style_text_font(l, f, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
}
static uint32_t sdInk()  { return scoobyTermOn() ? TERM_GREEN : COLOR_TEXT; }
static uint32_t sdDim()  { return scoobyTermOn() ? TERM_MID : COLOR_SUB; }

// A flat, square "[ LABEL ]" button.
static lv_obj_t* sdButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* ud, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(scoobyTermOn() ? TERM_DARK : COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, lv_color_hex(sdInk()), LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(b, 0, LV_PART_MAIN);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  sdStyleTermLabel(l, &lv_font_unscii_8, sdInk());
  lv_obj_center(l);
  return b;
}

// ============================================================================
// Dashboard
// ============================================================================
static lv_obj_t*   s_dash_root = nullptr;
static lv_obj_t*   s_dash_body = nullptr;
static lv_timer_t* s_dash_timer = nullptr;
static void closeScoobyDashboard();
static void scoobyOpenMtChats();

static void dashFill() {
  if (!s_dash_body || !lv_obj_is_valid(s_dash_body)) return;
  scoobyCpuSample();
  using namespace DualMesh;
  const Config& c = cfg();
  static char b[1600]; char bar[16];
  char* p = b; char* e = b + sizeof b;
  // battery
#if CAP_BATTERY
  const uint16_t mv = batteryMvSmoothed();
  const int pct = mv ? batteryPercentFromMv(mv) : -1;
#else
  const uint16_t mv = 0; const int pct = -1;
#endif
  sdBar(bar, pct < 0 ? 0 : pct, 10);
  if (pct >= 0) p += snprintf(p, e - p, "[BATT ] %3d%%  %d.%02dV %s\n", pct, mv / 1000, (mv % 1000) / 10, bar);
  else          p += snprintf(p, e - p, "[BATT ] --\n");
  // GPS
  const bool fix = g_lv.task && g_lv.task->getGpsFix();
  const int sats = g_lv.task ? g_lv.task->getGpsSats() : -1;
  if (sats < 0) p += snprintf(p, e - p, "[GPS  ] OFF / NOT PRESENT\n");
  else          p += snprintf(p, e - p, "[GPS  ] %s  SATS %d\n", fix ? "FIX   " : "NO FIX", sats);
  // unread
  const int mc_unread = g_lv.task ? g_lv.task->getUnreadTotal() : 0;
  p += snprintf(p, e - p, "[MSGS ] MESHCORE %-3d MESHTASTIC %d\n", mc_unread, (int)Mt::unreadTotal());
  // mode + profile
  const uint8_t on = radio_driver.onProfile();
  const char* onName = on == ON_MESHCORE ? "MESHCORE" : on == ON_MESHTASTIC ? "MESHTASTIC"
                     : on == ON_TX ? "TX" : on == ON_PAUSED ? "PAUSED" : "IDLE";
  p += snprintf(p, e - p, "[MODE ] %-10s RADIO ON %s\n", modeName(c.mode), onName);
  const RadioProfile mc = radio_driver.mcProfile(), mt = radio_driver.mtProfile();
  p += snprintf(p, e - p, "[MC   ] %.3f BW%.1f SF%u CR%u\n", (double)mc.freq, (double)mc.bw, mc.sf, mc.cr);
  p += snprintf(p, e - p, "[MT   ] %.3f BW%.0f SF%u %s\n", (double)mt.freq, (double)mt.bw, mt.sf, mtPresetName());
  if (c.shared_sf) p += snprintf(p, e - p, "[SF   ] SHARED SF%u (PEERS MUST MATCH)\n", c.shared_sf);
  // memory
  const uint32_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t ps_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const uint32_t ps_tot = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  p += snprintf(p, e - p, "[HEAP ] %luK FREE  MIN %luK\n", (unsigned long)(heap / 1024),
                (unsigned long)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
  if (ps_tot) {
    sdBar(bar, (int)((ps_tot - ps_free) * 100ull / ps_tot), 10);
    p += snprintf(p, e - p, "[PSRAM] %luK/%luK %s\n", (unsigned long)(ps_free / 1024), (unsigned long)(ps_tot / 1024), bar);
  }
  sdBar(bar, s_sd_cpu, 10);
  p += snprintf(p, e - p, "[CPU  ] %3u%%  %s\n", s_sd_cpu, bar);
  const RadioStats st = radio_driver.stats();
  if (radio_driver.cadLooks())
    p += snprintf(p, e - p, "[RX   ] MC %lu  MT %lu  CAD %lu/%lu\n", (unsigned long)st.mcRx, (unsigned long)st.mtRx,
                  (unsigned long)st.cadHits, (unsigned long)st.peeks);
  else
    p += snprintf(p, e - p, "[RX   ] MC %lu  MT %lu  MT-WIN %lu\n", (unsigned long)st.mcRx, (unsigned long)st.mtRx,
                  (unsigned long)st.peeks);
  if (c.mode == MODE_DUAL)
    p += snprintf(p, e - p, "[TDM  ] RX WINDOWS MC %lums / MT %lums%s\n", (unsigned long)radio_driver.homeMs(),
                  (unsigned long)radio_driver.mtWindowMs(), radio_driver.cadLooks() ? " (CAD LOOKS)" : "");
  p += snprintf(p, e - p, "[TX   ] MC %lu  MT %lu  FAIL %lu  NOISE %d\n", (unsigned long)st.mcTx, (unsigned long)st.mtTx,
                (unsigned long)(st.mcTxFail + st.mtTxFail), (int)st.noise);
  // Meshtastic RX debug (v0.1.1): radio events -> frames -> decode results.
  {
    const Mt::RxDiag dg = Mt::rxDiag();
    p += snprintf(p, e - p, "[MTRX ] PRE %lu HDR %lu HDRERR %lu CRC %lu\n", (unsigned long)st.mtPre,
                  (unsigned long)st.mtHdr, (unsigned long)st.mtHdrErr, (unsigned long)st.mtCrcErr);
    p += snprintf(p, e - p, "[MTRX ] RAW %lu OK %lu NOKEY %lu BADDEC %lu\n", (unsigned long)dg.frames,
                  (unsigned long)dg.decoded, (unsigned long)dg.noKey, (unsigned long)dg.badDecrypt);
    p += snprintf(p, e - p, "[MTRX ] FALLBK %lu PKI? %lu DUP %lu SELF %lu\n", (unsigned long)dg.fallback,
                  (unsigned long)dg.pkiOther, (unsigned long)dg.dupes, (unsigned long)dg.self);
    if (radio_driver.cadLooks())
      p += snprintf(p, e - p, "[MTRX ] CADMISS %lu\n", (unsigned long)st.cadMiss);
    if (st.mtLastMs) {
      p += snprintf(p, e - p, "[MTRX ] LAST %lus AGO RSSI %d SNR %.1f LEN %u\n",
                    (unsigned long)((millis() - st.mtLastMs) / 1000), (int)st.mtLastRssi,
                    (double)st.mtLastSnr4 / 4.0, (unsigned)st.mtLastLen);
    } else {
      p += snprintf(p, e - p, "[MTRX ] LAST -- (NO MESHTASTIC FRAME YET)\n");
    }
    if (dg.lastMs)
      p += snprintf(p, e - p, "[MTRX ] FROM !%08lx HASH 0x%02X PORT %u\n", (unsigned long)dg.lastFrom,
                    dg.lastHash, (unsigned)dg.lastPort);
    p += snprintf(p, e - p, "[MTRX ] MY HASH 0x%02X (%s)  MC CRC %lu\n", (unsigned)mtPrimaryHash(),
                  mtPrimaryName(), (unsigned long)st.mcCrcErr);
    p += snprintf(p, e - p, "[RADIO] REARM %lu LEVEL-IRQ %lu\n", (unsigned long)st.rearms, (unsigned long)st.levelIrqs);
  }
  char sn[5]; Mt::shortName(sn);
  p += snprintf(p, e - p, "[NODE ] %s (%s) !%08lx\n", Mt::longName(), sn, (unsigned long)Mt::myNum());
  snprintf(p, e - p, "[KEYS ] B DASHBOARD  M MESHTASTIC  BACK CLOSES\n"
                     "        SETTINGS > MESH > RADIO & MESH = MT RADIO");
  lv_label_set_text(s_dash_body, b);
}
static void dashTimerCb(lv_timer_t*) { dashFill(); }
static void dashChatsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeScoobyDashboard();
  scoobyOpenMtChats();
}
static void dashOpenScoobyAsync(void*) { goToTab(SETTINGS_TAB_INDEX); openSettingsCategory(CAT_SCOOBY); }
static void dashSettingsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeScoobyDashboard();
  lv_async_call(dashOpenScoobyAsync, nullptr);   // after this click, once the page is gone
}
static void dashHomeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeScoobyDashboard();
}
static void closeScoobyDashboard() {
  if (s_dash_timer) { lv_timer_del(s_dash_timer); s_dash_timer = nullptr; }
  s_dash_body = nullptr;
  if (s_dash_root) popupClose(&s_dash_root);
  appPageEnd(closeScoobyDashboard);
}
static void scoobyOpenDashboard() {
  if (s_dash_root) return;
  scoobyCpuBegin();
  s_dash_root = appPageCreateRoot(0x000000);
  appPageBegin("Scooby Deck", closeScoobyDashboard);
  const lv_coord_t top = statusBarCurH() - STATUSBAR_H + 4;
  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  lv_obj_t* hdr = lv_label_create(s_dash_root);
  char t[64];
  snprintf(t, sizeof t, "> SCOOBY DECK v%s  [%s]", SCOOBY_DECK_VERSION, DualMesh::modeName(DualMesh::cfg().mode));
  lv_label_set_text(hdr, t);
  sdStyleTermLabel(hdr, &lv_font_unscii_8, sdInk());
  lv_obj_set_pos(hdr, 6, top);
  lv_obj_t* body = lv_obj_create(s_dash_root);
  lv_obj_remove_style_all(body);
  const lv_coord_t btn_h = 26;
  const lv_coord_t body_h = appPageContentH() - top - 14 - btn_h - 8;
  lv_obj_set_size(body, w - 8, body_h);
  lv_obj_set_pos(body, 4, top + 12);
  lv_obj_set_style_border_color(body, lv_color_hex(sdDim()), LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(body, 4, LV_PART_MAIN);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  s_dash_body = lv_label_create(body);
  lv_obj_set_width(s_dash_body, w - 18);
  lv_label_set_long_mode(s_dash_body, LV_LABEL_LONG_WRAP);
  sdStyleTermLabel(s_dash_body, &lv_font_unscii_8, sdInk());
  lv_obj_set_style_text_line_space(s_dash_body, 3, LV_PART_MAIN);
  const lv_coord_t by = top + 12 + body_h + 4;
  const lv_coord_t bw = (w - 8 - 2 * 4) / 3;
  lv_obj_t* b1 = sdButton(s_dash_root, "[ CHATS ]", dashChatsCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b1, 4, by);
  lv_obj_t* b2 = sdButton(s_dash_root, "[ SETUP ]", dashSettingsCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b2, 4 + bw + 4, by);
  lv_obj_t* b3 = sdButton(s_dash_root, "[ HOME ]", dashHomeCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b3, 4 + 2 * (bw + 4), by);
  dashFill();
  s_dash_timer = lv_timer_create(dashTimerCb, 1000, nullptr);
}
// After the boot splash: the dashboard is the home screen (once per boot, not
// over the first-run setup wizard).
static void scoobyBootDashboard() {
  static bool done = false;
  if (done) return;
  done = true;
  scoobyCpuBegin();
  if (s_setup_root) return;
  scoobyOpenDashboard();
}

// ============================================================================
// Meshtastic chats
// ============================================================================
enum { MTV_LIST = 0, MTV_CHAN, MTV_DMLIST, MTV_DM, MTV_NODES };
static lv_obj_t*   s_mt_root = nullptr;
static lv_obj_t*   s_mt_msgs = nullptr;
static lv_obj_t*   s_mt_ta = nullptr;
static lv_timer_t* s_mt_timer = nullptr;
static uint8_t     s_mt_view = MTV_LIST;
static uint8_t     s_mt_chan = 0;
static uint32_t    s_mt_peer = 0;
static uint32_t    s_mt_seq = 0;
static char        s_mt_title[40];
static void closeMtChats();
static void mtShow(uint8_t view);

static lv_coord_t mtTop() { return statusBarCurH() - STATUSBAR_H + 4; }

static lv_obj_t* mtListContainer() {
  const lv_coord_t top = mtTop();
  lv_obj_t* c = lv_obj_create(s_mt_root);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, lv_disp_get_hor_res(nullptr), appPageContentH() - top);
  lv_obj_set_pos(c, 0, top);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(c, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_row(c, 4, LV_PART_MAIN);
  lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}
static lv_obj_t* mtRowBtn(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = sdButton(parent, "", cb, ud, lv_disp_get_hor_res(nullptr) - 8, 28);
  lv_obj_t* l = lv_obj_get_child(b, 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
  return b;
}
// A row's tap rebuilds the page the row is on: do it after the event, never
// inside the clicked object's own handler.
static void mtShowAsyncCb(void* v) { if (s_mt_root) mtShow((uint8_t)(uintptr_t)v); }
static void mtShowLater(uint8_t view) {
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  lv_async_call(mtShowAsyncCb, (void*)(uintptr_t)view);
}
static void mtChanCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_mt_chan = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  mtShowLater(MTV_CHAN);
}
static void mtDmListCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) mtShowLater(MTV_DMLIST); }
static void mtNodesCb(lv_event_t* e)  { if (lv_event_get_code(e) == LV_EVENT_CLICKED) mtShowLater(MTV_NODES); }
static void mtPeerCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_mt_peer = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  mtShowLater(MTV_DM);
}

static void mtAge(uint32_t ms, char* out, size_t cap) {
  const uint32_t s = (millis() - ms) / 1000;
  if (s < 60) snprintf(out, cap, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
  else snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}

static void mtFillMsgs() {
  if (!s_mt_msgs || !lv_obj_is_valid(s_mt_msgs)) return;
  using namespace DualMesh;
  lv_obj_clean(s_mt_msgs);
  const uint16_t n = Mt::msgCount();
  static Mt::Msg m;
  int shown = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (!Mt::msgAt(i, m)) continue;
    if (s_mt_view == MTV_CHAN && (m.chan != s_mt_chan)) continue;
    if (s_mt_view == MTV_DM && (m.chan != Mt::DM_CHAN || m.peer != s_mt_peer)) continue;
    char who[40], age[8], line[300];
    if (m.outgoing) snprintf(who, sizeof who, "ME");
    else Mt::nodeName(m.from, who, sizeof who);
    mtAge(m.ms, age, sizeof age);
    const char* st = "";
    if (m.outgoing) {
      if (m.chan == Mt::DM_CHAN) st = m.state == 2 ? " [ACK]" : m.state == 3 ? " [FAIL]" : " [..]";
      else st = m.state == 3 ? " [FAIL]" : "";
    }
    if (m.outgoing) snprintf(line, sizeof line, "%s> %s%s  %s", who, m.text, st, age);
    else snprintf(line, sizeof line, "<%s> %s  %s %ddB", who, m.text, age, (int)m.rssi);
    lv_obj_t* l = lv_label_create(s_mt_msgs);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, line);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    useChainedFont(l);
    lv_obj_set_style_text_color(l, lv_color_hex(m.outgoing ? sdDim() : sdInk()), LV_PART_MAIN);
    shown++;
  }
  if (!shown) {
    lv_obj_t* l = lv_label_create(s_mt_msgs);
    lv_label_set_text(l, "-- no messages yet --");
    sdStyleTermLabel(l, &lv_font_unscii_8, sdDim());
  }
  lv_obj_update_layout(s_mt_msgs);
  lv_obj_scroll_to_y(s_mt_msgs, LV_COORD_MAX, LV_ANIM_OFF);
  if (s_mt_view == MTV_CHAN) Mt::markRead(s_mt_chan);
  else if (s_mt_view == MTV_DM) Mt::markRead(Mt::DM_CHAN);
  s_mt_seq = Mt::changeSeq();
}

static void mtSend() {
  if (!s_mt_ta || !lv_obj_is_valid(s_mt_ta)) return;
  kbMirrorSyncToReal();
  const char* t = lv_textarea_get_text(s_mt_ta);
  if (!t || !t[0]) return;
  char buf[200];
  snprintf(buf, sizeof buf, "%s", t);
  bool ok;
  if (s_mt_view == MTV_DM) ok = DualMesh::Mt::sendDm(s_mt_peer, buf) != 0;
  else ok = DualMesh::Mt::sendChannel(s_mt_chan, buf);
  if (!ok && g_lv.task) g_lv.task->showAlert(DualMesh::cfg().mode == DualMesh::MODE_MESHCORE
                                             ? "Meshtastic is off (MeshCore-only mode)" : "Send failed (queue full or channel has no key)", 1800);
  lv_textarea_set_text(s_mt_ta, "");
  mtFillMsgs();
}
static void mtSendCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED || c == LV_EVENT_READY) mtSend();
}

static void mtTimerCb(lv_timer_t*) {
  if (!s_mt_root) return;
  if (DualMesh::Mt::changeSeq() == s_mt_seq) return;
  if (s_mt_view == MTV_CHAN || s_mt_view == MTV_DM) mtFillMsgs();
  else mtShow(s_mt_view);   // lists: counts changed
}

static void mtShow(uint8_t view) {
  using namespace DualMesh;
  if (!s_mt_root) return;
  s_mt_view = view;
  s_mt_msgs = nullptr; s_mt_ta = nullptr;
  lv_obj_clean(s_mt_root);
  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  char t[96];
  switch (view) {
    case MTV_LIST: {
      snprintf(s_mt_title, sizeof s_mt_title, "Meshtastic");
      lv_obj_t* c = mtListContainer();
      if (cfg().mode == MODE_MESHCORE) {
        lv_obj_t* l = lv_label_create(c);
        lv_obj_set_width(l, w - 8);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_label_set_text(l, "! MESHCORE-ONLY MODE: MESHTASTIC RX/TX IS OFF. CHANGE IT IN SETTINGS > SCOOBY DECK.");
        sdStyleTermLabel(l, &lv_font_unscii_8, TERM_AMBER);
      }
      for (uint8_t i = 0; i < MT_CHANNELS; i++) {
        const MtChannel& ch = cfg().ch[i];
        if (i > 0 && !ch.enabled) continue;
        const char* nm = (i == 0 && !ch.name[0]) ? mtPrimaryName() : ch.name;
        const uint16_t u = Mt::unread(i);
        if (u) snprintf(t, sizeof t, "[#%u] %-12s (%u NEW)", i, nm, u);
        else   snprintf(t, sizeof t, "[#%u] %s", i, nm);
        mtRowBtn(c, t, mtChanCb, (void*)(intptr_t)i);
      }
      const uint16_t du = Mt::unread(Mt::DM_CHAN);
      if (du) snprintf(t, sizeof t, "[DM] DIRECT MESSAGES (%u NEW)", du);
      else    snprintf(t, sizeof t, "[DM] DIRECT MESSAGES");
      mtRowBtn(c, t, mtDmListCb, nullptr);
      snprintf(t, sizeof t, "[NODES] HEARD %u", Mt::nodeCount());
      mtRowBtn(c, t, mtNodesCb, nullptr);
      break;
    }
    case MTV_DMLIST: case MTV_NODES: {
      snprintf(s_mt_title, sizeof s_mt_title, view == MTV_DMLIST ? "Direct messages" : "Nodes");
      lv_obj_t* c = mtListContainer();
      // Peers: anyone we have DM'd or heard a DM from, then the heard nodes.
      uint32_t peers[48]; int np = 0;
      if (view == MTV_DMLIST) {
        static Mt::Msg m;
        for (int i = (int)Mt::msgCount() - 1; i >= 0 && np < 48; i--) {
          if (!Mt::msgAt((uint16_t)i, m) || m.chan != Mt::DM_CHAN || !m.peer) continue;
          bool dup = false;
          for (int k = 0; k < np; k++) dup |= peers[k] == m.peer;
          if (!dup) peers[np++] = m.peer;
        }
      }
      for (uint8_t i = 0; i < Mt::nodeCount() && np < 48; i++) {
        Mt::Node nd;
        if (!Mt::nodeAt(i, nd)) continue;
        bool dup = false;
        for (int k = 0; k < np; k++) dup |= peers[k] == nd.num;
        if (!dup) peers[np++] = nd.num;
      }
      if (!np) {
        lv_obj_t* l = lv_label_create(c);
        lv_label_set_text(l, "-- no nodes heard yet --");
        sdStyleTermLabel(l, &lv_font_unscii_8, sdDim());
      }
      for (int k = 0; k < np; k++) {
        char nm[40]; Mt::nodeName(peers[k], nm, sizeof nm);
        Mt::Node nd {}; bool known = false;
        for (uint8_t i = 0; i < Mt::nodeCount(); i++) if (Mt::nodeAt(i, nd) && nd.num == peers[k]) { known = true; break; }
        char age[8] = "-";
        if (known) mtAge(nd.lastMs, age, sizeof age);
        if (known) snprintf(t, sizeof t, "%s %s %ddB %s", nm, age, (int)nd.rssi, nd.hasKey ? "[PKI]" : "");
        else       snprintf(t, sizeof t, "%s", nm);
        mtRowBtn(c, t, mtPeerCb, (void*)(uintptr_t)peers[k]);
      }
      break;
    }
    case MTV_CHAN: case MTV_DM: {
      if (view == MTV_CHAN) {
        const MtChannel& ch = cfg().ch[s_mt_chan];
        snprintf(s_mt_title, sizeof s_mt_title, "#%s", (s_mt_chan == 0 && !ch.name[0]) ? mtPrimaryName() : ch.name);
      } else {
        char nm[32]; Mt::nodeName(s_mt_peer, nm, sizeof nm);
        snprintf(s_mt_title, sizeof s_mt_title, "DM %s", nm);
      }
      const lv_coord_t top = mtTop();
      const lv_coord_t comp_h = 34;
      const lv_coord_t h = appPageContentH() - top;
      s_mt_msgs = lv_obj_create(s_mt_root);
      lv_obj_remove_style_all(s_mt_msgs);
      lv_obj_set_size(s_mt_msgs, w - 8, h - comp_h - 6);
      lv_obj_set_pos(s_mt_msgs, 4, top);
      lv_obj_set_flex_flow(s_mt_msgs, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(s_mt_msgs, 3, LV_PART_MAIN);
      lv_obj_set_style_pad_all(s_mt_msgs, 3, LV_PART_MAIN);
      lv_obj_set_style_border_color(s_mt_msgs, lv_color_hex(sdDim()), LV_PART_MAIN);
      lv_obj_set_style_border_width(s_mt_msgs, 1, LV_PART_MAIN);
      lv_obj_add_flag(s_mt_msgs, LV_OBJ_FLAG_SCROLLABLE);
      const lv_coord_t send_w = 64;
      s_mt_ta = lv_textarea_create(s_mt_root);
      lv_obj_set_size(s_mt_ta, w - 8 - send_w - 4, comp_h - 4);
      lv_obj_set_pos(s_mt_ta, 4, top + h - comp_h);
      lv_textarea_set_one_line(s_mt_ta, true);
      lv_textarea_set_max_length(s_mt_ta, view == MTV_DM ? 160 : 200);
      lv_obj_set_style_text_font(s_mt_ta, &g_font_14, LV_PART_MAIN);
      taSetPlaceholder(s_mt_ta, view == MTV_DM ? "Direct message..." : "Message channel...");
      attachSettingsTaEvents(s_mt_ta);
      lv_obj_add_event_cb(s_mt_ta, mtSendCb, LV_EVENT_READY, nullptr);
      lv_obj_t* sb = sdButton(s_mt_root, "[SEND]", mtSendCb, nullptr, send_w, comp_h - 4);
      lv_obj_set_pos(sb, w - 4 - send_w, top + h - comp_h);
      mtFillMsgs();
      break;
    }
  }
  s_apppage_title = s_mt_title;
  updateGlobalStatusBar();
  s_mt_seq = Mt::changeSeq();
}
// Back steps out one level: a chat goes back to the list, the list closes.
static void closeMtChats() {
  if (s_mt_root && s_mt_view != MTV_LIST) {
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    mtShow(s_mt_view == MTV_DM ? MTV_DMLIST : MTV_LIST);
    return;
  }
  if (s_mt_timer) { lv_timer_del(s_mt_timer); s_mt_timer = nullptr; }
  s_mt_msgs = nullptr; s_mt_ta = nullptr;
  if (s_mt_root) popupClose(&s_mt_root);
  appPageEnd(closeMtChats);
}
static bool scoobyDashOpen() { return s_dash_root != nullptr; }
static bool scoobyMtOpen() { return s_mt_root != nullptr; }
static void scoobyOpenMtChats() {
  if (s_mt_root) return;
  s_mt_root = appPageCreateRoot(COLOR_BG);
  appPageBegin("Meshtastic", closeMtChats);
  mtShow(MTV_LIST);
  s_mt_timer = lv_timer_create(mtTimerCb, 700, nullptr);
}

// ============================================================================
// Settings > Scooby Deck
// ============================================================================
static lv_obj_t* s_sd_mode_row = nullptr;
static lv_obj_t* s_sd_sf_row = nullptr;
static lv_obj_t* s_sd_preset_dd = nullptr;
static lv_obj_t* s_sd_slot_ta = nullptr;
static lv_obj_t* s_sd_freq_ta = nullptr;
static lv_obj_t* s_sd_bw_dd = nullptr;
static lv_obj_t* s_sd_sfc_dd = nullptr;
static lv_obj_t* s_sd_cr_dd = nullptr;
static lv_obj_t* s_sd_sync_ta = nullptr;
static lv_obj_t* s_sd_pre_ta = nullptr;
static lv_obj_t* s_sd_hops_ta = nullptr;
static lv_obj_t* s_sd_dbm_ta = nullptr;
static lv_obj_t* s_sd_home_ta = nullptr;
static lv_obj_t* s_sd_cad_ta = nullptr;
static lv_obj_t* s_sd_ann_sw = nullptr;
static lv_obj_t* s_sd_long_ta = nullptr;
static lv_obj_t* s_sd_short_ta = nullptr;
static lv_obj_t* s_sd_sync_sw = nullptr;
static lv_obj_t* s_sd_ch_en[DualMesh::MT_CHANNELS];
static lv_obj_t* s_sd_ch_name[DualMesh::MT_CHANNELS];
static lv_obj_t* s_sd_ch_key[DualMesh::MT_CHANNELS];
static const float kSdBw[] = { 31.25f, 62.5f, 125.0f, 250.0f, 500.0f };
static const char* kSdBwOpts = "31.25 kHz\n62.5 kHz\n125 kHz\n250 kHz\n500 kHz";
static const char* kSdSfOpts = "SF7\nSF8\nSF9\nSF10\nSF11\nSF12";
static const char* kSdCrOpts = "4/5\n4/6\n4/7\n4/8";

static bool sdOk(lv_obj_t* o) { return o && lv_obj_is_valid(o); }
static const char* sdTxt(lv_obj_t* ta) { return sdOk(ta) ? lv_textarea_get_text(ta) : ""; }
static void sdSetNum(lv_obj_t* ta, long v) { char b[16]; snprintf(b, sizeof b, "%ld", v); lv_textarea_set_text(ta, b); }

static void sdModeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int i = setChoiceIndex(e);
  if (i < 0 || i > 2) return;
  DualMesh::cfg().mode = (uint8_t)i;
  DualMesh::save();
  radio_driver.configChanged();
  setChoicePick(s_sd_mode_row, i);
  if (g_lv.task) g_lv.task->showAlert(i == 0 ? "Dual mesh: MeshCore home + Meshtastic peeks"
                                     : i == 1 ? "MeshCore only (Meshtastic off)" : "Meshtastic only (MeshCore TX off)", 1600);
}
static void sdSharedSfCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  static const uint8_t v[4] = { 0, 5, 7, 9 };
  const int i = setChoiceIndex(e);
  if (i < 0 || i > 3) return;
  DualMesh::cfg().shared_sf = v[i];
  DualMesh::save();
  radio_driver.configChanged();
  setChoicePick(s_sd_sf_row, i);
  if (g_lv.task) g_lv.task->showAlert(i ? "Shared SF set: every peer on BOTH meshes must use it" : "Shared SF off", 1800);
}
static void sdTermCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  scoobySetTerm(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
  if (g_lv.task) g_lv.task->showAlert("Theme saved: restart to apply", 1600);
}
static void sdDashCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeSettingsCategory();
  scoobyOpenDashboard();
}
static void sdChatsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeSettingsCategory();
  scoobyOpenMtChats();
}

static void sdSaveRadioCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  using namespace DualMesh;
  Config& c = cfg();
  if (sdOk(s_sd_preset_dd)) {
    const uint16_t sel = lv_dropdown_get_selected(s_sd_preset_dd);
    c.mt_preset = sel < mtPresetCount() ? (uint8_t)sel : PRESET_CUSTOM;
  }
  if (sdOk(s_sd_bw_dd)) c.mt_bw = kSdBw[LV_MIN(lv_dropdown_get_selected(s_sd_bw_dd), 4)];
  if (sdOk(s_sd_sfc_dd)) c.mt_sf = (uint8_t)(7 + lv_dropdown_get_selected(s_sd_sfc_dd));
  if (sdOk(s_sd_cr_dd)) c.mt_cr = (uint8_t)(5 + lv_dropdown_get_selected(s_sd_cr_dd));
  long slot = atol(sdTxt(s_sd_slot_ta));
  if (slot < 0) slot = 0;
  if (slot > (long)mtSlotCount()) slot = mtSlotCount();
  c.mt_slot = (uint16_t)slot;
  float f = (float)atof(sdTxt(s_sd_freq_ta));
  c.mt_freq = (f >= 150.0f && f <= 960.0f) ? f : 0.0f;
  long sync = strtol(sdTxt(s_sd_sync_ta), nullptr, 16);
  if (sync > 0 && sync < 256) c.mt_sync = (uint8_t)sync;
  long pre = atol(sdTxt(s_sd_pre_ta));
  if (pre >= 6 && pre <= 64) c.mt_preamble = (uint8_t)pre;
  long hops = atol(sdTxt(s_sd_hops_ta));
  if (hops >= 0 && hops <= 7) c.mt_hops = (uint8_t)hops;
  long dbm = atol(sdTxt(s_sd_dbm_ta));
  if (dbm >= -9 && dbm <= 22) c.mt_tx_dbm = (int8_t)dbm;
  long home = atol(sdTxt(s_sd_home_ta));
  c.tdm_home_ms = (uint16_t)LV_CLAMP(0, home, 2000);
  long cad = atol(sdTxt(s_sd_cad_ta));
  c.tdm_cad_syms = (uint8_t)LV_CLAMP(0, cad, 16);
  if (sdOk(s_sd_ann_sw)) c.mt_announce = lv_obj_has_state(s_sd_ann_sw, LV_STATE_CHECKED) ? 1 : 0;
  save();
  radio_driver.configChanged();
  char b[96];
  snprintf(b, sizeof b, "Meshtastic radio saved: %.3f MHz %s", (double)mtFreqMHz(), mtPresetName());
  if (g_lv.task) g_lv.task->showAlert(b, 1800);
}

static void sdSaveNamesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  using namespace DualMesh;
  Config& c = cfg();
  snprintf(c.long_name, sizeof c.long_name, "%s", sdTxt(s_sd_long_ta));
  snprintf(c.short_name, sizeof c.short_name, "%s", sdTxt(s_sd_short_ta));
  save();
  Mt::identityChanged();
  const bool sync = !sdOk(s_sd_sync_sw) || lv_obj_has_state(s_sd_sync_sw, LV_STATE_CHECKED);
  if (sync && c.long_name[0]) {
    if (NodePrefs* np = the_mesh.getNodePrefs()) {
      snprintf(np->node_name, sizeof np->node_name, "%s", c.long_name);   // MeshCore keeps 31
      the_mesh.savePrefs();
    }
  }
  if (g_lv.task) g_lv.task->showAlert(sync ? "Names saved (MeshCore name synced)" : "Names saved", 1600);
}

static void sdSaveChannelsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  using namespace DualMesh;
  int bad = -1;
  for (uint8_t i = 0; i < MT_CHANNELS; i++) {
    if (!sdOk(s_sd_ch_name[i])) continue;
    const bool en = i == 0 || (sdOk(s_sd_ch_en[i]) && lv_obj_has_state(s_sd_ch_en[i], LV_STATE_CHECKED));
    const char* nm = sdTxt(s_sd_ch_name[i]);
    const char* key = sdTxt(s_sd_ch_key[i]);
    if (i > 0 && !nm[0]) { setChannel(i, "", "none", false); continue; }   // empty = deleted
    if (!setChannel(i, nm, key, en) && bad < 0) bad = i;
  }
  save();
  radio_driver.configChanged();
  char b[80];
  if (bad >= 0) snprintf(b, sizeof b, "Channel %d: bad PSK (hex 32/64 digits, base64, or 0-10)", bad);
  else snprintf(b, sizeof b, "Meshtastic channels saved");
  if (g_lv.task) g_lv.task->showAlert(b, 2200);
}
static void sdClearChCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i <= 0 || i >= DualMesh::MT_CHANNELS) return;
  if (sdOk(s_sd_ch_name[i])) lv_textarea_set_text(s_sd_ch_name[i], "");
  if (sdOk(s_sd_ch_key[i])) lv_textarea_set_text(s_sd_ch_key[i], "");
  if (sdOk(s_sd_ch_en[i])) lv_obj_clear_state(s_sd_ch_en[i], LV_STATE_CHECKED);
}
static void sdResetCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  using namespace DualMesh;
  uint8_t priv[32]; const uint8_t had = cfg().mt_priv_set;
  memcpy(priv, cfg().mt_priv, 32);
  resetDefaults();
  memcpy(cfg().mt_priv, priv, 32); cfg().mt_priv_set = had;   // keep the DM identity
  save();
  radio_driver.configChanged();
  closeSettingsCategory();
  if (g_lv.task) g_lv.task->showAlert("Dual-mesh settings reset to defaults", 1800);
}

// The mode and Meshtastic radio cards: on Settings > Scooby Deck AND, since
// v0.1.1, on Settings > Radio & Mesh under WadaMesh's MeshCore radio cards.
static void sdBuildModeCard() {
  using namespace DualMesh;
  const Config& c = cfg();
  // Mode
  {
    lv_obj_t* card = setCard("Mesh mode");
    static const char* modes[3] = { "Dual", "MeshCore", "Meshtastic" };
    s_sd_mode_row = setChoiceRow(card, "Radio time-sharing",
                                 "Dual: MeshCore is home, with short Meshtastic listens. Single modes keep the radio on one mesh "
                                 "and turn the other one's sending off. Applies at once.",
                                 modes, 3, c.mode, sdModeCb);
    char mc[64];
    const RadioProfile p = radio_driver.mcProfile();
    snprintf(mc, sizeof mc, "%.3f / %.1f / SF%u", (double)p.freq, (double)p.bw, p.sf);
    setInfoRow(card, "MeshCore radio", mc);
    setNote(card, "MeshCore frequency, bandwidth, SF, CR and its channels are in Radio & Mesh and in Chats, as in WadaMesh.");
    static const char* sfs[4] = { "Off", "SF5", "SF7", "SF9" };
    const int sfi = c.shared_sf == 5 ? 1 : c.shared_sf == 7 ? 2 : c.shared_sf == 9 ? 3 : 0;
    s_sd_sf_row = setChoiceRow(card, "Shared spreading factor", "One SF on both meshes.", sfs, 4, sfi, sdSharedSfCb);
    setNote(card, "Every peer must use the same SF or you will not hear each other. Off keeps each mesh on its standard SF.",
            SET_TONE_WARN);
  }
}
static void sdBuildMtRadioCard() {
  using namespace DualMesh;
  const Config& c = cfg();
  // Meshtastic radio
  {
    lv_obj_t* card = setCard("Meshtastic radio");
    char opts[256] = "";
    for (uint8_t i = 0; i < mtPresetCount(); i++) { strcat(opts, mtPresets()[i].name); strcat(opts, "\n"); }
    strcat(opts, "Custom");
    s_sd_preset_dd = setDropdownRow(card, "Modem preset", nullptr, opts,
                                    c.mt_preset == PRESET_CUSTOM ? mtPresetCount() : c.mt_preset, nullptr);
    int bwi = 3;
    for (int i = 0; i < 5; i++) if (kSdBw[i] == c.mt_bw) bwi = i;
    s_sd_bw_dd = setDropdownRow(card, "Custom bandwidth", nullptr, kSdBwOpts, bwi, nullptr);
    s_sd_sfc_dd = setDropdownRow(card, "Custom SF", nullptr, kSdSfOpts, LV_CLAMP(0, (int)c.mt_sf - 7, 5), nullptr);
    s_sd_cr_dd = setDropdownRow(card, "Coding rate", "Custom preset only", kSdCrOpts, LV_CLAMP(0, (int)c.mt_cr - 5, 3), nullptr);
    char help[96];
    snprintf(help, sizeof help, "1-%u, 0 = from the primary channel name. Now: %.3f MHz", mtSlotCount(), (double)mtFreqMHz());
    s_sd_slot_ta = setNumFieldRow(card, "Frequency slot", help, 4);
    lv_textarea_set_accepted_chars(s_sd_slot_ta, "0123456789");
    sdSetNum(s_sd_slot_ta, c.mt_slot);
    s_sd_freq_ta = setNumFieldRow(card, "Frequency (MHz)", "Overrides the slot. 0 = use the slot.", 8);
    lv_textarea_set_accepted_chars(s_sd_freq_ta, "0123456789.");
    { char b[16]; snprintf(b, sizeof b, "%.3f", (double)c.mt_freq); lv_textarea_set_text(s_sd_freq_ta, c.mt_freq > 0 ? b : "0"); }
    s_sd_sync_ta = setNumFieldRow(card, "Sync word (hex)", "Meshtastic 2B", 2);
    lv_textarea_set_accepted_chars(s_sd_sync_ta, "0123456789abcdefABCDEF");
    { char b[4]; snprintf(b, sizeof b, "%02X", c.mt_sync); lv_textarea_set_text(s_sd_sync_ta, b); }
    s_sd_pre_ta = setNumFieldRow(card, "Preamble (symbols)", "Meshtastic 16", 2);
    lv_textarea_set_accepted_chars(s_sd_pre_ta, "0123456789");
    sdSetNum(s_sd_pre_ta, c.mt_preamble);
    s_sd_hops_ta = setNumFieldRow(card, "Hop limit", "0-7, for what you send", 1);
    lv_textarea_set_accepted_chars(s_sd_hops_ta, "01234567");
    sdSetNum(s_sd_hops_ta, c.mt_hops);
    s_sd_dbm_ta = setNumFieldRow(card, "TX power (dBm)", "Meshtastic frames, max 22", 3);
    lv_textarea_set_accepted_chars(s_sd_dbm_ta, "-0123456789");
    sdSetNum(s_sd_dbm_ta, c.mt_tx_dbm);
    s_sd_ann_sw = setSwitchRow(card, "Announce NodeInfo", "Every 3 h and before DMs, so others see your name and key", c.mt_announce != 0, nullptr);
    s_sd_home_ta = setNumFieldRow(card, "TDM MeshCore window (ms)", "Dual: MeshCore RX time between Meshtastic windows. 0 = auto (31 ms at MediumSlow)", 4);
    lv_textarea_set_accepted_chars(s_sd_home_ta, "0123456789");
    sdSetNum(s_sd_home_ta, c.tdm_home_ms);
    s_sd_cad_ta = setNumFieldRow(card, "TDM look mode", "0 = RX windows (default, v0.1.2). 2/4/8 = old CAD peeks with that many symbols", 2);
    lv_textarea_set_accepted_chars(s_sd_cad_ta, "0123456789");
    sdSetNum(s_sd_cad_ta, c.tdm_cad_syms);
    setNote(card, "Meshtastic never rebroadcasts from this node (client-mute). Default: MediumSlow, slot 48 = 913.875 MHz.");
    setButton(card, nullptr, "Save Meshtastic radio", sdSaveRadioCb, nullptr, SET_PRIMARY);
  }
}

// Settings > Scooby Deck from another page: after this click's event is done.
static void sdOpenScoobyAsync(void*) { closeSettingsCategory(); goToTab(SETTINGS_TAB_INDEX); openSettingsCategory(CAT_SCOOBY); }
static void sdOpenRadioAsync(void*)  { closeSettingsCategory(); goToTab(SETTINGS_TAB_INDEX); openSettingsCategory(CAT_RADIO); }
static void sdScoobyLinkCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_async_call(sdOpenScoobyAsync, nullptr);
}
static void sdRadioLinkCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_async_call(sdOpenRadioAsync, nullptr);
}

// Settings > Mesh > Radio & Mesh: the Meshtastic section, below MeshCore's.
static void scoobyBuildRadioMeshSection() {
  {
    lv_obj_t* card = setCard("Meshtastic (Scooby Deck)");
    char b[96];
    snprintf(b, sizeof b, "%s  %.3f MHz  SF%u", DualMesh::mtPresetName(), (double)DualMesh::mtFreqMHz(), DualMesh::mtSf());
    setInfoRow(card, "Meshtastic radio", b);
    setActionRow(card, UI_ICON_INFO, "Open dashboard (key B)", nullptr, sdDashCb);
    setActionRow(card, UI_ICON_MESSAGE_SQUARE, "Meshtastic chats (key M)", nullptr, sdChatsCb);
    setActionRow(card, UI_ICON_RADIO_TOWER, "Channels, names & more (Scooby Deck)", nullptr, sdScoobyLinkCb);
  }
  sdBuildModeCard();
  sdBuildMtRadioCard();
}

static void buildScoobySettings() {
  using namespace DualMesh;
  const Config& c = cfg();
  // About
  {
    lv_obj_t* card = setCard("Scooby Deck");
    char v[64];
    snprintf(v, sizeof v, "v%s (WadaMesh %s)", SCOOBY_DECK_VERSION, FIRMWARE_VERSION);
    setInfoRow(card, "Version", v);
    char id[48]; char sn[5]; Mt::shortName(sn);
    snprintf(id, sizeof id, "!%08lx %s", (unsigned long)Mt::myNum(), sn);
    setInfoRow(card, "Meshtastic node", id);
    setActionRow(card, UI_ICON_INFO, "Open dashboard (key B)", nullptr, sdDashCb);
    setActionRow(card, UI_ICON_MESSAGE_SQUARE, "Meshtastic chats (key M)", nullptr, sdChatsCb);
    setActionRow(card, UI_ICON_RADIO_TOWER, "Radio & Mesh (MeshCore + Meshtastic radio)", nullptr, sdRadioLinkCb);
    setSwitchRow(card, "Terminal theme", "Green-on-black, square and flat. Restart to apply.", scoobyTermOn(), sdTermCb);
    setNote(card, "Free software under the GPL-3.0. Built on WadaMesh (ALLFATHER-BV, GPL-3.0) with dual-mesh "
                  "time slicing ported from SquachWatch-CYD (skizzophrenic, GPL-3.0). Speaks MeshCore (MIT) and "
                  "Meshtastic (GPL-3.0). No warranty.");
  }
  sdBuildModeCard();
  sdBuildMtRadioCard();
  // Names
  {
    lv_obj_t* card = setCard("Names");
    s_sd_long_ta = setField(card, "Long name (up to 39)", "Scooby Deck", 39);
    lv_textarea_set_text(s_sd_long_ta, c.long_name);
    s_sd_short_ta = setField(card, "Short name (up to 4)", "SCBY", 4);
    lv_textarea_set_text(s_sd_short_ta, c.short_name);
    s_sd_sync_sw = setSwitchRow(card, "Use as MeshCore name", "The long name also becomes your MeshCore node name (31 max)", true, nullptr);
    setButton(card, nullptr, "Save names", sdSaveNamesCb, nullptr, SET_PRIMARY);
  }
  // Channels
  {
    lv_obj_t* card = setCard("Meshtastic channels");
    setNote(card, "Up to 8. Each is its own group chat. PSK: 32 or 64 hex digits (AES-128/256), base64 (e.g. AQ==), "
                  "or 1-10 for a default key, 0 for none. Empty name deletes. Channel 0 is the primary: an empty name "
                  "means the preset's name (MediumSlow), which is what stock nodes use.");
    for (uint8_t i = 0; i < MT_CHANNELS; i++) {
      const MtChannel& ch = c.ch[i];
      char t[24];
      snprintf(t, sizeof t, i == 0 ? "Channel 0 (primary)" : "Channel %u", i);
      if (i == 0) { s_sd_ch_en[0] = nullptr; setInfoRow(card, t, "always on"); }
      else s_sd_ch_en[i] = setSwitchRow(card, t, nullptr, ch.enabled != 0, nullptr);
      lv_obj_t* row = setFieldRow(card);
      s_sd_ch_name[i] = setField(row, "Name", i == 0 ? mtPresetName() : "name", 11, setFieldW(3));
      lv_textarea_set_text(s_sd_ch_name[i], ch.name);
      s_sd_ch_key[i] = setField(row, "PSK", "AQ==", 64, setFieldW(3) * 2 + SC(8));
      char hex[72] = "";
      if (ch.keyLen) keyHex(ch, hex, sizeof hex);
      else if (ch.name[0] || i == 0) snprintf(hex, sizeof hex, "0");
      lv_textarea_set_text(s_sd_ch_key[i], hex);
      if (i > 0) setActionRow(card, UI_ICON_X, "Clear this channel", nullptr, sdClearChCb, (void*)(intptr_t)i, SET_NORMAL, false);
    }
    setButton(card, nullptr, "Save channels", sdSaveChannelsCb, nullptr, SET_PRIMARY);
  }
  {
    lv_obj_t* card = setCard("Reset");
    setButton(card, nullptr, "Reset dual-mesh settings", sdResetCb, nullptr, SET_DANGER);
  }
}
#endif  // SCOOBY_DECK
