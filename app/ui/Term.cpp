// Scooby Deck v3 — terminal widgets, navigation, toasts, popups and the lock overlay.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include "Ui.h"
#include "DualMeshStore.h"
#include "ScoobyVersion.h"
#include "Clock.h"
#include "Favs.h"

LV_FONT_DECLARE(sd_font_term16);
LV_FONT_DECLARE(sd_font_term32);

namespace sd {

// ============================================================== theme
static const Palette kSchemes[] = {
  //  bg        ink       dim       line      focusBg   focusFg   pressBg   pressFg   warn      ok        err
  { 0x000000, 0x33FF66, 0x1E8C3C, 0x33FF66, 0x33FF66, 0x000000, 0x1E8C3C, 0x000000, 0xFFB000, 0x33FF66, 0xFF5050, "CLASSIC CYBER" },
  // night vision: only dim, warm reds (no white, no green, no blue)
  { 0x000000, 0xC8321E, 0x9A2A18, 0x8C2416, 0xB42D1A, 0x000000, 0x4A120A, 0xC8321E, 0xD2601E, 0xC8321E, 0xFF3C1E, "NIGHT MODE" },
  // WIN98: silver face, navy text, navy selection with white text
  { 0xC0C0C0, 0x000080, 0x404040, 0x000080, 0x000080, 0xFFFFFF, 0x808080, 0xFFFFFF, 0x800000, 0x006400, 0xA00000, "WIN98" },
  { 0xFFFFFF, 0x000000, 0x4A4A4A, 0x000000, 0x000000, 0xFFFFFF, 0x808080, 0xFFFFFF, 0xA00000, 0x006400, 0xA00000, "HIGH CONTRAST" },
};
uint8_t schemeCount() { return sizeof kSchemes / sizeof kSchemes[0]; }
const char* schemeName(uint8_t i) { return kSchemes[i < schemeCount() ? i : 0].name; }
const Palette& pal() { return kSchemes[prefs().scheme < schemeCount() ? prefs().scheme : 0]; }
const lv_font_t* const FONT_S = &lv_font_unscii_8;
const lv_font_t* const FONT_M = &sd_font_term16;   // 8x16: 40 columns, v2 used 8x8, v3.0 16x16
const lv_font_t* const FONT_L = &sd_font_term32;

// ============================================================== prefs
static UiPrefs s_prefs;
UiPrefs& prefs() { return s_prefs; }
void loadPrefs() {
  UiPrefs p;
  memset(&p, 0, sizeof p);
  const long n = DualMesh::Store::get("ui3", &p, sizeof p);
  if (n >= 8 && p.version == 1) {   // v3 nightly before the clock / battery settings
    p.version = 2; p.clock24 = 0; p.tz_q = -20; p.pad = 0; p.batt_full_mv = 0;
  } else if (n < 8 || p.version != 2) {
    memset(&p, 0, sizeof p);
    p.version = 2; p.brightness = 70; p.sleep_s = 60; p.kb_bl = 127; p.lock_on = 0;
    p.names_done = 0; p.scheme = 0; p.mc_preset = 0;
    p.clock24 = 0; p.tz_q = -20;     // 12 h, UTC-5 (US Eastern standard time)
  }
  if (p.tz_q < -48 || p.tz_q > 56) p.tz_q = 0;
  if (p.scheme >= schemeCount()) p.scheme = 0;   // unknown / older: CLASSIC CYBER
  if (p.brightness < 10) p.brightness = 10;
  if (p.brightness > 100) p.brightness = 100;
  s_prefs = p;
}
void savePrefs() { DualMesh::Store::put("ui3", &s_prefs, sizeof s_prefs); }

// ============================================================== navigation
Ctx ctx;
static lv_group_t* s_group = nullptr;
static Page s_page = P_NONE, s_pending = P_NONE;
static bool s_rebuild = false;
static int s_focusIdx = -1;
static bool s_keyNav = false;   // the user is steering with the trackball (not touch)      // focused object's index in the group (no popup open)
static lv_obj_t* s_body = nullptr;
static lv_obj_t* s_hdrRight = nullptr;
static lv_obj_t* s_hdrTitle = nullptr;
static uint32_t s_sig = 0, s_lastRefresh = 0, s_lastActivity = 0;

struct PageDef { const char* title; void (*build)(lv_obj_t*); Page parent; bool mesh; };
static const PageDef kPages[P_COUNT] = {
  /* P_NONE        */ { "",          nullptr,         P_HOME,   false },
  /* P_BOOT        */ { nullptr,     buildBoot,       P_HOME,   false },
  /* P_NAMES_FIRST */ { "FIRST BOOT",buildNamesFirst, P_HOME,   false },
  /* P_HOME        */ { nullptr,     buildHome,       P_HOME,   false },
  /* P_CORE        */ { "CORE  MeshCore",   buildCore,      P_HOME,  true },
  /* P_CORE_DMS    */ { "CORE  DMs",        buildCoreDms,   P_CORE,  true },
  /* P_CORE_EDIT   */ { "CORE  CHANNELS",   buildCoreEdit,  P_CORE,  false },
  /* P_CORE_CHAT   */ { "CORE",             buildCoreChat,  P_CORE,  true },
  /* P_CORE_HEARD  */ { "CORE  LAST HEARD", buildCoreHeard, P_CORE,  true },
  /* P_TASTIC      */ { "TASTIC  Meshtastic", buildTastic,  P_HOME,  true },
  /* P_MT_DMS      */ { "TASTIC  DMs",      buildMtDms,     P_TASTIC, true },
  /* P_MT_EDIT     */ { "TASTIC  CHANNELS", buildMtEdit,    P_TASTIC, false },
  /* P_MT_CHAT     */ { "TASTIC",           buildMtChat,    P_TASTIC, true },
  /* P_MT_HEARD    */ { "TASTIC  LAST HEARD", buildMtHeard, P_TASTIC, true },
  /* P_SETUP       */ { "SETUP",            buildSetup,     P_HOME,  false },
  /* P_SET_NAME    */ { "SETUP  DEVICE NAME", buildSetName, P_SETUP, false },
  /* P_SET_MT      */ { "SETUP  MESHTASTIC",  buildSetMt,   P_SETUP, false },
  /* P_SET_MC      */ { "SETUP  MESHCORE",    buildSetMc,   P_SETUP, false },
  /* P_SET_DISPLAY */ { "SETUP  DISPLAY",     buildSetDisplay, P_SETUP, false },
  /* P_SET_BATTERY */ { "SETUP  BATTERY",     buildSetBattery, P_SETUP, false },
  /* P_CHARGING    */ { nullptr,              buildCharging,   P_SET_BATTERY, false },
  /* P_SET_TIME    */ { "SETUP  TIME",        buildSetTime,    P_SETUP, false },
};

lv_group_t* group() { return s_group; }
Page page() { return s_page; }
void go(Page p) { s_pending = p; s_rebuild = true; }
void back() {
  if (popupOpen()) { popupClose(); return; }
  if (s_page == P_BOOT || s_page == P_NAMES_FIRST || s_page == P_HOME) return;
  go(kPages[s_page].parent);
}
lv_obj_t* body() { return s_body; }
lv_obj_t* headerRight() { return s_hdrRight; }
void setTitle(const char* title) {
  if (!s_hdrTitle) return;
  char t[48];
  snprintf(t, sizeof t, "< %s", title);
  lv_label_set_text(s_hdrTitle, t);
}
void setHeaderText(const char* text) { if (s_hdrTitle) lv_label_set_text(s_hdrTitle, text); }
void noteActivity() { s_lastActivity = millis(); }
void themeResetDefault() {
  if (prefs().scheme == 0) return;
  prefs().scheme = 0;
  savePrefs();
  s_pending = s_page; s_rebuild = true;   // redraw in CLASSIC CYBER
  toast("Theme reset: CLASSIC CYBER", 2000);
}
bool keyNav() { return s_keyNav; }
void setKeyNav(bool on) { s_keyNav = on; }
bool showFocus() {
  // make the (programmatic) focus visible; false if it already was or there is none
  lv_obj_t* f = s_group ? lv_group_get_focused(s_group) : nullptr;
  if (!f || lv_obj_has_state(f, LV_STATE_FOCUS_KEY)) return false;
  lv_obj_add_state(f, LV_STATE_FOCUS_KEY);
  lv_obj_scroll_to_view_recursive(f, LV_ANIM_OFF);
  return true;
}

bool typing() {
  lv_obj_t* f = s_group ? lv_group_get_focused(s_group) : nullptr;
  return f && lv_obj_check_type(f, &lv_textarea_class);
}

// ============================================================== styles
static void baseStyle(lv_obj_t* o) {
  lv_obj_remove_style_all(o);
  lv_obj_set_style_text_font(o, FONT_M, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(pal().ink), 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(pal().bg), 0);
  lv_obj_set_style_border_color(o, lv_color_hex(pal().line), 0);
}
void styleFocus(lv_obj_t* o) {
  // Highlight only what the trackball / keys point at (FOCUS_KEY), not the group's default
  // focus: the HOME default (CORE) looked selected all the time, and a touch left it lit.
  const lv_style_selector_t foc = LV_PART_MAIN | LV_STATE_FOCUS_KEY;
  const lv_style_selector_t prs = LV_PART_MAIN | LV_STATE_PRESSED;
  lv_obj_set_style_bg_color(o, lv_color_hex(pal().focusBg), foc);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, foc);
  lv_obj_set_style_text_color(o, lv_color_hex(pal().focusFg), foc);
  lv_obj_set_style_bg_color(o, lv_color_hex(pal().pressBg), prs);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, prs);
  lv_obj_set_style_text_color(o, lv_color_hex(pal().pressFg), prs);
}
static void scrollStyle(lv_obj_t* o) {
  lv_obj_set_style_bg_color(o, lv_color_hex(pal().line), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(o, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(o, 1, LV_PART_SCROLLBAR);
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f ? f : FONT_M, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color == 0xFFFFFFFF ? pal().ink : color), 0);
  lv_label_set_text(l, text ? text : "");
  return l;
}

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* b = lv_obj_create(parent);
  baseStyle(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_pad_all(b, 3, 0);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = lv_button_create(parent);
  baseStyle(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  styleFocus(b);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  if (s_group) lv_group_add_obj(s_group, b);
  return b;
}

lv_obj_t* list(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* l = lv_obj_create(parent);
  baseStyle(l);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, w, h);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_pad_all(l, 3, 0);
  lv_obj_set_style_pad_row(l, 1, 0);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(l, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_AUTO);
  scrollStyle(l);
  return l;
}

lv_obj_t* section(lv_obj_t* lst, const char* title) {
  char b[48];
  snprintf(b, sizeof b, "> %s", title);
  lv_obj_t* l = label(lst, b);
  lv_obj_set_width(l, lv_pct(100));
  return l;
}

lv_obj_t* row(lv_obj_t* lst, const char* left, const char* right, lv_event_cb_t cb, void* ud, bool hot) {
  lv_obj_t* r = lv_obj_create(lst);
  baseStyle(r);
  lv_obj_set_size(r, lv_pct(100), 18);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(r, 2, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  styleFocus(r);
  lv_obj_t* l = lv_label_create(r);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_label_set_text(l, left ? left : "");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  if (right && right[0]) {
    lv_obj_t* rr = lv_label_create(r);
    lv_label_set_text(rr, right);
    lv_obj_align(rr, LV_ALIGN_RIGHT_MID, 0, 0);
    if (hot) {   // unread counter: inverted
      lv_obj_set_style_bg_color(rr, lv_color_hex(pal().ink), 0);
      lv_obj_set_style_bg_opa(rr, LV_OPA_COVER, 0);
      lv_obj_set_style_text_color(rr, lv_color_hex(pal().bg), 0);
    } else {
      // no own colour: it inherits the row's text colour (ink, or focusFg / pressFg when the
      // row is selected / pressed) and is just fainter, so it stays readable on every theme
      lv_obj_set_style_text_opa(rr, LV_OPA_70, 0);
    }
    lv_obj_update_layout(rr);
    lv_obj_set_width(l, 300 - lv_obj_get_width(rr) - 12);
  } else {
    lv_obj_set_width(l, lv_pct(100));
  }
  if (cb) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    if (s_group) lv_group_add_obj(s_group, r);
  }
  return r;
}

lv_obj_t* textField(lv_obj_t* parent, int x, int y, int w, const char* text, size_t maxLen) {
  lv_obj_t* t = lv_textarea_create(parent);
  baseStyle(t);
  lv_obj_set_pos(t, x, y);
  lv_obj_set_size(t, w, 24);
  lv_textarea_set_one_line(t, true);
  lv_textarea_set_max_length(t, maxLen);
  lv_textarea_set_text(t, text ? text : "");
  lv_obj_set_style_border_width(t, 1, 0);
  lv_obj_set_style_border_color(t, lv_color_hex(pal().dim), 0);
  lv_obj_set_style_border_color(t, lv_color_hex(pal().line), LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(t, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_pad_all(t, 3, 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  // block cursor, blinking
  lv_obj_set_style_bg_color(t, lv_color_hex(pal().line), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_anim_duration(t, 500, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(t, lv_color_hex(pal().dim), LV_PART_TEXTAREA_PLACEHOLDER);
  if (s_group) lv_group_add_obj(s_group, t);
  return t;
}

void ageText(uint32_t s, char* out, size_t cap) {
  if (s < 60) snprintf(out, cap, "%us", (unsigned)s);
  else if (s < 3600) snprintf(out, cap, "%um", (unsigned)(s / 60));
  else if (s < 86400) snprintf(out, cap, "%uh", (unsigned)(s / 3600));
  else snprintf(out, cap, "%ud", (unsigned)(s / 86400));
}

// ============================================================== frame / header
static void backCb(lv_event_t*) { back(); }
static const int kHdrRightW = 13 * 8;   // "~12:34PM 100%"

lv_obj_t* frame(const char* title, bool backArrow) {
  lv_obj_t* scr = lv_screen_active();
  if (s_group) lv_group_remove_all_objs(s_group);
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(pal().bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  s_hdrRight = nullptr;
  s_hdrTitle = nullptr;
  int top = 0;
  if (title) {
    lv_obj_t* hdr = lv_obj_create(scr);
    baseStyle(hdr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, 320, 20);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    char t[48];
    snprintf(t, sizeof t, "%s%s", backArrow ? "< " : "> ", title);
    // right: clock + battery ("12:34PM 87%" = 11 columns); the title gets the rest and is
    // clipped instead of running under it
    s_hdrRight = label(hdr, "", FONT_M);
    lv_obj_align(s_hdrRight, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_t* l = label(hdr, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, 320 - 4 - kHdrRightW - 8);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 2, 0);
    s_hdrTitle = l;
    if (backArrow) {
      lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(l, 8);
      lv_obj_add_event_cb(l, backCb, LV_EVENT_CLICKED, nullptr);
    }
    top = 22;
  }
  lv_obj_t* b = lv_obj_create(scr);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, 0, top);
  lv_obj_set_size(b, 320, 240 - top);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  s_body = b;
  return b;
}

static void updateHeaderRight() {
  if (!s_hdrRight) return;
  char c[12], b[24];
  clockHeader(c, sizeof c);
  snprintf(b, sizeof b, "%s %u%%", c, (unsigned)battPct());
  lv_label_set_text(s_hdrRight, b);
  lv_obj_align(s_hdrRight, LV_ALIGN_RIGHT_MID, -2, 0);
}

// ============================================================== toast
static lv_obj_t* s_toast = nullptr;
static uint32_t s_toastUntil = 0;
void toast(const char* text, uint32_t ms) {
  if (s_toast) { lv_obj_delete(s_toast); s_toast = nullptr; }
  s_toast = lv_obj_create(lv_layer_top());
  baseStyle(s_toast);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_toast, 2, 0);
  lv_obj_set_style_pad_all(s_toast, 6, 0);
  lv_obj_set_width(s_toast, 300);
  lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* l = label(s_toast, text);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  s_toastUntil = millis() + ms;
}

// ============================================================== popups
static lv_obj_t* s_popup = nullptr;
static lv_obj_t* s_popupPrevFocus = nullptr;
bool popupOpen() { return s_popup != nullptr; }
void popupClose() {
  if (!s_popup) return;
  lv_obj_delete_async(s_popup);
  s_popup = nullptr;
  // the page underneath is rebuilt so its focus chain is complete again
  s_pending = s_page; s_rebuild = true;
}
lv_obj_t* popup(const char* title, int h) {
  // may run inside an event of the old popup's own button: delete it later, not now
  if (s_popup) { lv_obj_delete_async(s_popup); s_popup = nullptr; }
  s_popupPrevFocus = s_group ? lv_group_get_focused(s_group) : nullptr;
  if (s_group) lv_group_remove_all_objs(s_group);
  s_popup = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_popup);
  lv_obj_set_size(s_popup, 320, 240);
  lv_obj_set_style_bg_color(s_popup, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_popup, LV_OPA_70, 0);
  lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);   // swallow taps underneath
  lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* card = lv_obj_create(s_popup);
  baseStyle(card);
  lv_obj_set_size(card, 300, h);
  lv_obj_center(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_pad_all(card, 4, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  char t[48];
  snprintf(t, sizeof t, "> %s", title);
  lv_obj_t* l = label(card, t);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);   // long node names: clip at the card edge
  lv_obj_set_width(l, 288);
  lv_obj_set_pos(l, 0, 0);
  return card;
}
lv_obj_t* popupButton(lv_obj_t* card, int slot, int slots, const char* text, lv_event_cb_t cb, void* ud) {
  const int w = (290 - (slots - 1) * 4) / slots;
  lv_obj_t* b = button(card, text, slot * (w + 4), 0, w, 26, cb, ud);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, slot * (w + 4), 0);
  return b;
}

struct ConfirmCtx { void (*fn)(void*); void* ud; };
static ConfirmCtx s_confirm;
static void confirmYesCb(lv_event_t*) {
  ConfirmCtx c = s_confirm;
  popupClose();
  if (c.fn) c.fn(c.ud);
}
static void closeCb(lv_event_t*) { popupClose(); }
void confirm(const char* question, const char* yes, void (*onYes)(void*), void* ud) {
  s_confirm = { onYes, ud };
  lv_obj_t* c = popup("CONFIRM", 120);
  lv_obj_t* q = label(c, question);
  lv_obj_set_width(q, 288);
  lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(q, 0, 22);
  popupButton(c, 0, 2, yes, confirmYesCb);
  lv_obj_t* no = popupButton(c, 1, 2, "[ CANCEL ]", closeCb);
  lv_group_focus_obj(no);
}

// ============================================================== lock overlay
static lv_obj_t* s_lock = nullptr;
static lv_obj_t* s_lockHint = nullptr;
static const char* const kLockText = "LOCKED\nhold trackball 3s";
bool locked() { return s_lock != nullptr; }
void lockEngage() {
  if (s_lock) return;
  if (s_popup) { lv_obj_delete(s_popup); s_popup = nullptr; }
  s_lock = lv_obj_create(lv_layer_top());
  baseStyle(s_lock);
  lv_obj_set_size(s_lock, 320, 240);
  lv_obj_set_style_bg_opa(s_lock, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE);    // swallow touches
  lv_obj_remove_flag(s_lock, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* art = label(s_lock, SCOOBY_BANNER, FONT_S);
  lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_t* v = label(s_lock, "v" SCOOBY_DECK_VERSION, FONT_S, pal().dim);
  lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 110);
  lv_obj_t* cr = label(s_lock, SCOOBY_CREDITS, FONT_S, pal().dim);
  lv_obj_align(cr, LV_ALIGN_TOP_MID, 0, 132);
  // two short centred lines (8x16 font: 40 columns max); the M4 build's one 16x16 line was
  // 400 px wide and lost its left edge on the 320 px panel
  s_lockHint = label(s_lock, kLockText, FONT_M, pal().warn);
  lv_obj_set_width(s_lockHint, 320);
  lv_obj_set_style_text_align(s_lockHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_lockHint, LV_ALIGN_BOTTOM_MID, 0, -6);
  if (s_group) lv_group_remove_all_objs(s_group);
}
void lockHold(uint32_t held) {
  if (!s_lock || !s_lockHint) return;
  if (held == 0) { lv_label_set_text(s_lockHint, kLockText); return; }
  if (held >= 3000) {
    lv_obj_delete(s_lock);
    s_lock = nullptr; s_lockHint = nullptr;
    s_pending = s_page; s_rebuild = true;   // focus chain back
    return;
  }
  char b[40];
  snprintf(b, sizeof b, "UNLOCKING %u\nkeep holding", (unsigned)((3000 - held + 999) / 1000));
  lv_label_set_text(s_lockHint, b);
}

// ============================================================== hotkeys
static uint32_t s_spaceAt = 0;
static lv_obj_t* s_spacePop = nullptr;
static void spaceCancel() {
  s_spaceAt = 0;
  if (s_spacePop) { lv_obj_delete(s_spacePop); s_spacePop = nullptr; }
}
bool handleKey(uint32_t key) {
  if (s_spaceAt && key != ' ') { spaceCancel(); return true; }
  if (locked()) return true;
  if (typing()) return false;
  if (key >= 'A' && key <= 'Z') key += 32;
  if (s_page == P_BOOT && (key == 0x08 || key == 0x7F)) { themeResetDefault(); return true; }
  if (s_page == P_BOOT || s_page == P_NAMES_FIRST) return false;
  if (s_page == P_CHARGING) {   // only the trackball (or B / Esc) leaves; everything else is ignored
    if (key == 'b' || key == 0x08 || key == 0x1B) back();
    return true;
  }
  if (popupOpen()) {
    if (key == 'b' || key == 0x08 || key == 0x1B) { popupClose(); return true; }
    return false;
  }
  switch (key) {
    case 'c': go(P_CORE); return true;
    case 't': go(P_TASTIC); return true;
    case 'p': case 's': go(P_SETUP); return true;
    case 'h': go(P_HOME); return true;
    case 'b': case 0x08: case 0x1B: back(); return true;
    case ' ':
      // Space: a 1 s "LOCKING" countdown, then screen + keyboard light off and the lock screen.
      // The T-Deck keyboard sends one byte per press (no key-up), so a true "hold" can't be
      // measured; any other key or a tap cancels.
      if (!s_spaceAt) {
        s_spaceAt = millis();
        s_spacePop = lv_obj_create(lv_layer_top());
        baseStyle(s_spacePop);
        lv_obj_set_size(s_spacePop, 200, 50);
        lv_obj_center(s_spacePop);
        lv_obj_set_style_bg_opa(s_spacePop, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_spacePop, 2, 0);
        lv_obj_t* l = label(s_spacePop, "> LOCKING...");
        lv_obj_center(l);
      }
      return true;
    default: return false;
  }
}

// ============================================================== begin / tick
void begin() {
  loadPrefs();
  mtFavCount();   // load the favorite list before the radio asks for it
  s_group = lv_group_create();
  lv_group_set_wrap(s_group, true);
  lv_group_set_default(s_group);
  go(P_BOOT);
}

void tick(uint32_t now) {
  if (s_spaceAt && now - s_spaceAt >= 1000) {
    spaceCancel();
    lockEngage();
    halSleepNow(true);
  }
  if (s_toast && (int32_t)(now - s_toastUntil) >= 0) { lv_obj_delete(s_toast); s_toast = nullptr; }
  if (s_rebuild && !locked()) {
    s_rebuild = false;
    const Page p = s_pending;
    if (p != P_NONE && kPages[p].build) {
      // remember which row had focus so a same-page refresh (cycling a setting) keeps it
      const bool same = (p == s_page);
      const int keep = (same && p != P_CORE_CHAT && p != P_MT_CHAT) ? s_focusIdx : -1;
      if (s_popup) { lv_obj_delete(s_popup); s_popup = nullptr; }
      if (s_page == P_CHARGING && p != P_CHARGING) halChargingMode(false);   // brightness + sleep back
      if (p == P_CHARGING && s_page != P_CHARGING) halChargingMode(true);
      s_page = p;
      lv_obj_t* b = frame(p == P_HOME ? "SCOOBY DECK" : kPages[p].title,
                          p != P_HOME && p != P_BOOT && p != P_NAMES_FIRST);
      kPages[p].build(b);
      if (keep > 0 && (uint32_t)keep < lv_group_get_obj_count(group())) {
        lv_obj_t* o = lv_group_get_obj_by_index(group(), keep);
        if (o && o != lv_group_get_focused(group())) {
          lv_group_focus_obj(o);
          lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
        }
      }
      if (s_keyNav) showFocus();   // rebuilt while steering: keep the highlight on
      s_sig = kPages[p].mesh ? meshSignature(p) : 0;
      updateHeaderRight();
    }
  }
  if (!popupOpen() && !locked()) {
    lv_obj_t* f = lv_group_get_focused(group());
    s_focusIdx = -1;
    const uint32_t n = lv_group_get_obj_count(group());
    for (uint32_t i = 0; f && i < n; i++) if (lv_group_get_obj_by_index(group(), i) == f) { s_focusIdx = (int)i; break; }
  }
  if (now - s_lastRefresh >= 1000) {
    s_lastRefresh = now;
    updateHeaderRight();
    if (s_page == P_HOME) refreshHome(now);
    else if (s_page == P_BOOT) refreshBoot(now);
    else if (s_page == P_CHARGING) refreshCharging(now);
    else if (kPages[s_page].mesh && !popupOpen() && !locked()) {
      // rebuild a mesh page when its data changed, unless the user is typing a message
      const uint32_t sig = meshSignature(s_page);
      if (sig != s_sig) {
        s_sig = sig;
        if (!typing() || s_page == P_CORE_CHAT || s_page == P_MT_CHAT) { s_pending = s_page; s_rebuild = true; }
      }
    }
  }
}

}  // namespace sd
