// ============================================================================
// Scooby Deck v2.0 UI (included into UITask.cpp; -D SCOOBY_DECK -D SCOOBY_V2).
//   HOME   : the status dashboard, always under everything, with three boxes:
//            [ CORE ] (MeshCore)  [ SETUP ]  [ TASTIC ] (Meshtastic).
//   CORE / TASTIC : last heard + channels with counts, DMs, channel edit, chats.
//   SETUP  : Listening to, Device name, Meshtastic radio, MeshCore radio, Display.
//   First-boot name setup (from v1.2.1), lock screen = the boot banner.
// Green-on-black, unscii. GPL-3.0-or-later, like WadaMesh and SquachWatch-CYD.
// ============================================================================
#if defined(SCOOBY_DECK)
#include "../dualmesh/DualMeshConfig.h"
#include "../dualmesh/DualMeshMt.h"
#include "../dualmesh/DualMeshStore.h"
#include "../dualmesh/ScoobyVersion.h"
#include <esp_freertos_hooks.h>
#include <Preferences.h>

extern "C" volatile uint8_t lv_scooby_flat;   // patched into lv_draw_rect.c (scripts/build/patch_lvgl_scooby_flat.py)

// ---- terminal look ---------------------------------------------------------
static constexpr uint32_t TERM_GREEN = 0x00FF41, TERM_MID = 0x00B32D, TERM_DIM = 0x00802A,
                          TERM_DARK = 0x003311, TERM_AMBER = 0xFFB000, TERM_RED = 0xFF4040;
static int8_t s_sd_term = -1;
// The theme switch: /dm_term.bin on SPIFFS since v1.2.0 (NVS is wiped by a 0x0 flash).
// v2.0: the terminal look is the only look.
static bool scoobyTermOn() { (void)s_sd_term; return true; }
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
static bool sdOk(lv_obj_t* o) { return o && lv_obj_is_valid(o); }
static const char* sdTxt(lv_obj_t* ta) { return sdOk(ta) ? lv_textarea_get_text(ta) : ""; }

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
// v2 display settings: /dm_v2ui.bin (DualMeshStore: SPIFFS + CRC). WadaMesh's own
// prefs get the same values so its screen / lock / keyboard code follows them.
// ============================================================================
struct V2Ui {
  uint8_t  ver;        // 1
  uint8_t  bright;     // screen backlight %, 5-100
  uint16_t sleep_s;    // screen sleep, 0 = never
  uint8_t  kb_mode;    // keyboard light: 0 off, 1 on, 2 auto (lit while typing)
  uint8_t  kb_pct;     // keyboard light level %
  uint8_t  lock;       // 1 = lock when the screen sleeps (boot screen, hold trackball 3 s)
  uint8_t  mc_preset;  // MeshCore NA preset picked in SETUP (index), 0xFF = custom
};
static V2Ui s_v2ui = { 0, 0, 0, 0, 0, 0, 0 };
static void v2uiLoad() {
  s_v2ui.ver = 1;
  s_v2ui.bright = touchPrefsGetBrightness();
  s_v2ui.sleep_s = g_lv.task ? g_lv.task->getScreenTimeoutSecs() : 30;
#if KB_BL_DIMMABLE
  s_v2ui.kb_mode = s_kb_bl_mode;
  s_v2ui.kb_pct = s_tdeck_kb_bl_pct;
#endif
  s_v2ui.lock = s_lock_on_screen_off ? 1 : 0;
  s_v2ui.mc_preset = 0;
  V2Ui t;
  if (DualMesh::Store::get("v2ui", &t, sizeof t) == (long)sizeof t && t.ver == 1) s_v2ui = t;
}
static void v2uiSave() { DualMesh::Store::put("v2ui", &s_v2ui, sizeof s_v2ui); }
// persist: also write WadaMesh's prefs (NVS); false at boot (they are only re-applied).
static void v2uiApply(bool persist) {
  if (s_v2ui.bright < 5) s_v2ui.bright = 5;
  if (s_v2ui.bright > 100) s_v2ui.bright = 100;
  applyBrightness(s_v2ui.bright);
  if (g_lv.task && g_lv.task->getScreenTimeoutSecs() != s_v2ui.sleep_s) g_lv.task->setScreenTimeoutSecs(s_v2ui.sleep_s);
#if KB_BL_DIMMABLE
  s_kb_bl_mode = s_v2ui.kb_mode > 2 ? 2 : s_v2ui.kb_mode;
  s_tdeck_kb_bl_pct = s_v2ui.kb_pct > 100 ? 100 : s_v2ui.kb_pct;
  if (s_kb_bl_mode) noteKbActivity();   // auto: light it now so the change shows
#if defined(HAS_TDECK_KEYBOARD)
  tdeckKeyboardSetBacklight(s_kb_bl_mode ? tdeckKbBlLevel() : 0);
#endif
  if (persist) kbBlScheduleSave();
#endif
  s_lock_on_screen_off = s_v2ui.lock != 0;
  if (persist) {
    touchPrefsSetBrightness(s_v2ui.bright);
    touchPrefsSetLockOnScreenOff(s_lock_on_screen_off);
  }
}

// ============================================================================
// HOME: the dashboard, always open under the CORE / SETUP / TASTIC pages.
// ============================================================================
static lv_obj_t*   s_dash_root = nullptr;
static lv_obj_t*   s_dash_body = nullptr;
static lv_timer_t* s_dash_timer = nullptr;
static lv_obj_t*   s_dash_hdr = nullptr;
static void closeScoobyDashboard();
static void scoobyOpenMtChats();
static void sdOpenNet(uint8_t net);
static void sdOpenSetupPage();
static void closeMtChats();

static void dashFill() {
  if (!s_dash_body || !lv_obj_is_valid(s_dash_body)) return;
  scoobyCpuSample();
  using namespace DualMesh;
  const Config& c = cfg();
  static char b[1600]; char bar[16];
  char* p = b; char* e = b + sizeof b;
#if CAP_BATTERY
  const uint16_t mv = batteryMvSmoothed();
  const int pct = mv ? batteryPercentFromMv(mv) : -1;
#else
  const uint16_t mv = 0; const int pct = -1;
#endif
  sdBar(bar, pct < 0 ? 0 : pct, 10);
  if (pct >= 0) p += snprintf(p, e - p, "[BATT ] %3d%%  %d.%02dV %s\n", pct, mv / 1000, (mv % 1000) / 10, bar);
  else          p += snprintf(p, e - p, "[BATT ] --\n");
  const int mc_unread = g_lv.task ? g_lv.task->getUnreadTotal() : 0;
  p += snprintf(p, e - p, "[MSGS ] CORE %-3d TASTIC %d\n", mc_unread, (int)Mt::unreadTotal());
  const uint8_t on = radio_driver.onProfile();
  const char* onName = on == ON_MESHCORE ? "MESHCORE" : on == ON_MESHTASTIC ? "MESHTASTIC"
                     : on == ON_TX ? "TX" : on == ON_PAUSED ? "PAUSED" : "IDLE";
  const char* lis = c.mode == MODE_DUAL ? "BOTH" : c.mode == MODE_MESHCORE ? "MESHCORE" : "MESHTASTIC";
  p += snprintf(p, e - p, "[LISTN] %-10s RADIO ON %s\n", lis, onName);
  const RadioProfile mc = radio_driver.mcProfile(), mt = radio_driver.mtProfile();
  p += snprintf(p, e - p, "[CORE ] %.3f BW%.1f SF%u CR%u\n", (double)mc.freq, (double)mc.bw, mc.sf, mc.cr);
  p += snprintf(p, e - p, "[TASTC] %.3f BW%.0f SF%u %s\n", (double)mt.freq, (double)mt.bw, mt.sf, mtPresetName());
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
  {
    const Mt::RxDiag dg = Mt::rxDiag();
    p += snprintf(p, e - p, "[MTRX ] PRE %lu HDR %lu HDRERR %lu CRC %lu\n", (unsigned long)st.mtPre,
                  (unsigned long)st.mtHdr, (unsigned long)st.mtHdrErr, (unsigned long)st.mtCrcErr);
    p += snprintf(p, e - p, "[MTRX ] RAW %lu OK %lu NOKEY %lu BADDEC %lu\n", (unsigned long)dg.frames,
                  (unsigned long)dg.decoded, (unsigned long)dg.noKey, (unsigned long)dg.badDecrypt);
    p += snprintf(p, e - p, "[MTRX ] FALLBK %lu PKI? %lu DUP %lu SELF %lu\n", (unsigned long)dg.fallback,
                  (unsigned long)dg.pkiOther, (unsigned long)dg.dupes, (unsigned long)dg.self);
    p += snprintf(p, e - p, "[MTRX ] ACKTX %lu ACKRX %lu (ROLE CLIENT_MUTE)\n", (unsigned long)dg.ackTx,
                  (unsigned long)dg.ackRx);
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
  snprintf(p, e - p, "[KEYS ] C CORE  T TASTIC  P SETUP  B HOME\n"
                     "        SPACE: SLEEP + LOCK   BACK: CLOSE PAGE");
  lv_label_set_text(s_dash_body, b);
  if (s_dash_hdr && lv_obj_is_valid(s_dash_hdr)) {
    char t[64];
    snprintf(t, sizeof t, "> SCOOBY DECK v%s  [%s]", SCOOBY_DECK_VERSION, lis);
    lv_label_set_text(s_dash_hdr, t);
  }
}
static void dashTimerCb(lv_timer_t*) {
  // Only while HOME is what you see (no page over it) and the screen is on.
  if (scoobyMtOpen()) return;
  if (g_lv.task && g_lv.task->isScreenOff()) return;
  dashFill();
}
static void dashCoreCb(lv_event_t* e)   { if (lv_event_get_code(e) == LV_EVENT_CLICKED) sdOpenNet(1); }
static void dashTasticCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) sdOpenNet(0); }
static void dashSetupCb(lv_event_t* e)  { if (lv_event_get_code(e) == LV_EVENT_CLICKED) sdOpenSetupPage(); }
// HOME never closes (Back / the dismiss keys reach it last and do nothing).
static void closeScoobyDashboard() {}
static void scoobyOpenDashboard() {
  if (s_dash_root) return;
  scoobyCpuBegin();
  s_dash_root = appPageCreateRoot(0x000000);
  const lv_coord_t top = 4;
  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  s_dash_hdr = lv_label_create(s_dash_root);
  sdStyleTermLabel(s_dash_hdr, &lv_font_unscii_8, sdInk());
  lv_obj_set_pos(s_dash_hdr, 6, top);
  lv_label_set_text(s_dash_hdr, "> SCOOBY DECK");
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
  lv_obj_t* b1 = sdButton(s_dash_root, "[ CORE ]", dashCoreCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b1, 4, by);
  lv_obj_t* b2 = sdButton(s_dash_root, "[ SETUP ]", dashSetupCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b2, 4 + bw + 4, by);
  lv_obj_t* b3 = sdButton(s_dash_root, "[ TASTIC ]", dashTasticCb, nullptr, bw, btn_h);
  lv_obj_set_pos(b3, 4 + 2 * (bw + 4), by);
  dashFill();
  s_dash_timer = lv_timer_create(dashTimerCb, 1000, nullptr);
}

// ============================================================================
// ============================================================================
// Mesh pages (TASTIC = Meshtastic, CORE = MeshCore; layout from v1.2.0): home (last heard + channels), last heard, node
// details, DMs, edit channels, and the channel / DM chats.
// One root (s_mt_root) rebuilt per view; popups are an overlay on that root.
// The status bar stays one row (appPageBeginSlim) so 3 nodes + 3 channels fit.
// ============================================================================
enum { MTV_HOME = 0, MTV_CHAN, MTV_DMLIST, MTV_DM, MTV_HEARD, MTV_EDIT,
       SV_LIST, SV_MT, SV_MC, SV_DISP };   // v2: SETUP views share the page root
enum { NET_MT = 0, NET_MC = 1 };
static uint8_t s_net = NET_MT;   // which mesh the CORE/TASTIC page shows
static lv_obj_t*   s_mt_root = nullptr;
static lv_obj_t*   s_mt_msgs = nullptr;
static lv_obj_t*   s_mt_ta = nullptr;
static lv_obj_t*   s_mt_scroll = nullptr;    // the view's scrolling container (kept across refreshes)
static lv_obj_t*   s_mt_pop = nullptr;       // an open popup (overlay)
static lv_obj_t*   s_mt_pop_name = nullptr;  // channel entry popup fields
static lv_obj_t*   s_mt_pop_key = nullptr;
static lv_timer_t* s_mt_timer = nullptr;
static uint8_t     s_mt_view = MTV_HOME;
static uint8_t     s_mt_chan = 0;
static uint32_t    s_mt_peer = 0;
static uint8_t     s_mt_dm_back = MTV_DMLIST;   // where Back goes from a DM
static int8_t      s_mt_sel = -1;               // Edit: selected slot
static int8_t      s_mt_edit_slot = -1;         // Edit: slot the entry popup writes
static uint32_t    s_mt_pop_num = 0;            // node / DM peer the popup is about
static uint32_t    s_mt_seq = 0;
static uint32_t    s_mt_built_ms = 0;
static char        s_mt_title[40];
static void closeMtChats();
static void mtShow(uint8_t view);

static lv_coord_t mtTop() { return statusBarCurH() - STATUSBAR_H + 2; }
static lv_coord_t mtW() { return lv_disp_get_hor_res(nullptr); }
static lv_coord_t mtH() { return appPageContentH(); }
static const lv_coord_t MT_BTN_H = 26, MT_ROW_H = 22, MT_HINT_H = 14;
static lv_coord_t mtBtnY() { return mtH() - 4 - MT_BTN_H; }

static void mtAge(uint32_t ms, char* out, size_t cap) {
  const uint32_t s = (millis() - ms) / 1000;
  if (s < 60) snprintf(out, cap, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
  else snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}
static const char* mtChanName(uint8_t i) {
  const DualMesh::MtChannel& ch = DualMesh::cfg().ch[i];
  return (i == 0 && !ch.name[0]) ? DualMesh::mtPrimaryName() : ch.name;
}
static bool mtSlotUsed(uint8_t i) {
  const DualMesh::MtChannel& ch = DualMesh::cfg().ch[i];
  return i == 0 || (ch.enabled && ch.name[0]);
}

// ---- building blocks -------------------------------------------------------
static lv_obj_t* mtLabel(lv_obj_t* parent, const char* text, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  sdStyleTermLabel(l, &lv_font_unscii_8, color);
  return l;
}
static void mtStyleScroll(lv_obj_t* o) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(o, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_bg_color(o, lv_color_hex(sdInk()), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(o, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(o, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(o, 1, LV_PART_SCROLLBAR);
}
// A bordered box. Column flex; `scroll` makes it the view's scroller.
static lv_obj_t* mtBox(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, bool scroll) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(b, lv_color_hex(scoobyTermOn() ? TERM_DIM : COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(b, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(b, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_right(b, scroll ? 8 : 3, LV_PART_MAIN);
  lv_obj_set_style_pad_row(b, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  if (scroll) mtStyleScroll(b); else lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}
// A tappable row. `ev` gets SHORT_CLICKED (tap) and LONG_PRESSED (hold).
static lv_obj_t* mtRow(lv_obj_t* parent, lv_coord_t h, lv_event_cb_t ev, void* ud) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), h);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(r, lv_color_hex(scoobyTermOn() ? TERM_DARK : COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  if (ev) {
    lv_obj_add_event_cb(r, ev, LV_EVENT_SHORT_CLICKED, ud);
    lv_obj_add_event_cb(r, ev, LV_EVENT_LONG_PRESSED, ud);
  }
  return r;
}
static lv_obj_t* mtRowText(lv_obj_t* row, const char* text, uint32_t color, lv_align_t al, lv_coord_t x, lv_coord_t w) {
  lv_obj_t* l = mtLabel(row, text, color);
  if (w > 0) { lv_obj_set_width(l, w); lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP); }
  lv_obj_align(l, al, x, 0);
  return l;
}
// "(n)": black on green when n is new, dim otherwise.
static void mtBadge(lv_obj_t* row, unsigned n, bool hot) {
  char b[12];
  snprintf(b, sizeof b, "(%u)", n);
  lv_obj_t* l = mtLabel(row, b, hot ? 0x000000 : sdDim());
  if (hot) {
    lv_obj_set_style_bg_color(l, lv_color_hex(sdInk()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(l, 1, LV_PART_MAIN);
  }
  lv_obj_align(l, LV_ALIGN_RIGHT_MID, -1, 0);
}
static void mtHint(const char* text) {
  lv_obj_t* l = mtLabel(s_mt_root, text, sdDim());
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -3);
}
static lv_obj_t* mtPinnedBtn(int idx, const char* text, lv_event_cb_t cb) {
  const lv_coord_t bw = (mtW() - 8 - 4) / 2;
  lv_obj_t* b = sdButton(s_mt_root, text, cb, nullptr, bw, MT_BTN_H);
  lv_obj_set_pos(b, 4 + idx * (bw + 4), mtBtnY());
  return b;
}

// A view change from inside an event: after the event, never in the clicked
// object's own handler (the rebuild deletes it).
static void mtShowAsyncCb(void* v) { if (s_mt_root) mtShow((uint8_t)(uintptr_t)v); }
static void mtShowLater(uint8_t view) {
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  lv_async_call(mtShowAsyncCb, (void*)(uintptr_t)view);
}

// ---- popups ----------------------------------------------------------------
static void mtPopDelete() {
  if (s_mt_pop && lv_obj_is_valid(s_mt_pop)) lv_obj_del(s_mt_pop);
  s_mt_pop = nullptr; s_mt_pop_name = nullptr; s_mt_pop_key = nullptr;
}
static void mtPopCloseAsync(void*) { mtPopDelete(); }
static void mtPopClose() {   // from a popup's own button: after the event
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  lv_async_call(mtPopCloseAsync, nullptr);
}
static void mtPopCloseCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) mtPopClose(); }
// Dimmed modal overlay with a bordered panel; returns the panel.
static lv_obj_t* mtPopOpen(lv_coord_t pw, lv_coord_t ph, lv_coord_t py, const char* title) {
  mtPopDelete();
  // On the top layer, over the page root: it is then the frontmost layer-top
  // child, so trackball / keyboard focus stays inside the popup.
  s_mt_pop = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_mt_pop);
  lv_obj_set_size(s_mt_pop, mtW(), mtH());
  lv_obj_set_pos(s_mt_pop, 0, appPageContentTop());
  lv_obj_set_style_bg_color(s_mt_pop, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_mt_pop, LV_OPA_70, LV_PART_MAIN);
  lv_obj_add_flag(s_mt_pop, LV_OBJ_FLAG_CLICKABLE);   // modal
  lv_obj_clear_flag(s_mt_pop, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* p = lv_obj_create(s_mt_pop);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, pw, ph);
  if (py < 0) lv_obj_center(p); else lv_obj_align(p, LV_ALIGN_TOP_MID, 0, py);
  lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(p, lv_color_hex(sdInk()), LV_PART_MAIN);
  lv_obj_set_style_border_width(p, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(p, 0, LV_PART_MAIN);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* t = mtLabel(p, title, sdInk());
  lv_obj_set_pos(t, 8, 6);
  lv_obj_update_layout(p);   // callers place children from the panel's real size
  return p;
}
// Two buttons along the panel's bottom edge.
static lv_obj_t* mtPopBtn(lv_obj_t* panel, int idx, const char* text, lv_event_cb_t cb, bool danger) {
  const lv_coord_t pw = lv_obj_get_width(panel);
  const lv_coord_t bw = (pw - 3 * 10) / 2;
  lv_obj_t* b = sdButton(panel, text, cb, nullptr, bw, MT_BTN_H - 4);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 10 + idx * (bw + 10), -8);
  if (danger) {
    lv_obj_set_style_border_color(b, lv_color_hex(TERM_RED), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_hex(TERM_RED), LV_PART_MAIN);
  }
  return b;
}
static void mtConfirm(const char* msg, lv_event_cb_t yes, const char* yesText) {
  lv_obj_t* p = mtPopOpen(mtW() - 40, 104, -1, "> CONFIRM");
  lv_obj_t* l = mtLabel(p, msg, sdInk());
  lv_obj_set_width(l, lv_obj_get_width(p) - 20);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_line_space(l, 4, LV_PART_MAIN);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 26);
  mtPopBtn(p, 0, "[ CANCEL ]", mtPopCloseCb, false);
  mtPopBtn(p, 1, yesText, yes, true);
}
// ---- node details / delete -------------------------------------------------
static void mtNodeDmCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  s_mt_peer = s_mt_pop_num;
  s_mt_dm_back = MTV_HEARD;
  mtShowLater(MTV_DM);   // the rebuild removes the popup
}
static void mtNodePopup(uint32_t num) {
  using namespace DualMesh;
  Mt::Node nd {};
  const bool known = Mt::nodeByNum(num, nd);
  char sn[8], ln[40], hw[24], age[8], pos[40], batt[12], hops[16], snr[16], rssi[16];
  Mt::nodeNames(num, sn, sizeof sn, ln, sizeof ln);
  snprintf(hw, sizeof hw, "?");
  if (known && nd.hasUser) { const char* h = Mt::hwName(nd.hwModel, hw, sizeof hw); if (h != hw) snprintf(hw, sizeof hw, "%s", h); }
  snprintf(age, sizeof age, "?"); if (known) mtAge(nd.lastMs, age, sizeof age);
  if (known && nd.hasPos) snprintf(pos, sizeof pos, "%.4f, %.4f", nd.latI / 1e7, nd.lonI / 1e7);
  else snprintf(pos, sizeof pos, "no GPS");
  if (!known || !nd.battery) snprintf(batt, sizeof batt, "?");
  else if (nd.battery > 100) snprintf(batt, sizeof batt, "powered");
  else snprintf(batt, sizeof batt, "%u%%", nd.battery);
  if (!known || nd.hops == 0xFF) snprintf(hops, sizeof hops, "?");
  else if (nd.hops == 0) snprintf(hops, sizeof hops, "0 (direct)");
  else snprintf(hops, sizeof hops, "%u", nd.hops);
  if (known) { snprintf(snr, sizeof snr, "%+.1f dB", nd.snr4 / 4.0); snprintf(rssi, sizeof rssi, "%d dBm", nd.rssi); }
  else { snprintf(snr, sizeof snr, "?"); snprintf(rssi, sizeof rssi, "?"); }
  char body[400];
  snprintf(body, sizeof body,
           "LONG   %s\nSHORT  %s\nID     !%08lx\nHW     %s\nROLE   %s\nHEARD  %s ago\nSNR    %s\nRSSI   %s\n"
           "HOPS   %s\nPOS    %s\nBATT   %s\nPKI    %s",
           ln[0] ? ln : "?", sn[0] ? sn : "?", (unsigned long)num, hw,
           known && nd.hasUser ? Mt::roleName(nd.role) : "?", age, snr, rssi, hops, pos, batt,
           known && nd.hasKey ? "key known (DMs encrypted)" : "no key yet");
  const lv_coord_t ph = LV_MIN(mtH() - 8, 204);
  lv_obj_t* p = mtPopOpen(mtW() - 24, ph, -1, "> NODE");
  s_mt_pop_num = num;
  lv_obj_t* l = mtLabel(p, body, sdInk());
  lv_obj_set_width(l, mtW() - 24 - 20);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_line_space(l, 3, LV_PART_MAIN);
  lv_obj_set_pos(l, 10, 20);
  mtPopBtn(p, 0, "[ DM ]", mtNodeDmCb, false);
  mtPopBtn(p, 1, "[ CLOSE ]", mtPopCloseCb, false);
}
static void mtNodeDeleteYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  DualMesh::Mt::deleteNode(s_mt_pop_num);
  mtShowLater(MTV_HEARD);
}
static void mtHeardRowCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  const uint32_t num = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  if (c == LV_EVENT_SHORT_CLICKED) mtNodePopup(num);
  else if (c == LV_EVENT_LONG_PRESSED) {
    char sn[8], ln[40], msg[80];
    if (!DualMesh::Mt::nodeNames(num, sn, sizeof sn, ln, sizeof ln) || !sn[0]) snprintf(sn, sizeof sn, "%04lx", (unsigned long)(num & 0xFFFF));
    snprintf(msg, sizeof msg, "DELETE NODE %s\nFROM LIST?", sn);
    mtConfirm(msg, mtNodeDeleteYesCb, "[ DELETE ]");
    s_mt_pop_num = num;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);   // no tap on release
  }
}

// One "name  !id  age  snr" line.
static void mtNodeLine(const DualMesh::Mt::Node& nd, char* out, size_t cap, bool withHops) {
  char nm[40], age[8];
  DualMesh::Mt::nodeName(nd.num, nm, sizeof nm);
  mtAge(nd.lastMs, age, sizeof age);
  char h[6] = "";
  if (withHops) { if (nd.hops == 0xFF) snprintf(h, sizeof h, " H?"); else snprintf(h, sizeof h, " H%u", nd.hops > 9 ? 9 : nd.hops); }
  snprintf(out, cap, "%-12.12s !%08lx %3s %+5.1f%s", nm, (unsigned long)nd.num, age, nd.snr4 / 4.0, h);
}

// ---- DMs -------------------------------------------------------------------
static void mtDmDeleteYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  DualMesh::Mt::deleteDm(s_mt_pop_num);
  mtShowLater(MTV_DMLIST);
}
static void mtDmRowCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  const uint32_t peer = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  if (c == LV_EVENT_SHORT_CLICKED) { s_mt_peer = peer; s_mt_dm_back = MTV_DMLIST; mtShowLater(MTV_DM); }
  else if (c == LV_EVENT_LONG_PRESSED) {
    char sn[8], ln[40], msg[80];
    if (!DualMesh::Mt::nodeNames(peer, sn, sizeof sn, ln, sizeof ln) || !sn[0]) snprintf(sn, sizeof sn, "%04lx", (unsigned long)(peer & 0xFFFF));
    snprintf(msg, sizeof msg, "DELETE CONVERSATION\nWITH %s?", sn);
    mtConfirm(msg, mtDmDeleteYesCb, "[ DELETE ]");
    s_mt_pop_num = peer;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  }
}

// ---- Edit channels ---------------------------------------------------------
static void mtEditSave() {
  using namespace DualMesh;
  if (s_mt_edit_slot < 0 || !sdOk(s_mt_pop_name)) return;
  kbMirrorSyncToReal();
  const uint8_t i = (uint8_t)s_mt_edit_slot;
  const char* nm = sdTxt(s_mt_pop_name);
  const char* key = sdTxt(s_mt_pop_key);
  if (i > 0 && !nm[0]) { if (g_lv.task) g_lv.task->showAlert("A channel needs a name", 1600); return; }
  if (!setChannel(i, nm, key[0] ? key : "AQ==", true)) {
    if (g_lv.task) g_lv.task->showAlert("Bad PSK: 32/64 hex digits, base64 (AQ==), 1-10, or 0 for none", 2200);
    return;
  }
  save();
  radio_driver.configChanged();
  s_mt_sel = (int8_t)i;
  char b[64];
  snprintf(b, sizeof b, "Channel %u saved: %s", i, mtChanName(i));
  if (g_lv.task) g_lv.task->showAlert(b, 1600);
  mtShowLater(MTV_EDIT);
}
static void mtEditSaveCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED || c == LV_EVENT_READY) mtEditSave();
}
static lv_obj_t* mtPopField(lv_obj_t* p, const char* label, const char* ph, uint32_t maxLen, lv_coord_t y, lv_event_cb_t ready = nullptr) {
  lv_obj_t* l = mtLabel(p, label, sdDim());
  lv_obj_set_pos(l, 10, y + 9);
  lv_obj_t* ta = lv_textarea_create(p);
  lv_obj_set_size(ta, lv_obj_get_width(p) - 68, 28);
  lv_obj_set_pos(ta, 58, y);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, maxLen);
  lv_obj_set_style_text_font(ta, &g_font_14, LV_PART_MAIN);
  useChainedFont(ta);
  taSetPlaceholder(ta, ph);
  attachSettingsTaEvents(ta);
  lv_obj_add_event_cb(ta, ready ? ready : mtEditSaveCb, LV_EVENT_READY, nullptr);
  return ta;
}
// The name + PSK entry, as in Settings > Scooby Deck: hex 32/64, base64, 1-10, 0.
static void mtEditPopup(uint8_t slot) {
  using namespace DualMesh;
  s_mt_edit_slot = (int8_t)slot;
  char t[32];
  snprintf(t, sizeof t, mtSlotUsed(slot) ? "> EDIT CHANNEL %u" : "> ADD CHANNEL %u", slot);
  lv_obj_t* p = mtPopOpen(mtW() - 16, 128, 2, t);   // at the top: the keyboard comes up below
  const MtChannel& ch = cfg().ch[slot];
  s_mt_pop_name = mtPopField(p, "NAME", slot == 0 ? mtPresetName() : "e.g. NYC-Local", 11, 20);
  s_mt_pop_key = mtPopField(p, "PSK", "hex / base64 / 1-10 (empty = AQ==)", 64, 52);
  if (mtSlotUsed(slot)) {
    lv_textarea_set_text(s_mt_pop_name, ch.name);
    char hex[72] = "";
    if (ch.keyLen) keyHex(ch, hex, sizeof hex); else snprintf(hex, sizeof hex, "0");
    lv_textarea_set_text(s_mt_pop_key, hex);
  }
  mtPopBtn(p, 0, "[ SAVE ]", mtEditSaveCb, false);
  mtPopBtn(p, 1, "[ CANCEL ]", mtPopCloseCb, false);
}
static void mtEditRowCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  const uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (c == LV_EVENT_SHORT_CLICKED) { s_mt_sel = (int8_t)slot; mtShowLater(MTV_EDIT); }
  else if (c == LV_EVENT_LONG_PRESSED) {
    s_mt_sel = (int8_t)slot;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    mtEditPopup(slot);
  }
}
static void mtAddCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int slot = -1;
  if (s_mt_sel > 0 && !mtSlotUsed((uint8_t)s_mt_sel)) slot = s_mt_sel;
  for (uint8_t i = 1; slot < 0 && i < DualMesh::MT_CHANNELS; i++) if (!mtSlotUsed(i)) slot = i;
  if (slot < 0) { if (g_lv.task) g_lv.task->showAlert("All 8 channel slots are in use: remove one first", 1800); return; }
  mtEditPopup((uint8_t)slot);
}
static void mtRemoveYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  using namespace DualMesh;
  if (s_mt_sel <= 0 || s_mt_sel >= (int)MT_CHANNELS) return;
  setChannel((uint8_t)s_mt_sel, "", "none", false);
  save();
  radio_driver.configChanged();
  if (g_lv.task) g_lv.task->showAlert("Channel removed", 1400);
  mtShowLater(MTV_EDIT);
}
static void mtRemoveCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_mt_sel == 0) { if (g_lv.task) g_lv.task->showAlert("Channel 0 is the primary: it can't be removed (hold it to edit)", 2000); return; }
  if (s_mt_sel < 0 || !mtSlotUsed((uint8_t)s_mt_sel)) { if (g_lv.task) g_lv.task->showAlert("Tap a channel to select it first", 1600); return; }
  char msg[64];
  snprintf(msg, sizeof msg, "REMOVE CHANNEL %d\n%s?", s_mt_sel, mtChanName((uint8_t)s_mt_sel));
  mtConfirm(msg, mtRemoveYesCb, "[ REMOVE ]");
}
// ---- home ------------------------------------------------------------------
static void mtHeardBoxCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) mtShowLater(MTV_HEARD); }
static void mtChanRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  s_mt_chan = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  mtShowLater(MTV_CHAN);
}
static void mtDmsBtnCb(lv_event_t* e)  { if (lv_event_get_code(e) == LV_EVENT_CLICKED) mtShowLater(MTV_DMLIST); }
static void mtEditBtnCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) { s_mt_sel = -1; mtShowLater(MTV_EDIT); } }
// ============================================================================
// CORE (MeshCore) data: contacts = last heard, the_mesh channels, and the
// WadaMesh chat ring (every MeshCore message already lands there and persists).
// ============================================================================
static char     s_mc_chan[33] = "";          // open channel (its name = its chat thread)
static char     s_mc_peer[33] = "";          // open DM: contact name (= its chat thread)
static uint8_t  s_mc_pub[PUB_KEY_SIZE];      // open DM: contact key
static uint8_t  s_mc_pop_pub[PUB_KEY_SIZE];  // the contact a popup is about
static int      s_mc_pop_thread = -1;        // the DM thread a popup is about
static uint32_t s_mc_sig = 0;
static uint32_t s_mc_list_ms = 0;
static const int SD_MC_HEARD_MAX = 40;

static uint32_t sdMcNow() { mesh::RTCClock* r = the_mesh.getRTCClock(); return r ? r->getCurrentTime() : 0; }
static void sdMcAge(uint32_t ts, char* out, size_t cap) {
  const uint32_t now = sdMcNow();
  const uint32_t s = (ts && now > ts) ? now - ts : 0;
  if (!ts) snprintf(out, cap, "?");
  else if (s < 60) snprintf(out, cap, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
  else snprintf(out, cap, "%lud", (unsigned long)(s / 86400));
}
static char sdMcTypeChar(uint8_t t) {
  return t == ADV_TYPE_CHAT ? 'C' : t == ADV_TYPE_REPEATER ? 'R' : t == ADV_TYPE_ROOM ? 'S' : t == ADV_TYPE_SENSOR ? 'X' : '?';
}
static const char* sdMcTypeName(uint8_t t) {
  return t == ADV_TYPE_CHAT ? "chat" : t == ADV_TYPE_REPEATER ? "repeater" : t == ADV_TYPE_ROOM ? "room server"
       : t == ADV_TYPE_SENSOR ? "sensor" : "?";
}
// The newest contacts by when we last heard them (lastmod, our clock).
struct SdMcHeard { uint32_t lastmod; uint16_t idx; };
static int sdMcHeard(SdMcHeard* out, int cap) {
  const int n = the_mesh.getNumContacts();
  int k = 0;
  ContactInfo c;
  for (int i = 0; i < n; i++) {
    if (!the_mesh.getContactByIdx((uint32_t)i, c)) continue;
    int pos = k;
    while (pos > 0 && out[pos - 1].lastmod < c.lastmod) pos--;
    if (pos >= cap) continue;
    const int last = k < cap ? k : cap - 1;
    for (int j = last; j > pos; j--) out[j] = out[j - 1];
    out[pos].lastmod = c.lastmod; out[pos].idx = (uint16_t)i;
    if (k < cap) k++;
  }
  return k;
}
static void sdMcContactLine(const ContactInfo& c, char* out, size_t cap) {
  char age[8], h[6];
  sdMcAge(c.lastmod, age, sizeof age);
  if (c.out_path_len == OUT_PATH_UNKNOWN) snprintf(h, sizeof h, "FL");
  else snprintf(h, sizeof h, "H%u", c.out_path_len > 9 ? 9 : c.out_path_len);
  snprintf(out, cap, "%-16.16s %c %4s %s", c.name[0] ? c.name : "?", sdMcTypeChar(c.type), age, h);
}
static int sdMcChanUnread(const char* name) {
  if (!g_lv.task) return 0;
  const UITask::UIThread* t = g_lv.task->sdThread(g_lv.task->sdFindThread(name, true));
  return t ? t->unread : 0;
}
static int sdMcChanCount(const char* name) {
  if (!g_lv.task) return 0;
  int n = 0;
  for (int i = 0, m = g_lv.task->sdMsgCount(); i < m; i++) {
    const UITask::UIMessage* u = g_lv.task->sdMsgAt(i);
    if (u && u->channel && !strncmp(u->thread, name, UITask::MAX_THREAD_NAME)) n++;
  }
  return n;
}
static int sdMcDmUnreadTotal() {
  if (!g_lv.task) return 0;
  int n = 0;
  for (int i = 0; i < UITask::MAX_UI_THREADS; i++) {
    const UITask::UIThread* t = g_lv.task->sdThread(i);
    if (t && !t->channel) n += t->unread;
  }
  return n;
}
static bool sdMcChannelByName(const char* name, int& slot, ChannelDetails& cd) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, cd) && cd.name[0] && !strncmp(cd.name, name, sizeof cd.name)) { slot = i; return true; }
  }
  return false;
}
// What the page shows changed: new messages, unread counts, ACKs, contacts.
static uint32_t sdMcSignature() {
  if (!g_lv.task) return 0;
  uint32_t s = g_lv.task->sdMsgSeq() * 2654435761u;
  s ^= (uint32_t)g_lv.task->getUnreadTotal() * 40503u;
  s ^= (uint32_t)the_mesh.getNumContacts() << 20;
  const int m = g_lv.task->sdMsgCount();
  for (int i = m - 1; i >= 0 && i >= m - 4; i--) {
    const UITask::UIMessage* u = g_lv.task->sdMsgAt(i);
    if (u) s ^= (uint32_t)u->deliv_state << ((m - 1 - i) * 2);
  }
  return s;
}

// ---- sending -------------------------------------------------------------------
static bool sdMcSendChannel(const char* chan, const char* text) {
  int slot = -1; ChannelDetails cd;
  if (!sdMcChannelByName(chan, slot, cd)) return false;
  static uint32_t last_ts = 0;
  const char* sender = the_mesh.getNodePrefs()->node_name;
  if (!sender || !sender[0]) sender = "me";
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  if (ts <= last_ts) ts = last_ts + 1;
  last_ts = ts;
  if (!sendGroupMessageScoped(slot, cd, ts, sender, text)) return false;
  if (g_lv.task) g_lv.task->appSentMsgToChannel(cd.name, text, the_mesh.uiLastSentFp());
  return true;
}
static bool sdMcSendDm(const uint8_t* pub, const char* text) {
  ContactInfo* by = the_mesh.lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!by) return false;
  static uint32_t last_ts = 0;
  static uint8_t attempt_ctr = 4;
  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  if (ts <= last_ts) ts = last_ts + 1;
  last_ts = ts;
  ContactInfo rcpt = *by;
  rcpt.out_path_len = OUT_PATH_UNKNOWN;   // flood, like the WadaMesh terminal send
  uint8_t attempt = attempt_ctr++;
  if (attempt < 4) { attempt = 4; attempt_ctr = 5; }
  uint32_t expected_ack = 0, est_timeout = 0;
  if (the_mesh.sendMessage(rcpt, ts, attempt, text, expected_ack, est_timeout) == MSG_SEND_FAILED) return false;
  the_mesh.uiRegisterExpectedAck(expected_ack, by->id.pub_key);
  if (g_lv.task) g_lv.task->appSentMsgToContact(by->id.pub_key, by->name, text, expected_ack, the_mesh.uiLastSentFp());
  return true;
}

// ---- CORE popups -----------------------------------------------------------------
static void sdMcNodeDmCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  ContactInfo* c = the_mesh.lookupContactByPubKey(s_mc_pop_pub, PUB_KEY_SIZE);
  if (!c) { mtPopClose(); return; }
  memcpy(s_mc_pub, c->id.pub_key, PUB_KEY_SIZE);
  snprintf(s_mc_peer, sizeof s_mc_peer, "%s", c->name);
  s_mt_dm_back = MTV_HEARD;
  mtShowLater(MTV_DM);
}
static void sdMcNodePopup(const ContactInfo& c) {
  memcpy(s_mc_pop_pub, c.id.pub_key, PUB_KEY_SIZE);
  char key[20], age[8], path[24], body[300];
  for (int i = 0; i < 8; i++) snprintf(key + i * 2, 3, "%02x", c.id.pub_key[i]);
  sdMcAge(c.lastmod, age, sizeof age);
  if (c.out_path_len == OUT_PATH_UNKNOWN) snprintf(path, sizeof path, "flood (no route yet)");
  else if (c.out_path_len == 0) snprintf(path, sizeof path, "direct");
  else snprintf(path, sizeof path, "%u hop%s", c.out_path_len, c.out_path_len == 1 ? "" : "s");
  snprintf(body, sizeof body, "NAME   %s\nTYPE   %s\nKEY    %s..\nHEARD  %s ago\nPATH   %s\nFAV    %s",
           c.name[0] ? c.name : "?", sdMcTypeName(c.type), key, age, path, c.isFav() ? "yes" : "no");
  lv_obj_t* p = mtPopOpen(mtW() - 24, LV_MIN(mtH() - 8, 150), -1, "> CORE NODE");
  lv_obj_t* l = mtLabel(p, body, sdInk());
  lv_obj_set_width(l, mtW() - 24 - 20);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_line_space(l, 3, LV_PART_MAIN);
  lv_obj_set_pos(l, 10, 20);
  if (c.type == ADV_TYPE_CHAT) mtPopBtn(p, 0, "[ DM ]", sdMcNodeDmCb, false);
  mtPopBtn(p, 1, "[ CLOSE ]", mtPopCloseCb, false);
}
static void sdMcNodeDeleteYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  ContactInfo* c = the_mesh.lookupContactByPubKey(s_mc_pop_pub, PUB_KEY_SIZE);
  if (c) { ContactInfo copy = *c; the_mesh.uiRemoveContact(copy); }
  mtShowLater(MTV_HEARD);
}
static void sdMcHeardRowCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  ContactInfo c;
  if (!the_mesh.getContactByIdx(idx, c)) return;
  if (code == LV_EVENT_SHORT_CLICKED) sdMcNodePopup(c);
  else if (code == LV_EVENT_LONG_PRESSED) {
    memcpy(s_mc_pop_pub, c.id.pub_key, PUB_KEY_SIZE);
    char msg[80];
    snprintf(msg, sizeof msg, "DELETE CONTACT\n%.20s?", c.name[0] ? c.name : "?");
    mtConfirm(msg, sdMcNodeDeleteYesCb, "[ DELETE ]");
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  }
}
static void sdMcDmDeleteYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_lv.task && s_mc_pop_thread >= 0) g_lv.task->removeThread(s_mc_pop_thread);
  s_mc_pop_thread = -1;
  mtShowLater(MTV_DMLIST);
}
static void sdMcDmRowCb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const int ti = (int)(intptr_t)lv_event_get_user_data(e);
  const UITask::UIThread* t = g_lv.task ? g_lv.task->sdThread(ti) : nullptr;
  if (!t) return;
  if (code == LV_EVENT_SHORT_CLICKED) {
    snprintf(s_mc_peer, sizeof s_mc_peer, "%s", t->name);
    memcpy(s_mc_pub, t->mesh_contact_pub, PUB_KEY_SIZE);
    s_mt_dm_back = MTV_DMLIST;
    mtShowLater(MTV_DM);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    s_mc_pop_thread = ti;
    char msg[80];
    snprintf(msg, sizeof msg, "DELETE CONVERSATION\nWITH %.20s?", t->name);
    mtConfirm(msg, sdMcDmDeleteYesCb, "[ DELETE ]");
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  }
}

// ---- CORE channel edit -------------------------------------------------------------
static bool sdHexKey16(const char* s, uint8_t out[16]) {
  char h[40]; int n = 0;
  for (; *s && n < 39; s++) if (!isspace((unsigned char)*s)) h[n++] = *s;
  h[n] = 0;
  if (n != 32) return false;
  for (int i = 0; i < 16; i++) {
    unsigned v;
    if (!isxdigit((unsigned char)h[i * 2]) || !isxdigit((unsigned char)h[i * 2 + 1])) return false;
    sscanf(h + i * 2, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  return true;
}
static void sdMcEditSave() {
  if (s_mt_edit_slot < 0 || !sdOk(s_mt_pop_name)) return;
  kbMirrorSyncToReal();
  const int slot = s_mt_edit_slot;
  char nm[32]; snprintf(nm, sizeof nm, "%s", sdTxt(s_mt_pop_name));
  const char* key = sdTxt(s_mt_pop_key);
  uint8_t secret[16];
  if (!nm[0]) { if (g_lv.task) g_lv.task->showAlert("A channel needs a name", 1600); return; }
  if (key[0]) {
    if (!sdHexKey16(key, secret)) { if (g_lv.task) g_lv.task->showAlert("Key: 32 hex digits (or empty with a #name)", 2000); return; }
  } else if (nm[0] == '#') {
    char hashed[42];
    if (!deriveHashtagChannelKey(nm, hashed, sizeof hashed, secret)) return;
    snprintf(nm, sizeof nm, "%s", hashed);
  } else if (!strcasecmp(nm, "Public")) {
    static const uint8_t pub[16] = { 0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                     0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72 };
    memcpy(secret, pub, 16);
  } else {
    if (g_lv.task) g_lv.task->showAlert("Private channel: enter its 32-hex key (or use a #name)", 2200);
    return;
  }
  if (!the_mesh.uiAddOrUpdateChannel(slot, nm, secret)) {
    if (g_lv.task) g_lv.task->showAlert("Could not save the channel", 1600);
    return;
  }
  if (g_lv.task) { g_lv.task->refreshThreadsFromMesh(); g_lv.dirty_threads = true; }
  s_mt_sel = (int8_t)slot;
  char b[64]; snprintf(b, sizeof b, "Channel %d saved: %s", slot, nm);
  if (g_lv.task) g_lv.task->showAlert(b, 1600);
  mtShowLater(MTV_EDIT);
}
static void sdMcEditSaveCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED || c == LV_EVENT_READY) sdMcEditSave();
}
static void sdMcEditPopup(int slot) {
  s_mt_edit_slot = (int8_t)slot;
  ChannelDetails cd;
  const bool used = the_mesh.getChannel(slot, cd) && cd.name[0];
  char t[32];
  snprintf(t, sizeof t, used ? "> EDIT CHANNEL %d" : "> ADD CHANNEL %d", slot);
  lv_obj_t* p = mtPopOpen(mtW() - 16, 128, 2, t);
  s_mt_pop_name = mtPopField(p, "NAME", "#name, Public, or a name", 31, 20, sdMcEditSaveCb);
  s_mt_pop_key = mtPopField(p, "KEY", "32 hex (empty for #name)", 32, 52, sdMcEditSaveCb);
  if (used) {
    lv_textarea_set_text(s_mt_pop_name, cd.name);
    char hex[40];
    for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02x", cd.channel.secret[i]);
    lv_textarea_set_text(s_mt_pop_key, hex);
  }
  mtPopBtn(p, 0, "[ SAVE ]", sdMcEditSaveCb, false);
  mtPopBtn(p, 1, "[ CANCEL ]", mtPopCloseCb, false);
}
static bool sdMcSlotUsed(int i) { ChannelDetails cd; return the_mesh.getChannel(i, cd) && cd.name[0]; }
static void sdMcEditRowCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  const int slot = (int)(intptr_t)lv_event_get_user_data(e);
  if (c == LV_EVENT_SHORT_CLICKED) { s_mt_sel = (int8_t)slot; mtShowLater(MTV_EDIT); }
  else if (c == LV_EVENT_LONG_PRESSED) {
    s_mt_sel = (int8_t)slot;
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    sdMcEditPopup(slot);
  }
}
static void sdMcAddCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int slot = the_mesh.findFirstEmptyChannelSlot();
  if (slot < 0) { if (g_lv.task) g_lv.task->showAlert("The channel table is full: remove one first", 1800); return; }
  sdMcEditPopup(slot);
}
static void sdMcRemoveYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_mt_sel <= 0) return;
  ChannelDetails cd;
  if (the_mesh.getChannel(s_mt_sel, cd) && cd.name[0] && g_lv.task) {
    const int ti = g_lv.task->sdFindThread(cd.name, true);
    if (ti >= 0) g_lv.task->removeThread(ti);
  }
  the_mesh.uiDeleteChannel(s_mt_sel);
  if (g_lv.task) { g_lv.task->refreshThreadsFromMesh(); g_lv.dirty_threads = true; g_lv.task->showAlert("Channel removed", 1400); }
  s_mt_sel = -1;
  mtShowLater(MTV_EDIT);
}
static void sdMcRemoveCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_mt_sel == 0) { if (g_lv.task) g_lv.task->showAlert("Slot 0 is the primary: it can't be removed (hold it to edit)", 2000); return; }
  if (s_mt_sel < 0 || !sdMcSlotUsed(s_mt_sel)) { if (g_lv.task) g_lv.task->showAlert("Tap a channel to select it first", 1600); return; }
  ChannelDetails cd; the_mesh.getChannel(s_mt_sel, cd);
  char msg[64];
  snprintf(msg, sizeof msg, "REMOVE CHANNEL %d\n%.20s?", s_mt_sel, cd.name);
  mtConfirm(msg, sdMcRemoveYesCb, "[ REMOVE ]");
}
static void sdMcChanRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  ChannelDetails cd;
  if (!the_mesh.getChannel((int)(intptr_t)lv_event_get_user_data(e), cd) || !cd.name[0]) return;
  snprintf(s_mc_chan, sizeof s_mc_chan, "%s", cd.name);
  mtShowLater(MTV_CHAN);
}

// ---- CORE chats --------------------------------------------------------------------
static void sdMcFillMsgs() {
  if (!s_mt_msgs || !lv_obj_is_valid(s_mt_msgs) || !g_lv.task) return;
  lv_obj_clean(s_mt_msgs);
  const bool chan = s_mt_view == MTV_CHAN;
  const char* thread = chan ? s_mc_chan : s_mc_peer;
  // The last 60 of this thread, oldest first.
  static int16_t pick[60];
  int np = 0;
  for (int i = g_lv.task->sdMsgCount() - 1; i >= 0 && np < 60; i--) {
    const UITask::UIMessage* m = g_lv.task->sdMsgAt(i);
    if (!m || m->channel != chan || strncmp(m->thread, thread, UITask::MAX_THREAD_NAME)) continue;
    pick[np++] = (int16_t)i;
  }
  for (int k = np - 1; k >= 0; k--) {
    const UITask::UIMessage* m = g_lv.task->sdMsgAt(pick[k]);
    if (!m) continue;
    char age[8], line[260];
    sdMcAge(m->ts, age, sizeof age);
    if (m->outgoing) {
      const char* st = "";
      if (!chan) st = m->deliv_state == UITask::DELIV_DELIVERED ? " [ACK]" : m->deliv_state == UITask::DELIV_FAILED ? " [FAIL]" : " [..]";
      snprintf(line, sizeof line, "ME> %s%s  %s", m->text, st, age);
    } else {
      const char* who = m->sender[0] ? m->sender : thread;
      if (m->rssi) snprintf(line, sizeof line, "<%s> %s  %s %ddB", who, m->text, age, (int)m->rssi);
      else snprintf(line, sizeof line, "<%s> %s  %s", who, m->text, age);
    }
    lv_obj_t* l = lv_label_create(s_mt_msgs);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, line);
    lv_obj_set_style_text_font(l, &g_font_14, LV_PART_MAIN);
    useChainedFont(l);
    lv_obj_set_style_text_color(l, lv_color_hex(m->outgoing ? sdDim() : sdInk()), LV_PART_MAIN);
  }
  if (!np) mtLabel(s_mt_msgs, "-- no messages yet --", sdDim());
  lv_obj_update_layout(s_mt_msgs);
  lv_obj_scroll_to_y(s_mt_msgs, LV_COORD_MAX, LV_ANIM_OFF);
  const int ti = g_lv.task->sdFindThread(thread, chan);
  if (ti >= 0) g_lv.task->markThreadRead(ti);
  s_mc_sig = sdMcSignature();
}
static void sdMcSend(const char* text) {
  using namespace DualMesh;
  bool ok;
  if (s_mt_view == MTV_DM) {
    ok = the_mesh.lookupContactByPubKey(s_mc_pub, PUB_KEY_SIZE) != nullptr;
    if (!ok) { if (g_lv.task) g_lv.task->showAlert("Contact not found (deleted?)", 1800); return; }
    ok = sdMcSendDm(s_mc_pub, text);
  } else {
    ok = sdMcSendChannel(s_mc_chan, text);
  }
  if (!ok && g_lv.task) g_lv.task->showAlert(cfg().mode == MODE_MESHTASTIC
                                             ? "MeshCore is off (SETUP > LISTENING TO)" : "Send failed", 1800);
}

// ---- CORE views --------------------------------------------------------------------
static void sdMcShow(uint8_t view, lv_coord_t w, lv_coord_t top) {
  using namespace DualMesh;
  char t[120];
  static SdMcHeard heard[SD_MC_HEARD_MAX];
  switch (view) {
    case MTV_HOME: {
      snprintf(s_mt_title, sizeof s_mt_title, "CORE  MeshCore");
      lv_obj_t* col = lv_obj_create(s_mt_root);
      lv_obj_remove_style_all(col);
      lv_obj_set_size(col, w - 4, mtBtnY() - 4 - top);
      lv_obj_set_pos(col, 4, top);
      lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_right(col, 7, LV_PART_MAIN);
      mtStyleScroll(col);
      s_mt_scroll = col;
      if (cfg().mode == MODE_MESHTASTIC) {
        lv_obj_t* l = mtLabel(col, "! LISTENING TO MESHTASTIC ONLY: MESHCORE IS OFF. SETUP > LISTENING TO.", TERM_AMBER);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      }
      lv_obj_t* lh = mtBox(col, lv_pct(100), LV_SIZE_CONTENT, false);
      lv_obj_add_flag(lh, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_color(lh, lv_color_hex(TERM_DARK), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_add_event_cb(lh, mtHeardBoxCb, LV_EVENT_CLICKED, nullptr);
      mtLabel(lh, "> LAST HEARD", sdInk());
      const int nn = sdMcHeard(heard, 3);
      if (!nn) {
        lv_obj_t* r = mtRow(lh, MT_ROW_H, nullptr, nullptr); lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
        mtRowText(r, "-- no contacts heard yet --", sdDim(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      for (int i = 0; i < nn; i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(heard[i].idx, c)) continue;
        sdMcContactLine(c, t, sizeof t);
        lv_obj_t* r = mtRow(lh, MT_ROW_H, nullptr, nullptr);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      lv_obj_t* hint = mtLabel(lh, the_mesh.getNumContacts() > 3 ? "[ tap to open: all contacts ]" : "[ tap to open ]", sdDim());
      lv_obj_set_width(hint, lv_pct(100));
      lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(hint, 2, LV_PART_MAIN);
      lv_obj_t* cb = mtBox(col, lv_pct(100), LV_SIZE_CONTENT, false);
      mtLabel(cb, "> CHANNELS", sdInk());
      int shown = 0;
      for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails cd;
        if (!the_mesh.getChannel(i, cd) || !cd.name[0]) continue;
        lv_obj_t* r = mtRow(cb, MT_ROW_H, sdMcChanRowCb, (void*)(intptr_t)i);
        snprintf(t, sizeof t, i == 0 ? "# %s (primary)" : "# %s", cd.name);
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, w - 4 - 7 - 8 - 48);
        const int u = sdMcChanUnread(cd.name);
        mtBadge(r, u ? u : sdMcChanCount(cd.name), u != 0);
        shown++;
      }
      if (!shown) mtLabel(cb, "-- no channels: EDIT > ADD --", sdDim());
      const int du = sdMcDmUnreadTotal();
      if (du) snprintf(t, sizeof t, "[ DMs (%d) ]", du); else snprintf(t, sizeof t, "[ DMs ]");
      mtPinnedBtn(0, t, mtDmsBtnCb);
      mtPinnedBtn(1, "[ EDIT ]", mtEditBtnCb);
      break;
    }
    case MTV_HEARD: {
      snprintf(s_mt_title, sizeof s_mt_title, "CORE  Last Heard");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtH() - top - MT_HINT_H - 4, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      const int total = the_mesh.getNumContacts();
      const int nn = sdMcHeard(heard, SD_MC_HEARD_MAX);
      snprintf(t, sizeof t, "> LAST HEARD  * %d CONTACT%s%s", total, total == 1 ? "" : "S", total > nn ? " (NEWEST 40)" : "");
      mtLabel(box, t, sdInk());
      if (!nn) mtLabel(box, "\n-- no contacts heard yet --", sdDim());
      for (int i = 0; i < nn; i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(heard[i].idx, c)) continue;
        sdMcContactLine(c, t, sizeof t);
        lv_obj_t* r = mtRow(box, MT_ROW_H, sdMcHeardRowCb, (void*)(uintptr_t)heard[i].idx);
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      mtHint("C chat R rptr S room X sensor FL flood");
      break;
    }
    case MTV_DMLIST: {
      snprintf(s_mt_title, sizeof s_mt_title, "CORE  Direct messages");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtH() - top - MT_HINT_H - 4, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      int idx[UITask::MAX_UI_THREADS]; int np = 0;
      for (int i = 0; i < UITask::MAX_UI_THREADS && g_lv.task; i++) {
        const UITask::UIThread* th = g_lv.task->sdThread(i);
        if (!th || th->channel) continue;
        if (!g_lv.task->getThreadLastMessage(i, nullptr, 0, nullptr, 0, nullptr)) continue;
        int pos = np;
        while (pos > 0 && g_lv.task->sdThread(idx[pos - 1])->last_ts < th->last_ts) { idx[pos] = idx[pos - 1]; pos--; }
        idx[pos] = i; np++;
      }
      snprintf(t, sizeof t, "> DMs  * %d CONVERSATION%s", np, np == 1 ? "" : "S");
      mtLabel(box, t, sdInk());
      if (!np) {
        lv_obj_t* l = mtLabel(box, "\n-- no conversations yet --\n\nTo start one: LAST HEARD on the CORE page, tap a contact, [ DM ].", sdDim());
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      }
      for (int k = 0; k < np; k++) {
        const UITask::UIThread* th = g_lv.task->sdThread(idx[k]);
        char snd[34], txt[164]; bool out = false;
        if (!th || !g_lv.task->getThreadLastMessage(idx[k], snd, sizeof snd, txt, sizeof txt, &out)) continue;
        lv_obj_t* r = mtRow(box, 36, sdMcDmRowCb, (void*)(intptr_t)idx[k]);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(r, lv_color_hex(TERM_DARK), LV_PART_MAIN);
        lv_obj_set_style_border_width(r, 1, LV_PART_MAIN);
        char age[8];
        sdMcAge(th->last_ts, age, sizeof age);
        lv_obj_t* n = mtLabel(r, th->name, sdInk());
        lv_obj_set_width(n, w - 8 - 11 - 80); lv_label_set_long_mode(n, LV_LABEL_LONG_CLIP);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_t* a = mtLabel(r, age, sdDim());
        lv_obj_align(a, LV_ALIGN_TOP_RIGHT, th->unread ? -36 : -1, 4);
        if (th->unread) {
          char b[12]; snprintf(b, sizeof b, "(%u)", th->unread);
          lv_obj_t* bl = mtLabel(r, b, 0x000000);
          lv_obj_set_style_bg_color(bl, lv_color_hex(sdInk()), LV_PART_MAIN);
          lv_obj_set_style_bg_opa(bl, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_align(bl, LV_ALIGN_TOP_RIGHT, -1, 4);
        }
        snprintf(t, sizeof t, "%s%s", out ? "> " : "", txt);
        lv_obj_t* pv = mtLabel(r, t, sdDim());
        lv_obj_set_width(pv, w - 8 - 11 - 6); lv_label_set_long_mode(pv, LV_LABEL_LONG_DOT);
        lv_obj_align(pv, LV_ALIGN_TOP_LEFT, 4, 18);
      }
      mtHint("tap: open   hold: delete");
      break;
    }
    case MTV_EDIT: {
      snprintf(s_mt_title, sizeof s_mt_title, "CORE  Edit Channels");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtBtnY() - 4 - top, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      snprintf(t, sizeof t, "> CHANNELS  0-%d", MAX_GROUP_CHANNELS - 1);
      mtLabel(box, t, sdInk());
      bool emptyShown = false;
      for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails cd;
        const bool used = the_mesh.getChannel(i, cd) && cd.name[0];
        if (!used && (emptyShown && s_mt_sel != i)) continue;   // the used ones + the next free slot
        if (!used) emptyShown = true;
        const bool sel = s_mt_sel == i;
        lv_obj_t* r = mtRow(box, 24, sdMcEditRowCb, (void*)(intptr_t)i);
        if (sel) {
          lv_obj_set_style_bg_color(r, lv_color_hex(sdInk()), LV_PART_MAIN);
          lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
        }
        if (used) snprintf(t, sizeof t, i == 0 ? "%d  %s (primary)" : "%d  %s", i, cd.name);
        else snprintf(t, sizeof t, "%d  -- empty --", i);
        mtRowText(r, t, sel ? 0x000000 : (used ? sdInk() : sdDim()), LV_ALIGN_LEFT_MID, 2, 0);
      }
      lv_obj_t* note = mtLabel(box, "tap: select   hold: edit name/key\n#name: key made from the name", sdDim());
      lv_obj_set_style_pad_top(note, 4, LV_PART_MAIN);
      lv_obj_set_style_text_line_space(note, 3, LV_PART_MAIN);
      mtPinnedBtn(0, "[ ADD ]", sdMcAddCb);
      mtPinnedBtn(1, "[ REMOVE ]", sdMcRemoveCb);
      break;
    }
    default: break;
  }
  s_mc_sig = sdMcSignature();
  s_mc_list_ms = millis();
}

// ============================================================================
// SETUP: Listening to / Device name / Meshtastic / Meshcore / Display.
// Rows: tap cycles a value or opens a subpage / popup; changes apply live.
// ============================================================================
enum {
  SVR_LISTEN = 1, SVR_NAME, SVR_MT, SVR_MC, SVR_DISP,
  SVR_MT_PRESET = 20, SVR_MT_BW, SVR_MT_CR, SVR_MT_SLOT, SVR_MT_SF, SVR_MT_HOPS, SVR_MT_TX,
  SVR_MT_KEYSAVE, SVR_MT_KEYLOAD, SVR_MT_KEYNEW,
  SVR_MC_PRESET = 40, SVR_MC_FREQ, SVR_MC_BW, SVR_MC_SF, SVR_MC_CR, SVR_MC_TX,
  SVR_D_BRIGHT = 60, SVR_D_SLEEP, SVR_D_KBMODE, SVR_D_KBPCT, SVR_D_LOCK,
};
static void sdSvRowCb(lv_event_t* e);
// "LABEL ........ value >"
static lv_obj_t* sdSvRow(lv_obj_t* box, const char* label, const char* value, int id, bool sub) {
  lv_obj_t* r = mtRow(box, 24, id ? sdSvRowCb : nullptr, (void*)(intptr_t)id);
  if (!id) lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
  mtRowText(r, label, id ? sdInk() : sdDim(), LV_ALIGN_LEFT_MID, 2, 0);
  char v[48];
  snprintf(v, sizeof v, "%s%s", value ? value : "", sub ? " >" : "");
  lv_obj_t* l = mtRowText(r, v, id ? sdInk() : sdDim(), LV_ALIGN_RIGHT_MID, -2, 0);
  if (id && !sub) {   // a value you can cycle: inverse video
    lv_obj_set_style_bg_color(l, lv_color_hex(TERM_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(l, 2, LV_PART_MAIN);
  }
  return r;
}
static lv_obj_t* sdSvBox(lv_coord_t w, lv_coord_t top, const char* head) {
  lv_obj_t* box = mtBox(s_mt_root, w - 8, mtH() - top - MT_HINT_H - 4, true);
  lv_obj_set_pos(box, 4, top);
  s_mt_scroll = box;
  if (head) mtLabel(box, head, sdInk());
  return box;
}
static lv_obj_t* sdSvNote(lv_obj_t* box, const char* text) {
  lv_obj_t* l = mtLabel(box, text, sdDim());
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(l, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(l, 3, LV_PART_MAIN);
  return l;
}
static const char* sdListenName(uint8_t m) {
  return m == DualMesh::MODE_DUAL ? "BOTH" : m == DualMesh::MODE_MESHCORE ? "MESHCORE" : "MESHTASTIC";
}
static void sdApplyMt(const char* what) {
  DualMesh::save();
  radio_driver.configChanged();
  if (what && g_lv.task) {
    char b[96];
    snprintf(b, sizeof b, "%s: %.3f MHz SF%u", what, (double)DualMesh::mtFreqMHz(), DualMesh::mtSf());
    g_lv.task->showAlert(b, 1400);
  }
}

// ---- MeshCore NA presets ---------------------------------------------------------
struct SdMcPreset { const char* name; float freq, bw; uint8_t sf, cr; };
static const SdMcPreset kSdMcNa[] = {
  { "US/Canada",      910.525f, 62.5f, 7, 5 },
  { "USA: SoCal",     927.875f, 62.5f, 7, 5 },
  { "USA: SF Bay",    910.525f, 62.5f, 7, 5 },
  { "USA: Sacramento",909.875f, 62.5f, 9, 5 },
  { "USA: Pacific NW",910.525f, 62.5f, 7, 5 },
};
static const int kSdMcNaN = sizeof kSdMcNa / sizeof kSdMcNa[0];
static bool sdMcMatches(const SdMcPreset& p, const NodePrefs* np) {
  return fabsf(np->freq - p.freq) < 0.002f && fabsf(np->bw - p.bw) < 0.01f && np->sf == p.sf && np->cr == p.cr;
}
// The preset the radio is on: the one picked in SETUP if it still matches, else any match, else -1.
static int sdMcPresetNow() {
  const NodePrefs* np = the_mesh.getNodePrefs();
  if (s_v2ui.mc_preset < kSdMcNaN && sdMcMatches(kSdMcNa[s_v2ui.mc_preset], np)) return s_v2ui.mc_preset;
  for (int i = 0; i < kSdMcNaN; i++) if (sdMcMatches(kSdMcNa[i], np)) return i;
  return -1;
}
static void sdMcApplyRadio(float f, float bw, uint8_t sf, uint8_t cr, int8_t tx) {
  const NodePrefs* np = the_mesh.getNodePrefs();
  if (!g_lv.task) return;
  if (!g_lv.task->setRadioParams(f, bw, sf, cr, tx, np->airtime_factor)) {
    g_lv.task->showAlert("MeshCore radio: rejected", 1600);
    return;
  }
  char b[80];
  snprintf(b, sizeof b, "MeshCore: %.3f MHz BW%.1f SF%u CR%u", (double)f, (double)bw, sf, cr);
  g_lv.task->showAlert(b, 1400);
}

// ---- popups: device name, Meshtastic slot, MeshCore frequency ----------------------
static lv_obj_t* s_sv_ta2 = nullptr;
static uint8_t   s_sv_pop_kind = 0;   // 1 names, 2 MT slot, 3 MC freq
static void sdTrim(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, n - i + 1);
}
static void sdCutUtf8(char* s, size_t max) {
  if (strlen(s) <= max) return;
  size_t n = max;
  while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}
// Long name -> MeshCore node name + Meshtastic long name; short -> Meshtastic only.
static bool sdSaveNames(const char* longName, const char* shortName) {
  using namespace DualMesh;
  char ln[64], sn[16];
  snprintf(ln, sizeof ln, "%s", longName); snprintf(sn, sizeof sn, "%s", shortName);
  sdTrim(ln); sdTrim(sn);
  if (!ln[0] || !sn[0]) { if (g_lv.task) g_lv.task->showAlert("Both names are needed", 1600); return false; }
  sdCutUtf8(ln, 31);   // MeshCore keeps 31 bytes
  sdCutUtf8(sn, 4);    // Meshtastic short name: 4 max
  Config& c = cfg();
  snprintf(c.long_name, sizeof c.long_name, "%s", ln);
  snprintf(c.short_name, sizeof c.short_name, "%s", sn);
  save();
  Mt::identityChanged();
  if (g_lv.task) g_lv.task->setNodeName(ln);
  char b[80];
  snprintf(b, sizeof b, "Saved: %s / %s", ln, sn);
  if (g_lv.task) g_lv.task->showAlert(b, 1600);
  return true;
}
static void sdSvPopSave() {
  using namespace DualMesh;
  kbMirrorSyncToReal();
  if (s_sv_pop_kind == 1) {
    if (!sdSaveNames(sdTxt(s_mt_pop_name), sdTxt(s_sv_ta2))) return;
  } else if (s_sv_pop_kind == 2) {
    const long v = atol(sdTxt(s_mt_pop_name));
    if (v < 0 || v > (long)mtSlotCount()) {
      char b[64]; snprintf(b, sizeof b, "Slot: 0 (auto) or 1-%u", mtSlotCount());
      if (g_lv.task) g_lv.task->showAlert(b, 1600);
      return;
    }
    cfg().mt_slot = (uint16_t)v;
    cfg().mt_freq = 0.0f;
    sdApplyMt("Meshtastic slot");
  } else if (s_sv_pop_kind == 3) {
    const float f = (float)atof(sdTxt(s_mt_pop_name));
    if (f < 902.0f || f > 928.0f) { if (g_lv.task) g_lv.task->showAlert("North America: 902.000 - 928.000 MHz", 1800); return; }
    const NodePrefs* np = the_mesh.getNodePrefs();
    s_v2ui.mc_preset = 0xFF; v2uiSave();
    sdMcApplyRadio(f, np->bw, np->sf, np->cr, np->tx_power_dbm);
  }
  hideKb();
  mtShowLater(s_mt_view);   // rebuild the view (drops the popup)
}
static void sdSvPopSaveCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED) sdSvPopSave();
  else if (c == LV_EVENT_READY) {
    // Enter in the first of two fields moves on to the second.
    if (s_sv_pop_kind == 1 && lv_event_get_target(e) == s_mt_pop_name && sdOk(s_sv_ta2)) {
      kbMirrorBind(s_sv_ta2);
      if (s_nav_group && lv_obj_get_group(s_sv_ta2) == s_nav_group) lv_group_focus_obj(s_sv_ta2);
      return;
    }
    sdSvPopSave();
  }
}
static void sdNamesPopup() {
  const DualMesh::Config& c = DualMesh::cfg();
  s_sv_pop_kind = 1;
  lv_obj_t* p = mtPopOpen(mtW() - 16, 150, 2, "> DEVICE NAME");
  s_mt_pop_name = mtPopField(p, "LONG", "MeshCore + Meshtastic", 31, 20, sdSvPopSaveCb);
  s_sv_ta2 = mtPopField(p, "SHORT", "4 max", 4, 52, sdSvPopSaveCb);
  lv_textarea_set_text(s_mt_pop_name, c.long_name[0] ? c.long_name : DualMesh::Mt::longName());
  char sn[5]; DualMesh::Mt::shortName(sn);
  lv_textarea_set_text(s_sv_ta2, c.short_name[0] ? c.short_name : sn);
  lv_obj_t* n = mtLabel(p, "LONG: shared by TASTIC + CORE.\nSHORT: TASTIC only (4 max).", TERM_AMBER);
  lv_obj_set_style_text_line_space(n, 3, LV_PART_MAIN);
  lv_obj_set_pos(n, 10, 86);
  mtPopBtn(p, 0, "[ SAVE ]", sdSvPopSaveCb, false);
  mtPopBtn(p, 1, "[ CANCEL ]", mtPopCloseCb, false);
}
static void sdNumPopup(uint8_t kind, const char* title, const char* label, const char* ph, const char* val, const char* note) {
  s_sv_pop_kind = kind;
  s_sv_ta2 = nullptr;
  lv_obj_t* p = mtPopOpen(mtW() - 16, 118, 2, title);
  s_mt_pop_name = mtPopField(p, label, ph, 10, 20, sdSvPopSaveCb);
  lv_textarea_set_accepted_chars(s_mt_pop_name, "0123456789.");
  lv_textarea_set_text(s_mt_pop_name, val);
  lv_obj_t* n = mtLabel(p, note, sdDim());
  lv_obj_set_pos(n, 10, 54);
  mtPopBtn(p, 0, "[ SAVE ]", sdSvPopSaveCb, false);
  mtPopBtn(p, 1, "[ CANCEL ]", mtPopCloseCb, false);
}

// ---- Meshtastic key on the SD card -------------------------------------------------
static const char* kSdKeyPath = "/scooby_mt_key.txt";
static bool sdCardReady() {
#if CAP_SD
  if (!fmSdTryMount()) return false;
  markSdIo();
  return SD.cardType() != CARD_NONE;
#else
  return false;
#endif
}
static void sdKeyBackup() {
#if CAP_SD
  if (!sdCardReady()) { if (g_lv.task) g_lv.task->showAlert("No SD card", 1600); return; }
  File f = SD.open(kSdKeyPath, FILE_WRITE);
  if (!f) { if (g_lv.task) g_lv.task->showAlert("SD write failed", 1600); return; }
  const uint8_t* pr = DualMesh::Mt::privateKey();
  const uint8_t* pu = DualMesh::Mt::publicKey();
  char hex[72];
  f.printf("# Scooby Deck Meshtastic PKI key. KEEP PRIVATE: it decrypts your DMs.\n");
  f.printf("node=!%08lx\n", (unsigned long)DualMesh::Mt::myNum());
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", pr[i]);
  f.printf("private=%s\n", hex);
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", pu[i]);
  f.printf("public=%s\n", hex);
  f.close();
  if (g_lv.task) g_lv.task->showAlert("Key saved to SD: /scooby_mt_key.txt", 1800);
#endif
}
static void sdKeyRestore() {
#if CAP_SD
  if (!sdCardReady()) { if (g_lv.task) g_lv.task->showAlert("No SD card", 1600); return; }
  File f = SD.open(kSdKeyPath, FILE_READ);
  if (!f) { if (g_lv.task) g_lv.task->showAlert("No /scooby_mt_key.txt on the SD card", 1800); return; }
  uint8_t key[32]; bool got = false;
  char line[100];
  while (f.available() && !got) {
    const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
    line[n] = 0;
    if (strncmp(line, "private=", 8)) continue;
    const char* h = line + 8;
    int i = 0;
    for (; i < 32 && isxdigit((unsigned char)h[i * 2]) && isxdigit((unsigned char)h[i * 2 + 1]); i++) {
      unsigned v; sscanf(h + i * 2, "%2x", &v); key[i] = (uint8_t)v;
    }
    got = i == 32;
  }
  f.close();
  if (!got || !DualMesh::Mt::setPrivateKey(key)) { if (g_lv.task) g_lv.task->showAlert("No valid private= line in the file", 1800); return; }
  if (g_lv.task) g_lv.task->showAlert("Key restored from SD", 1600);
#endif
}
static void sdKeyNewYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  DualMesh::Mt::setPrivateKey(nullptr);
  if (g_lv.task) g_lv.task->showAlert("New key made: peers relearn it from our next NodeInfo", 2000);
  mtShowLater(SV_MT);
}
static void sdKeyLoadYesCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  sdKeyRestore();
  mtShowLater(SV_MT);
}

// ---- the row actions -----------------------------------------------------------------
static int sdCycle(const int* v, int n, int cur) {
  for (int i = 0; i < n; i++) if (v[i] == cur) return v[(i + 1) % n];
  for (int i = 0; i < n; i++) if (v[i] > cur) return v[i];
  return v[0];
}
static void sdSvRowCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  using namespace DualMesh;
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  Config& c = cfg();
  NodePrefs* np = the_mesh.getNodePrefs();
  char t[64];
  switch (id) {
    // SETUP list
    case SVR_LISTEN: {
      // Meshtastic -> Meshcore -> Both -> Meshtastic
      c.mode = c.mode == MODE_MESHTASTIC ? MODE_MESHCORE : c.mode == MODE_MESHCORE ? MODE_DUAL : MODE_MESHTASTIC;
      save();
      radio_driver.configChanged();
      snprintf(t, sizeof t, "Listening to: %s", sdListenName(c.mode));
      if (g_lv.task) g_lv.task->showAlert(t, 1200);
      mtShowLater(SV_LIST);
      return;
    }
    case SVR_NAME: sdNamesPopup(); return;
    case SVR_MT:   mtShowLater(SV_MT); return;
    case SVR_MC:   mtShowLater(SV_MC); return;
    case SVR_DISP: mtShowLater(SV_DISP); return;
    // Meshtastic
    case SVR_MT_PRESET:
      c.mt_preset = c.mt_preset == PRESET_CUSTOM ? 0 : (c.mt_preset + 1 >= mtPresetCount() ? PRESET_CUSTOM : c.mt_preset + 1);
      if (c.mt_preset == PRESET_CUSTOM) c.mt_sf_ovr = 0;
      sdApplyMt(mtPresetName());
      break;
    case SVR_MT_BW: { static const int v[] = { 62, 125, 250, 500 };
      const int nb = sdCycle(v, 4, (int)c.mt_bw); c.mt_bw = nb == 62 ? 62.5f : (float)nb; sdApplyMt("Bandwidth"); break; }
    case SVR_MT_CR: c.mt_cr = c.mt_cr >= 8 ? 5 : c.mt_cr + 1; sdApplyMt("Coding rate"); break;
    case SVR_MT_SF:
      if (c.mt_preset == PRESET_CUSTOM) c.mt_sf = c.mt_sf >= 12 ? 7 : (c.mt_sf < 7 ? 7 : c.mt_sf + 1);
      else c.mt_sf_ovr = c.mt_sf_ovr == 0 ? 7 : (c.mt_sf_ovr >= 12 ? 0 : c.mt_sf_ovr + 1);
      sdApplyMt("Spreading factor");
      break;
    case SVR_MT_SLOT: {
      char v[8], note[48];
      snprintf(v, sizeof v, "%u", c.mt_slot);
      snprintf(note, sizeof note, "0 = auto (stock default)\n1-%u at this bandwidth", mtSlotCount());
      sdNumPopup(2, "> MESHTASTIC SLOT", "SLOT", "0 = auto", v, note);
      return;
    }
    case SVR_MT_HOPS: c.mt_hops = c.mt_hops >= 7 ? 1 : c.mt_hops + 1; sdApplyMt(nullptr); break;
    case SVR_MT_TX: { static const int v[] = { 22, 20, 17, 14, 10, 5 };
      int i = 0; while (i < 6 && v[i] != c.mt_tx_dbm) i++;
      c.mt_tx_dbm = (int8_t)v[i >= 5 ? 0 : i + 1]; sdApplyMt(nullptr); break; }
    case SVR_MT_KEYSAVE: sdKeyBackup(); return;
    case SVR_MT_KEYLOAD: mtConfirm("REPLACE THE KEY WITH\n/scooby_mt_key.txt?", sdKeyLoadYesCb, "[ RESTORE ]"); return;
    case SVR_MT_KEYNEW:  mtConfirm("MAKE A NEW KEY?\nOLD DMs TO US STOP\nDECRYPTING.", sdKeyNewYesCb, "[ NEW KEY ]"); return;
    // MeshCore
    case SVR_MC_PRESET: {
      const int cur = sdMcPresetNow();
      const int nx = cur < 0 ? 0 : cur + 1;
      if (nx >= kSdMcNaN) {   // -> CUSTOM: keep the current values, edit them below
        s_v2ui.mc_preset = 0xFF; v2uiSave();
        if (g_lv.task) g_lv.task->showAlert("MeshCore: custom (edit freq / BW / SF / CR)", 1600);
        break;
      }
      s_v2ui.mc_preset = (uint8_t)nx; v2uiSave();
      sdMcApplyRadio(kSdMcNa[nx].freq, kSdMcNa[nx].bw, kSdMcNa[nx].sf, kSdMcNa[nx].cr, np->tx_power_dbm);
      break;
    }
    case SVR_MC_FREQ: {
      char v[16]; snprintf(v, sizeof v, "%.3f", (double)np->freq);
      sdNumPopup(3, "> MESHCORE FREQUENCY", "MHz", "910.525", v, "North America: 902 - 928 MHz");
      return;
    }
    case SVR_MC_BW: { static const int v[] = { 62, 125, 250, 500 };
      const int nb = sdCycle(v, 4, (int)np->bw); s_v2ui.mc_preset = 0xFF; v2uiSave();
      sdMcApplyRadio(np->freq, nb == 62 ? 62.5f : (float)nb, np->sf, np->cr, np->tx_power_dbm); break; }
    case SVR_MC_SF: s_v2ui.mc_preset = 0xFF; v2uiSave();
      sdMcApplyRadio(np->freq, np->bw, np->sf >= 12 ? 5 : np->sf + 1, np->cr, np->tx_power_dbm); break;
    case SVR_MC_CR: s_v2ui.mc_preset = 0xFF; v2uiSave();
      sdMcApplyRadio(np->freq, np->bw, np->sf, np->cr >= 8 ? 5 : np->cr + 1, np->tx_power_dbm); break;
    case SVR_MC_TX: { static const int v[] = { 22, 20, 17, 14, 10, 5 };
      int i = 0; while (i < 6 && v[i] != np->tx_power_dbm) i++;
      sdMcApplyRadio(np->freq, np->bw, np->sf, np->cr, (int8_t)v[i >= 5 ? 0 : i + 1]); break; }
    // Display
    case SVR_D_BRIGHT: { static const int v[] = { 10, 25, 40, 60, 80, 100 };
      s_v2ui.bright = (uint8_t)sdCycle(v, 6, s_v2ui.bright); v2uiApply(true); v2uiSave(); break; }
    case SVR_D_SLEEP: { static const int v[] = { 15, 30, 60, 120, 300, 600, 0 };
      int i = 0; while (i < 7 && v[i] != s_v2ui.sleep_s) i++;
      s_v2ui.sleep_s = (uint16_t)v[i >= 6 ? 0 : i + 1]; v2uiApply(true); v2uiSave(); break; }
    case SVR_D_KBMODE: s_v2ui.kb_mode = (uint8_t)((s_v2ui.kb_mode + 1) % 3);
      if (s_v2ui.kb_mode && !s_v2ui.kb_pct) s_v2ui.kb_pct = 50;
      v2uiApply(true); v2uiSave(); break;
    case SVR_D_KBPCT: { static const int v[] = { 10, 25, 50, 75, 100 };
      s_v2ui.kb_pct = (uint8_t)sdCycle(v, 5, s_v2ui.kb_pct); if (!s_v2ui.kb_mode) s_v2ui.kb_mode = 1;
      v2uiApply(true); v2uiSave(); break; }
    case SVR_D_LOCK: s_v2ui.lock = s_v2ui.lock ? 0 : 1; v2uiApply(true); v2uiSave(); break;
    default: return;
  }
  mtShowLater(s_mt_view);
}

// ---- the SETUP views ---------------------------------------------------------------
static void sdSvShow(uint8_t view, lv_coord_t w, lv_coord_t top) {
  using namespace DualMesh;
  const Config& c = cfg();
  const NodePrefs* np = the_mesh.getNodePrefs();
  char v[64], t[96];
  switch (view) {
    case SV_LIST: {
      snprintf(s_mt_title, sizeof s_mt_title, "SETUP");
      snprintf(t, sizeof t, "> SETUP                v%s", SCOOBY_DECK_VERSION);
      lv_obj_t* box = sdSvBox(w, top, t);
      sdSvRow(box, "1 LISTENING TO", sdListenName(c.mode), SVR_LISTEN, false);
      snprintf(v, sizeof v, "%.14s / %s", c.long_name[0] ? c.long_name : Mt::longName(), c.short_name);
      sdSvRow(box, "2 DEVICE NAME", v, SVR_NAME, true);
      sdSvRow(box, "3 MESHTASTIC", mtPresetName(), SVR_MT, true);
      const int pi = sdMcPresetNow();
      sdSvRow(box, "4 MESHCORE", pi >= 0 ? kSdMcNa[pi].name : "Custom", SVR_MC, true);
      snprintf(v, sizeof v, "%u%%%s", s_v2ui.bright, s_v2ui.lock ? " LOCK" : "");
      sdSvRow(box, "5 DISPLAY", v, SVR_DISP, true);
      sdSvNote(box, "LISTENING TO cycles MESHTASTIC > MESHCORE > BOTH (BOTH = time-sliced on one radio).");
      mtHint("tap: cycle / open   BACK: home");
      break;
    }
    case SV_MT: {
      snprintf(s_mt_title, sizeof s_mt_title, "SETUP  Meshtastic");
      lv_obj_t* box = sdSvBox(w, top, "> MESHTASTIC RADIO");
      sdSvRow(box, "PRESET", mtPresetName(), SVR_MT_PRESET, false);
      if (c.mt_preset == PRESET_CUSTOM) {
        snprintf(v, sizeof v, "%.1f kHz", (double)c.mt_bw); sdSvRow(box, "BANDWIDTH", v, SVR_MT_BW, false);
        snprintf(v, sizeof v, "4/%u", c.mt_cr);            sdSvRow(box, "CODING RATE", v, SVR_MT_CR, false);
        snprintf(v, sizeof v, "SF%u", c.mt_sf);            sdSvRow(box, "SPREAD FACTOR", v, SVR_MT_SF, false);
      } else {
        if (c.mt_sf_ovr) snprintf(v, sizeof v, "SF%u (OVERRIDE)", c.mt_sf_ovr);
        else snprintf(v, sizeof v, "PRESET (SF%u)", mtPresets()[c.mt_preset].sf);
        sdSvRow(box, "SPREAD FACTOR", v, SVR_MT_SF, false);
      }
      if (c.mt_freq > 100.0f) snprintf(v, sizeof v, "FIXED FREQ");
      else if (c.mt_slot) snprintf(v, sizeof v, "%u / %u", c.mt_slot, mtSlotCount());
      else snprintf(v, sizeof v, "AUTO / %u", mtSlotCount());
      sdSvRow(box, "FREQ SLOT", v, SVR_MT_SLOT, true);
      snprintf(v, sizeof v, "%.3f MHz", (double)mtFreqMHz()); sdSvRow(box, "  FREQUENCY", v, 0, false);
      snprintf(v, sizeof v, "%u", c.mt_hops);                 sdSvRow(box, "HOP LIMIT", v, SVR_MT_HOPS, false);
      snprintf(v, sizeof v, "%d dBm", c.mt_tx_dbm);           sdSvRow(box, "TX POWER", v, SVR_MT_TX, false);
      sdSvRow(box, "ROLE", "CLIENT_MUTE", 0, false);
      snprintf(v, sizeof v, "!%08lx", (unsigned long)Mt::myNum()); sdSvRow(box, "NODE ID", v, 0, false);
      const uint8_t* pk = Mt::publicKey();
      snprintf(v, sizeof v, "%02x%02x%02x%02x%02x%02x..", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5]);
      sdSvRow(box, "PKI KEY", v, 0, false);
      sdSvRow(box, "[ BACK UP KEY TO SD ]", "", SVR_MT_KEYSAVE, true);
      sdSvRow(box, "[ RESTORE KEY FROM SD ]", "", SVR_MT_KEYLOAD, true);
      sdSvRow(box, "[ GENERATE NEW KEY ]", "", SVR_MT_KEYNEW, true);
      sdSvNote(box, "Default: LongFast, slot AUTO (US: 906.875), preset SF, hop limit 3. SF/slot changes must match your peers.");
      mtHint("tap: cycle / edit");
      break;
    }
    case SV_MC: {
      snprintf(s_mt_title, sizeof s_mt_title, "SETUP  MeshCore");
      lv_obj_t* box = sdSvBox(w, top, "> MESHCORE RADIO (NORTH AMERICA)");
      const int pi = sdMcPresetNow();
      sdSvRow(box, "PRESET", pi >= 0 ? kSdMcNa[pi].name : "CUSTOM", SVR_MC_PRESET, false);
      snprintf(v, sizeof v, "%.3f MHz", (double)np->freq); sdSvRow(box, "FREQUENCY", v, SVR_MC_FREQ, true);
      snprintf(v, sizeof v, "%.1f kHz", (double)np->bw);   sdSvRow(box, "BANDWIDTH", v, SVR_MC_BW, false);
      snprintf(v, sizeof v, "SF%u", np->sf);               sdSvRow(box, "SPREAD FACTOR", v, SVR_MC_SF, false);
      snprintf(v, sizeof v, "4/%u", np->cr);               sdSvRow(box, "CODING RATE", v, SVR_MC_CR, false);
      snprintf(v, sizeof v, "%d dBm", np->tx_power_dbm);   sdSvRow(box, "TX POWER", v, SVR_MC_TX, false);
      sdSvNote(box, "Changing FREQ / BW / SF / CR makes it CUSTOM. Applied live; peers must match.");
      mtHint("tap: cycle / edit");
      break;
    }
    case SV_DISP: {
      snprintf(s_mt_title, sizeof s_mt_title, "SETUP  Display");
      lv_obj_t* box = sdSvBox(w, top, "> DISPLAY");
      snprintf(v, sizeof v, "%u%%", s_v2ui.bright); sdSvRow(box, "BRIGHTNESS", v, SVR_D_BRIGHT, false);
      if (!s_v2ui.sleep_s) snprintf(v, sizeof v, "NEVER");
      else if (s_v2ui.sleep_s < 60) snprintf(v, sizeof v, "%us", s_v2ui.sleep_s);
      else snprintf(v, sizeof v, "%um", s_v2ui.sleep_s / 60);
      sdSvRow(box, "SCREEN SLEEP", v, SVR_D_SLEEP, false);
#if KB_BL_DIMMABLE
      sdSvRow(box, "KEYBOARD LIGHT", s_v2ui.kb_mode == 0 ? "OFF" : s_v2ui.kb_mode == 1 ? "ON" : "AUTO", SVR_D_KBMODE, false);
      snprintf(v, sizeof v, "%u%%", s_v2ui.kb_pct); sdSvRow(box, "KEYBOARD LEVEL", v, SVR_D_KBPCT, false);
#endif
      sdSvRow(box, "LOCK SCREEN", s_v2ui.lock ? "ON" : "OFF", SVR_D_LOCK, false);
      sdSvNote(box, "LOCK ON: when the screen sleeps it locks; waking shows the boot screen until you HOLD the trackball 3 s.\n"
                    "SPACE (not typing): sleep + lock now (1 s countdown, any key cancels).\n"
                    "AUTO keyboard light: lit while typing.");
      mtHint("tap: cycle");
      break;
    }
    default: break;
  }
}

// ---- chats -----------------------------------------------------------------
static void mtFillMsgs() {
  if (s_net == NET_MC) { sdMcFillMsgs(); return; }
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
      else st = m.state == 3 ? " [FAIL]" : m.state == 2 ? " [RELAYED]" : "";
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
  if (!shown) mtLabel(s_mt_msgs, "-- no messages yet --", sdDim());
  lv_obj_update_layout(s_mt_msgs);
  lv_obj_scroll_to_y(s_mt_msgs, LV_COORD_MAX, LV_ANIM_OFF);
  if (s_mt_view == MTV_CHAN) Mt::markRead(s_mt_chan);
  else if (s_mt_view == MTV_DM) Mt::markReadDm(s_mt_peer);
  s_mt_seq = Mt::changeSeq();
}
static void mtSend() {
  if (!s_mt_ta || !lv_obj_is_valid(s_mt_ta)) return;
  kbMirrorSyncToReal();
  const char* t = lv_textarea_get_text(s_mt_ta);
  if (!t || !t[0]) return;
  char buf[200];
  snprintf(buf, sizeof buf, "%s", t);
  if (s_net == NET_MC) {
    sdMcSend(buf);
    lv_textarea_set_text(s_mt_ta, "");
    mtFillMsgs();
    return;
  }
  bool ok;
  if (s_mt_view == MTV_DM) ok = DualMesh::Mt::sendDm(s_mt_peer, buf) != 0;
  else ok = DualMesh::Mt::sendChannel(s_mt_chan, buf);
  if (!ok && g_lv.task) g_lv.task->showAlert(DualMesh::cfg().mode == DualMesh::MODE_MESHCORE
                                             ? "Meshtastic is off (SETUP > LISTENING TO)" : "Send failed (queue full or channel has no key)", 1800);
  lv_textarea_set_text(s_mt_ta, "");
  mtFillMsgs();
}
static void mtSendCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED || c == LV_EVENT_READY) mtSend();
}
static bool mtPointerDown() {
  for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i))
    if (i->proc.state == LV_INDEV_STATE_PRESSED) return true;
  return false;
}
static void mtTimerCb(lv_timer_t*) {
  if (!s_mt_root) return;
  if (g_lv.task && g_lv.task->isScreenOff()) return;
  if (s_mt_view >= SV_LIST) return;   // SETUP: nothing live
  if (s_net == NET_MC) {
    const bool changed = sdMcSignature() != s_mc_sig;
    if (s_mt_view == MTV_CHAN || s_mt_view == MTV_DM) { if (changed) sdMcFillMsgs(); return; }
    if (s_mt_pop || mtPointerDown() || millis() - s_mc_list_ms < 2000) return;
    if (s_mt_view == MTV_EDIT) return;
    if (changed || millis() - s_mc_list_ms > 15000) mtShow(s_mt_view);   // ages / new contacts
    return;
  }
  if (DualMesh::Mt::changeSeq() == s_mt_seq) return;
  if (s_mt_view == MTV_CHAN || s_mt_view == MTV_DM) { mtFillMsgs(); return; }
  // Lists: refresh at most every 2 s, never under a finger or an open popup
  // (a rebuild would cancel the hold / close the popup).
  if (s_mt_pop || mtPointerDown() || millis() - s_mt_built_ms < 2000) return;
  if (s_mt_view == MTV_EDIT) { s_mt_seq = DualMesh::Mt::changeSeq(); return; }   // nothing live there
  mtShow(s_mt_view);
}
// ---- the views ---------------------------------------------------------------
static void mtShow(uint8_t view) {
  using namespace DualMesh;
  if (!s_mt_root) return;
  // A refresh of the same list keeps its scroll position.
  lv_coord_t keepY = 0;
  if (view == s_mt_view && s_mt_scroll && lv_obj_is_valid(s_mt_scroll)) keepY = lv_obj_get_scroll_y(s_mt_scroll);
  s_mt_view = view;
  s_mt_msgs = nullptr; s_mt_ta = nullptr; s_mt_scroll = nullptr;
  mtPopDelete();   // never called from inside a popup's own event (those go through mtShowLater)
  lv_obj_clean(s_mt_root);
  const lv_coord_t w = mtW(), top = mtTop();
  char t[96];
  if (view >= SV_LIST) sdSvShow(view, w, top);
  else if (s_net == NET_MC) sdMcShow(view, w, top);
  if (view >= SV_LIST || s_net == NET_MC) {
    if (view == MTV_CHAN || view == MTV_DM) goto chats;   // the chat layout is shared
    goto done;
  }
  switch (view) {
    case MTV_HOME: {
      snprintf(s_mt_title, sizeof s_mt_title, "TASTIC  Meshtastic");
      lv_obj_t* col = lv_obj_create(s_mt_root);
      lv_obj_remove_style_all(col);
      lv_obj_set_size(col, w - 4, mtBtnY() - 4 - top);
      lv_obj_set_pos(col, 4, top);
      lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(col, 4, LV_PART_MAIN);
      lv_obj_set_style_pad_right(col, 7, LV_PART_MAIN);
      mtStyleScroll(col);
      s_mt_scroll = col;
      if (cfg().mode == MODE_MESHCORE) {
        lv_obj_t* l = mtLabel(col, "! LISTENING TO MESHCORE ONLY: MESHTASTIC IS OFF. SETUP > LISTENING TO.", TERM_AMBER);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      }
      // LAST HEARD: the 3 newest; the whole box opens the full list.
      lv_obj_t* lh = mtBox(col, lv_pct(100), LV_SIZE_CONTENT, false);
      lv_obj_add_flag(lh, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_color(lh, lv_color_hex(scoobyTermOn() ? TERM_DARK : COLOR_CONTROL_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_add_event_cb(lh, mtHeardBoxCb, LV_EVENT_CLICKED, nullptr);
      mtLabel(lh, "> LAST HEARD", sdInk());
      const uint8_t nn = Mt::nodeCount();
      if (!nn) {
        lv_obj_t* r = mtRow(lh, MT_ROW_H, nullptr, nullptr); lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
        mtRowText(r, "-- no nodes heard yet --", sdDim(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      for (uint8_t i = 0; i < nn && i < 3; i++) {
        Mt::Node nd;
        if (!Mt::nodeAt(i, nd)) continue;
        mtNodeLine(nd, t, sizeof t, false);
        lv_obj_t* r = mtRow(lh, MT_ROW_H, nullptr, nullptr);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);   // taps land on the box
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      lv_obj_t* hint = mtLabel(lh, nn > 3 ? "[ tap to open: all nodes ]" : "[ tap to open ]", sdDim());
      lv_obj_set_width(hint, lv_pct(100));
      lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_set_style_pad_ver(hint, 2, LV_PART_MAIN);
      // CHANNELS: one row each, with new (or stored) message counts.
      lv_obj_t* cb = mtBox(col, lv_pct(100), LV_SIZE_CONTENT, false);
      mtLabel(cb, "> CHANNELS", sdInk());
      for (uint8_t i = 0; i < MT_CHANNELS; i++) {
        if (!mtSlotUsed(i)) continue;
        lv_obj_t* r = mtRow(cb, MT_ROW_H, mtChanRowCb, (void*)(uintptr_t)i);
        snprintf(t, sizeof t, i == 0 ? "# %s (primary)" : "# %s", mtChanName(i));
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, w - 4 - 7 - 8 - 48);
        const uint16_t u = Mt::unread(i);
        mtBadge(r, u ? u : Mt::chanMsgCount(i), u != 0);
      }
      const uint16_t du = Mt::unread(Mt::DM_CHAN);
      if (du) snprintf(t, sizeof t, "[ DMs (%u) ]", du); else snprintf(t, sizeof t, "[ DMs ]");
      mtPinnedBtn(0, t, mtDmsBtnCb);
      mtPinnedBtn(1, "[ EDIT ]", mtEditBtnCb);
      break;
    }
    case MTV_HEARD: {
      snprintf(s_mt_title, sizeof s_mt_title, "TASTIC  Last Heard");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtH() - top - MT_HINT_H - 4, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      const uint8_t nn = Mt::nodeCount();
      snprintf(t, sizeof t, "> LAST HEARD  * %u NODE%s", nn, nn == 1 ? "" : "S");
      mtLabel(box, t, sdInk());
      if (!nn) mtLabel(box, "\n-- no nodes heard yet --", sdDim());
      for (uint8_t i = 0; i < nn; i++) {
        Mt::Node nd;
        if (!Mt::nodeAt(i, nd)) continue;
        mtNodeLine(nd, t, sizeof t, true);
        lv_obj_t* r = mtRow(box, MT_ROW_H, mtHeardRowCb, (void*)(uintptr_t)nd.num);
        mtRowText(r, t, sdInk(), LV_ALIGN_LEFT_MID, 0, 0);
      }
      mtHint("tap: details   hold: delete");
      break;
    }
    case MTV_DMLIST: {
      snprintf(s_mt_title, sizeof s_mt_title, "TASTIC  Direct messages");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtH() - top - MT_HINT_H - 4, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      // Conversations, newest first: anyone we have DM'd or heard a DM from.
      static Mt::Msg m;
      uint32_t peers[32]; uint16_t last[32]; int np = 0;
      for (int i = (int)Mt::msgCount() - 1; i >= 0 && np < 32; i--) {
        if (!Mt::msgAt((uint16_t)i, m) || m.chan != Mt::DM_CHAN || !m.peer) continue;
        bool dup = false;
        for (int k = 0; k < np; k++) dup |= peers[k] == m.peer;
        if (!dup) { peers[np] = m.peer; last[np] = (uint16_t)i; np++; }
      }
      snprintf(t, sizeof t, "> DMs  * %d CONVERSATION%s", np, np == 1 ? "" : "S");
      mtLabel(box, t, sdInk());
      if (!np) {
        lv_obj_t* l = mtLabel(box, "\n-- no conversations yet --\n\nTo start one: [ LAST HEARD ] on the Meshtastic page, tap a node, [ DM ].", sdDim());
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      }
      for (int k = 0; k < np; k++) {
        if (!Mt::msgAt(last[k], m)) continue;
        lv_obj_t* r = mtRow(box, 36, mtDmRowCb, (void*)(uintptr_t)peers[k]);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(r, lv_color_hex(scoobyTermOn() ? TERM_DARK : COLOR_HAIR), LV_PART_MAIN);
        lv_obj_set_style_border_width(r, 1, LV_PART_MAIN);
        char nm[40], age[8];
        Mt::nodeName(peers[k], nm, sizeof nm);
        mtAge(m.ms, age, sizeof age);
        const uint16_t u = Mt::unreadDm(peers[k]);
        lv_obj_t* n = mtLabel(r, nm, sdInk());
        lv_obj_set_width(n, w - 8 - 11 - 80); lv_label_set_long_mode(n, LV_LABEL_LONG_CLIP);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_t* a = mtLabel(r, age, sdDim());
        lv_obj_align(a, LV_ALIGN_TOP_RIGHT, u ? -36 : -1, 4);
        if (u) {
          char b[12]; snprintf(b, sizeof b, "(%u)", u);
          lv_obj_t* bl = mtLabel(r, b, 0x000000);
          lv_obj_set_style_bg_color(bl, lv_color_hex(sdInk()), LV_PART_MAIN);
          lv_obj_set_style_bg_opa(bl, LV_OPA_COVER, LV_PART_MAIN);
          lv_obj_align(bl, LV_ALIGN_TOP_RIGHT, -1, 4);
        }
        snprintf(t, sizeof t, "%s%s", m.outgoing ? "> " : "", m.text);
        lv_obj_t* pv = mtLabel(r, t, sdDim());
        lv_obj_set_width(pv, w - 8 - 11 - 6); lv_label_set_long_mode(pv, LV_LABEL_LONG_DOT);
        lv_obj_align(pv, LV_ALIGN_TOP_LEFT, 4, 18);
      }
      mtHint("tap: open   hold: delete");
      break;
    }
    case MTV_EDIT: {
      snprintf(s_mt_title, sizeof s_mt_title, "TASTIC  Edit Channels");
      lv_obj_t* box = mtBox(s_mt_root, w - 8, mtBtnY() - 4 - top, true);
      lv_obj_set_pos(box, 4, top);
      s_mt_scroll = box;
      mtLabel(box, "> CHANNELS  0-7", sdInk());
      for (uint8_t i = 0; i < MT_CHANNELS; i++) {
        const bool used = mtSlotUsed(i), sel = s_mt_sel == (int8_t)i;
        lv_obj_t* r = mtRow(box, 24, mtEditRowCb, (void*)(uintptr_t)i);
        if (sel) {
          lv_obj_set_style_bg_color(r, lv_color_hex(sdInk()), LV_PART_MAIN);
          lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_MAIN);
        }
        if (used) snprintf(t, sizeof t, i == 0 ? "%u  %s (primary)" : "%u  %s", i, mtChanName(i));
        else snprintf(t, sizeof t, "%u  -- empty --", i);
        mtRowText(r, t, sel ? 0x000000 : (used ? sdInk() : sdDim()), LV_ALIGN_LEFT_MID, 2, 0);
        if (used && cfg().ch[i].keyLen == 0) mtRowText(r, "NO KEY", sel ? 0x000000 : TERM_AMBER, LV_ALIGN_RIGHT_MID, -2, 0);
      }
      lv_obj_t* note = mtLabel(box, "tap: select   hold: edit name/PSK", sdDim());
      lv_obj_set_style_pad_top(note, 4, LV_PART_MAIN);
      mtPinnedBtn(0, "[ ADD ]", mtAddCb);
      mtPinnedBtn(1, "[ REMOVE ]", mtRemoveCb);
      break;
    }
    case MTV_CHAN: case MTV_DM: chats: {
      if (s_net == NET_MC) {
        if (view == MTV_CHAN) snprintf(s_mt_title, sizeof s_mt_title, "CORE %s%s", s_mc_chan[0] == '#' ? "" : "#", s_mc_chan);
        else snprintf(s_mt_title, sizeof s_mt_title, "CORE DM %s", s_mc_peer);
      } else if (view == MTV_CHAN) snprintf(s_mt_title, sizeof s_mt_title, "TASTIC #%s", mtChanName(s_mt_chan));
      else { char nm[32]; Mt::nodeName(s_mt_peer, nm, sizeof nm); snprintf(s_mt_title, sizeof s_mt_title, "TASTIC DM %s", nm); }
      const lv_coord_t comp_h = 34;
      const lv_coord_t h = mtH() - top;
      s_mt_msgs = lv_obj_create(s_mt_root);
      lv_obj_remove_style_all(s_mt_msgs);
      lv_obj_set_size(s_mt_msgs, w - 8, h - comp_h - 6);
      lv_obj_set_pos(s_mt_msgs, 4, top);
      lv_obj_set_flex_flow(s_mt_msgs, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(s_mt_msgs, 3, LV_PART_MAIN);
      lv_obj_set_style_pad_all(s_mt_msgs, 3, LV_PART_MAIN);
      lv_obj_set_style_pad_right(s_mt_msgs, 8, LV_PART_MAIN);
      lv_obj_set_style_border_color(s_mt_msgs, lv_color_hex(sdDim()), LV_PART_MAIN);
      lv_obj_set_style_border_width(s_mt_msgs, 1, LV_PART_MAIN);
      mtStyleScroll(s_mt_msgs);
      const lv_coord_t send_w = 64;
      s_mt_ta = lv_textarea_create(s_mt_root);
      lv_obj_set_size(s_mt_ta, w - 8 - send_w - 4, comp_h - 4);
      lv_obj_set_pos(s_mt_ta, 4, top + h - comp_h);
      lv_textarea_set_one_line(s_mt_ta, true);
      lv_textarea_set_max_length(s_mt_ta, view == MTV_DM ? 160 : 200);
      lv_obj_set_style_text_font(s_mt_ta, &g_font_14, LV_PART_MAIN);
      useChainedFont(s_mt_ta);
      taSetPlaceholder(s_mt_ta, view == MTV_DM ? "Direct message..." : "Message channel...");
      attachSettingsTaEvents(s_mt_ta);
      lv_obj_add_event_cb(s_mt_ta, mtSendCb, LV_EVENT_READY, nullptr);
      lv_obj_t* sb = sdButton(s_mt_root, "[SEND]", mtSendCb, nullptr, send_w, comp_h - 4);
      lv_obj_set_pos(sb, w - 4 - send_w, top + h - comp_h);
      mtFillMsgs();
      break;
    }
  }
done:
  if (s_mt_scroll && keepY > 0) {
    lv_obj_update_layout(s_mt_scroll);
    lv_obj_scroll_to_y(s_mt_scroll, keepY, LV_ANIM_OFF);
  }
  appPageBeginSlim(s_mt_title, closeMtChats);   // one-row bar, "< title" closes a level
  s_mt_seq = Mt::changeSeq();
  s_mt_built_ms = millis();
}
// Back (the bar, or M) steps out one level: popup, then chat/list, then home,
// then the page closes.
static void closeMtChats() {
  if (s_mt_root && s_mt_pop) { mtPopDelete(); return; }
  if (s_mt_root && s_mt_view != MTV_HOME && s_mt_view != SV_LIST) {
    lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
    hideKb();
    mtShow(s_mt_view >= SV_LIST ? SV_LIST : s_mt_view == MTV_DM ? s_mt_dm_back : MTV_HOME);
    return;
  }
  hideKb();
  if (s_mt_timer) { lv_timer_del(s_mt_timer); s_mt_timer = nullptr; }
  s_mt_msgs = nullptr; s_mt_ta = nullptr; s_mt_scroll = nullptr;
  mtPopDelete();
  if (s_mt_root) popupClose(&s_mt_root);
  appPageEnd(closeMtChats);
  dashFill();   // HOME is back in view
}
// Whoever deletes the page root: the popup (on the top layer) goes with it.
static void mtRootDeleteCb(lv_event_t* e) {
  if (s_mt_root && lv_event_get_target(e) != s_mt_root) return;   // an old root that sdOpenPage replaced
  if (s_mt_pop && lv_obj_is_valid(s_mt_pop)) lv_obj_del_async(s_mt_pop);
  s_mt_pop = nullptr; s_mt_pop_name = nullptr; s_mt_pop_key = nullptr;
  s_mt_msgs = nullptr; s_mt_ta = nullptr; s_mt_scroll = nullptr;
}
static bool scoobyDashOpen() { return s_dash_root != nullptr; }
static bool scoobyMtOpen() { return s_mt_root != nullptr; }
// Open the page root on `view` (CORE / TASTIC home, or the SETUP list). An open
// page is replaced.
static void sdOpenPage(uint8_t net, uint8_t view) {
  if (s_mt_root) {
    if (s_mt_timer) { lv_timer_del(s_mt_timer); s_mt_timer = nullptr; }
    mtPopDelete();
    lv_obj_t* old = s_mt_root;
    s_mt_root = nullptr;
    lv_obj_del_async(old);
    s_mt_msgs = nullptr; s_mt_ta = nullptr; s_mt_scroll = nullptr;
  }
  s_net = net;
  s_mt_sel = -1;
  s_mt_root = appPageCreateRoot(COLOR_BG);
  lv_obj_add_event_cb(s_mt_root, mtRootDeleteCb, LV_EVENT_DELETE, nullptr);
  appPageBeginSlim(s_mt_title, closeMtChats);
  s_mt_view = view;
  mtShow(view);
  s_mt_timer = lv_timer_create(mtTimerCb, 700, nullptr);
}
static void sdOpenNet(uint8_t net) { sdOpenPage(net, MTV_HOME); }
static void sdOpenSetupPage() { sdOpenPage(NET_MT, SV_LIST); }
static void scoobyOpenMtChats() { sdOpenNet(NET_MT); }
// ============================================================================
// First-boot name setup (v1.2.1). Shown once, before the dashboard, when no
// DualMesh names are saved. LONG name = MeshCore node name + Meshtastic
// long name; SHORT name = Meshtastic short name (4 max). Done flag:
// /dm_setup.bin (DualMeshStore, SPIFFS), so a reflash does not bring it back.
// ============================================================================
static lv_obj_t* s_sds_root = nullptr;
static lv_obj_t* s_sds_long = nullptr;
static lv_obj_t* s_sds_short = nullptr;
static char      s_sds_def_long[40];
static char      s_sds_def_short[8];
static bool scoobySetupOpen() { return s_sds_root != nullptr; }

static void sdsMac(uint8_t mac[6]) { memset(mac, 0, 6); esp_read_mac(mac, ESP_MAC_WIFI_STA); }
// The Meshtastic node ID, "!a1b2c3d4": the last 4 MAC bytes, the same number
// Mt::begin uses, so it is stable across reflashes and factory resets.
static void sdsDeviceId(char* out, size_t cap) {
  uint8_t m[6]; sdsMac(m);
  snprintf(out, cap, "!%02x%02x%02x%02x", m[2], m[3], m[4], m[5]);
}
// MeshCore's own default name is the first 4 bytes of its public key in hex.
static bool sdsIsMcDefault(const char* n) {
  if (!n || !n[0] || !strcmp(n, "NONAME") || !strcmp(n, "Scooby Deck")) return true;
  if (strlen(n) != 8) return false;
  for (const char* p = n; *p; p++) if (!isxdigit((unsigned char)*p)) return false;
  return true;
}
static void sdsDefaults() {
  const char* mc = DualMesh::wadaNodeName();
  if (!sdsIsMcDefault(mc)) snprintf(s_sds_def_long, sizeof s_sds_def_long, "%.31s", mc);   // a name the user already gave MeshCore
  else sdsDeviceId(s_sds_def_long, sizeof s_sds_def_long);
  uint8_t m[6]; sdsMac(m);
  snprintf(s_sds_def_short, sizeof s_sds_def_short, "%02x%02x", m[4], m[5]);
}
static bool scoobySetupDoneFlag() {
  uint8_t v = 0;
  return DualMesh::Store::get("setup", &v, 1) == 1 && v == 1;
}
static void scoobySetupMarkDone() { const uint8_t v = 1; DualMesh::Store::put("setup", &v, 1); }
// Needed on a first boot. A device coming from v1.2.0 with names already set
// in Settings > Scooby Deck is marked done silently (its names are kept).
static bool scoobySetupNeeded() {
  if (scoobySetupDoneFlag()) return false;
  const DualMesh::Config& c = DualMesh::cfg();
  if (c.long_name[0] || c.short_name[0]) { scoobySetupMarkDone(); return false; }
  return true;
}

static void sdsTrim(char* s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, n - i + 1);
}
// Cut to `max` bytes without splitting a UTF-8 character.
static void sdsCut(char* s, size_t max) {
  if (strlen(s) <= max) return;
  size_t n = max;
  while (n && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}
static void sdsBind(lv_obj_t* ta) {
  if (!sdOk(ta)) return;
  kbMirrorBind(ta);   // the physical keyboard types into it
#if CAP_KEYPAD_NAV
  s_nav_ta_editing = true;
#endif
  if (s_nav_group && lv_obj_get_group(ta) == s_nav_group) lv_group_focus_obj(ta);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  if (sdOk(s_sds_long) && ta != s_sds_long) lv_obj_clear_state(s_sds_long, LV_STATE_FOCUSED);
  if (sdOk(s_sds_short) && ta != s_sds_short) lv_obj_clear_state(s_sds_short, LV_STATE_FOCUSED);
  lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}
static void sdsCloseAsync(void*) {
  if (s_sds_root && lv_obj_is_valid(s_sds_root)) lv_obj_del(s_sds_root);
  s_sds_root = s_sds_long = s_sds_short = nullptr;
  if (g_statusbar.root) lv_obj_clear_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);
  scoobyOpenDashboard();   // HOME (normally already open under the setup)
  dashFill();
}
static void sdsSave() {
  using namespace DualMesh;
  if (!s_sds_root) return;
  kbMirrorSyncToReal();
  char ln[64], sn[16];
  snprintf(ln, sizeof ln, "%s", sdTxt(s_sds_long));
  snprintf(sn, sizeof sn, "%s", sdTxt(s_sds_short));
  sdsTrim(ln); sdsTrim(sn);
  if (!ln[0]) snprintf(ln, sizeof ln, "%s", s_sds_def_long);
  if (!sn[0]) snprintf(sn, sizeof sn, "%s", s_sds_def_short);
  sdsCut(ln, 31);   // MeshCore keeps 31 bytes; Meshtastic would take 39
  sdsCut(sn, 4);    // Meshtastic short name: 4 max
  Config& c = cfg();
  snprintf(c.long_name, sizeof c.long_name, "%s", ln);
  snprintf(c.short_name, sizeof c.short_name, "%s", sn);
  save();                       // /dm_cfg.bin
  Mt::identityChanged();        // Meshtastic: NodeInfo with the new names on the next send
  if (g_lv.task) g_lv.task->setNodeName(ln);   // MeshCore node name (+ its prefs file)
  scoobySetupMarkDone();
  Serial.printf("[setup] names: long \"%s\" short \"%s\"\n", ln, sn);
  char b[80];
  snprintf(b, sizeof b, "Saved: %s / %s", ln, sn);
  if (g_lv.task) g_lv.task->showAlert(b, 1800);
  hideKb();
  lv_indev_t* a = lv_indev_get_act(); if (a) lv_indev_wait_release(a);
  lv_async_call(sdsCloseAsync, nullptr);   // never delete the screen inside its own event
}
static void sdsOkCb(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) sdsSave(); }
static void sdsDefaultsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_textarea_set_text(s_sds_long, s_sds_def_long);
  lv_textarea_set_text(s_sds_short, s_sds_def_short);
  sdsBind(s_sds_long);
}
// Enter in LONG goes to SHORT; Enter in SHORT saves. Defaults: Enter, Enter.
static void sdsReadyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_READY) return;
  if (lv_event_get_target(e) == s_sds_long) sdsBind(s_sds_short);
  else sdsSave();
}
static void sdsFocusCb(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_CLICKED || c == LV_EVENT_FOCUSED) sdsBind(lv_event_get_target(e));
}
// A printable key while no field is bound (focus on a button) types into LONG.
static bool scoobySetupTypeKey(uint32_t key) {
  if (!s_sds_root || key < 0x20 || key >= 0x7F || !sdOk(s_sds_long)) return false;
  sdsBind(s_sds_long);
  lv_textarea_add_char(s_sds_long, key);
  return true;
}
static lv_obj_t* sdsField(lv_coord_t x, lv_coord_t y, lv_coord_t w, uint32_t maxLen, const char* text) {
  lv_obj_t* ta = lv_textarea_create(s_sds_root);
  lv_obj_set_size(ta, w, 30);
  lv_obj_set_pos(ta, x, y);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, maxLen);
  lv_obj_set_style_text_font(ta, &g_font_14, LV_PART_MAIN);
  lv_obj_set_style_radius(ta, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ta, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_color_hex(sdDim()), LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_color_hex(sdInk()), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
  lv_obj_set_style_border_width(ta, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(ta, lv_color_hex(sdInk()), LV_PART_MAIN);
  attachSettingsTaEvents(ta);
  lv_obj_add_event_cb(ta, sdsFocusCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(ta, sdsFocusCb, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, sdsReadyCb, LV_EVENT_READY, nullptr);
  lv_textarea_set_text(ta, text);
  return ta;
}
static void scoobyOpenSetup() {
  if (s_sds_root) return;
  sdsDefaults();
  const DualMesh::Config& c = DualMesh::cfg();
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr), sh = lv_disp_get_ver_res(nullptr);
  if (g_statusbar.root) lv_obj_add_flag(g_statusbar.root, LV_OBJ_FLAG_HIDDEN);   // own the whole screen
  s_sds_root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sds_root);
  lv_obj_set_size(s_sds_root, sw, sh);
  lv_obj_set_pos(s_sds_root, 0, 0);
  lv_obj_set_style_bg_color(s_sds_root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_sds_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_sds_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_sds_root, LV_OBJ_FLAG_CLICKABLE);   // modal
  char t[96], id[16];
  sdsDeviceId(id, sizeof id);
  lv_obj_t* l = mtLabel(s_sds_root, "> SCOOBY DECK // FIRST BOOT SETUP", sdInk());
  lv_obj_set_pos(l, 6, 6);
  l = mtLabel(s_sds_root, "========================================", TERM_DIM);
  lv_obj_set_pos(l, 0, 16);
  snprintf(t, sizeof t, "DEVICE ID  %s   v%s", id, SCOOBY_DECK_VERSION);
  l = mtLabel(s_sds_root, t, sdDim());
  lv_obj_set_pos(l, 6, 28);
  l = mtLabel(s_sds_root, "LONG NAME   MeshCore + Meshtastic (31)", sdInk());
  lv_obj_set_pos(l, 6, 46);
  s_sds_long = sdsField(6, 58, sw - 12, 31, c.long_name[0] ? c.long_name : s_sds_def_long);
  l = mtLabel(s_sds_root, "SHORT NAME  Meshtastic (4 max)", sdInk());
  lv_obj_set_pos(l, 6, 98);
  s_sds_short = sdsField(6, 110, 96, 4, c.short_name[0] ? c.short_name : s_sds_def_short);
  snprintf(t, sizeof t, "default: %s", s_sds_def_short);
  l = mtLabel(s_sds_root, t, TERM_DIM);
  lv_obj_set_pos(l, 112, 121);
  l = mtLabel(s_sds_root,
              "Type to edit. ENTER: next field, then save.\n"
              "Keep the defaults: ENTER, ENTER.\n"
              "Change later: SETUP > DEVICE NAME.", sdDim());
  lv_obj_set_style_text_line_space(l, 4, LV_PART_MAIN);
  lv_obj_set_pos(l, 6, 152);
  const lv_coord_t bw = (sw - 8 - 4) / 2, by = sh - 4 - 26;
  lv_obj_t* b = sdButton(s_sds_root, "[ DEFAULTS ]", sdsDefaultsCb, nullptr, bw, 26);
  lv_obj_set_pos(b, 4, by);
  b = sdButton(s_sds_root, "[ OK ]", sdsOkCb, nullptr, bw, 26);
  lv_obj_set_pos(b, 4 + bw + 4, by);
  lv_obj_move_foreground(s_sds_root);
  sdsBind(s_sds_long);
}

// ============================================================================
// Lock screen (v2): the boot banner + credits until the trackball is held 3 s.
// Called from lockscreenShow() with its full-screen overlay root.
// ============================================================================
static void scoobyLockBuild(lv_obj_t* root) {
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_t* art = lv_label_create(root);
  lv_label_set_text(art, SCOOBY_BANNER);
  sdStyleTermLabel(art, &lv_font_unscii_8, TERM_GREEN);
  lv_obj_set_style_text_line_space(art, 2, LV_PART_MAIN);
  lv_obj_align(art, LV_ALIGN_CENTER, 0, -22);
  lv_obj_t* sub = lv_label_create(root);
  lv_label_set_text(sub, "> DUAL MESH v" SCOOBY_DECK_VERSION "  MESHCORE + MESHTASTIC\n" SCOOBY_CREDITS);
  sdStyleTermLabel(sub, &lv_font_unscii_8, TERM_MID);
  lv_obj_set_style_text_line_space(sub, 3, LV_PART_MAIN);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 62);
  lv_obj_t* hint = lv_label_create(root);
  lv_label_set_text(hint, "[ LOCKED ]  HOLD THE TRACKBALL 3 s_");
  sdStyleTermLabel(hint, &lv_font_unscii_8, TERM_AMBER);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ============================================================================
// Boot: HOME after the splash (once), then the first-boot names if needed.
// ============================================================================
static void scoobyBootDashboard() {
  static bool done = false;
  if (done) return;
  done = true;
  scoobyCpuBegin();
  v2uiLoad();
  v2uiApply(false);
  scoobyOpenDashboard();
  if (scoobySetupNeeded()) scoobyOpenSetup();
}

// Hotkeys with no text field being edited (UITask's key handler). C = CORE,
// T / M = TASTIC, P = SETUP, B / H = HOME (closes the page). True = handled.
static bool scoobyV2HotKey(int key) {
  if (s_sds_root) return false;
  const int lk = (key >= 'A' && key <= 'Z') ? key + 32 : key;
  switch (lk) {
    case 'c': sdOpenNet(NET_MC); return true;
    case 't': case 'm': sdOpenNet(NET_MT); return true;
    case 'p': sdOpenSetupPage(); return true;
    case 'b': case 'h':
      if (s_mt_root) { mtPopDelete(); if (s_mt_timer) { lv_timer_del(s_mt_timer); s_mt_timer = nullptr; }
                       s_mt_msgs = nullptr; s_mt_ta = nullptr; s_mt_scroll = nullptr;
                       popupClose(&s_mt_root); appPageEnd(closeMtChats); dashFill(); }
      return true;
    default: return false;
  }
}

// WadaMesh's Settings hooks: there are no WadaMesh settings pages in v2 (SETUP
// on HOME replaces them); kept as stubs so its category table still links.
static void scoobyBuildRadioMeshSection() {}
static void buildScoobySettings() {}
#endif  // SCOOBY_DECK
