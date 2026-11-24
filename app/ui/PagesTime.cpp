// Scooby Deck v3 — SETUP > TIME: 12/24 h clock, UTC offset, set the date and time by hand.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include "Ui.h"
#include "Clock.h"

namespace sd {

static const char* srcName(ClockSrc s) {
  switch (s) {
    case CLK_MANUAL:   return "SET BY HAND";
    case CLK_GPS:      return "GPS";
    case CLK_RESTORED: return "~ ESTIMATE";
    default:           return "NOT SET";
  }
}

static void offsetText(int8_t q, char* out, size_t cap) {
  const int m = q * 15, a = m < 0 ? -m : m;
  if (a % 60) snprintf(out, cap, "UTC%c%d:%02d", m < 0 ? '-' : '+', a / 60, a % 60);
  else snprintf(out, cap, "UTC%c%d", m < 0 ? '-' : '+', a / 60);
}

// ---------------------------------------------------------------- parsing
// "2026-11-24" (also 2026/11/24)
static bool parseDate(const char* s, DateTime& d) {
  unsigned y, m, dd;
  if (sscanf(s, "%u-%u-%u", &y, &m, &dd) != 3 && sscanf(s, "%u/%u/%u", &y, &m, &dd) != 3) return false;
  static const uint8_t dim[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (y < 2026 || y > 2099 || m < 1 || m > 12 || dd < 1 || dd > dim[m - 1]) return false;
  if (m == 2 && dd == 29 && !((y % 4 == 0 && y % 100) || y % 400 == 0)) return false;
  d.year = y; d.month = m; d.day = dd;
  return true;
}
// "15:42", "3:42PM", "3:42 pm", "12:05am"
static bool parseTime(const char* s, DateTime& d) {
  unsigned h, mi;
  if (sscanf(s, "%u:%u", &h, &mi) != 2 || mi > 59) return false;
  bool am = false, pm = false;
  for (const char* p = s; *p; p++) {
    const char c = toupper((unsigned char)*p);
    if (c == 'A') am = true;
    if (c == 'P') pm = true;
  }
  if (am || pm) {
    if (h < 1 || h > 12) return false;
    h %= 12;
    if (pm) h += 12;
  } else if (h > 23) return false;
  d.hour = h; d.minute = mi; d.second = 0;
  return true;
}
// "-5", "+5.5", "-3:30", "5.75"
static bool parseOffset(const char* s, int8_t& q) {
  while (*s == ' ') s++;
  int sign = 1;
  if (*s == '-' || *s == '+') { if (*s == '-') sign = -1; s++; }
  if (!isdigit((unsigned char)*s)) return false;
  const int h = atoi(s);
  int mins = 0;
  const char* c = strchr(s, ':');
  const char* dot = strchr(s, '.');
  if (c) mins = atoi(c + 1);
  else if (dot) mins = (int)(atof(dot) * 60.0f + 0.5f);
  if (mins % 15 || mins >= 60) return false;
  const int total = sign * (h * 60 + mins);
  if (total < -12 * 60 || total > 14 * 60) return false;
  q = (int8_t)(total / 15);
  return true;
}

// ---------------------------------------------------------------- popups
static lv_obj_t* s_fDate = nullptr;
static lv_obj_t* s_fTime = nullptr;
static lv_obj_t* s_fOff = nullptr;

static void setApply() {
  DateTime d;
  if (!parseDate(lv_textarea_get_text(s_fDate), d)) { toast("Date: YYYY-MM-DD, e.g. 2026-11-24"); return; }
  if (!parseTime(lv_textarea_get_text(s_fTime), d)) { toast("Time: 15:42 or 3:42PM"); return; }
  popupClose();
  clockSetLocal(d);
  toast("Time set");
}
static void dateReadyCb(lv_event_t*) { lv_group_focus_obj(s_fTime); lv_group_set_editing(group(), true); }
static void timeReadyCb(lv_event_t*) { setApply(); }
static void setOkCb(lv_event_t*) { setApply(); }
static void setTimePopup() {
  char ds[16], ts[12];
  if (clockSource() != CLK_NONE) {
    const DateTime d = clockLocal();
    snprintf(ds, sizeof ds, "%04u-%02u-%02u", d.year, d.month, d.day);
    if (prefs().clock24) snprintf(ts, sizeof ts, "%02u:%02u", d.hour, d.minute);
    else snprintf(ts, sizeof ts, "%u:%02u%s", d.hour % 12 ? d.hour % 12 : 12, d.minute, d.hour < 12 ? "AM" : "PM");
  } else {
    snprintf(ds, sizeof ds, "2026-11-24");
    snprintf(ts, sizeof ts, prefs().clock24 ? "12:00" : "12:00PM");
  }
  lv_obj_t* p = popup("SET DATE & TIME", 176);
  lv_obj_t* l1 = label(p, "DATE  YYYY-MM-DD", FONT_M, pal().dim);
  lv_obj_set_pos(l1, 0, 22);
  s_fDate = textField(p, 0, 40, 288, ds, 10);
  lv_textarea_set_accepted_chars(s_fDate, "0123456789-/");
  lv_obj_t* l2 = label(p, prefs().clock24 ? "TIME  HH:MM (24 h)" : "TIME  H:MM AM/PM", FONT_M, pal().dim);
  lv_obj_set_pos(l2, 0, 68);
  s_fTime = textField(p, 0, 86, 288, ts, 8);
  lv_textarea_set_accepted_chars(s_fTime, "0123456789:APMapm ");
  lv_obj_add_event_cb(s_fDate, dateReadyCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_fTime, timeReadyCb, LV_EVENT_READY, nullptr);
  popupButton(p, 0, 2, "[ SET ]", setOkCb);
  popupButton(p, 1, 2, "[ CANCEL ]", [](lv_event_t*) { popupClose(); });
  lv_group_focus_obj(s_fDate);
  lv_group_set_editing(group(), true);
}

static void offApply() {
  int8_t q;
  if (!parseOffset(lv_textarea_get_text(s_fOff), q)) { toast("UTC offset: -5, +5.5 or -3:30 (-12 .. +14)"); return; }
  popupClose();
  // keep the moment (UTC) and show it in the new zone
  prefs().tz_q = q;
  savePrefs();
  toast("UTC offset saved");
}
static void offOkCb(lv_event_t*) { offApply(); }
static void offsetPopup() {
  char cur[12];
  const int m = prefs().tz_q * 15, a = m < 0 ? -m : m;
  if (a % 60) snprintf(cur, sizeof cur, "%c%d:%02d", m < 0 ? '-' : '+', a / 60, a % 60);
  else snprintf(cur, sizeof cur, "%c%d", m < 0 ? '-' : '+', a / 60);
  lv_obj_t* p = popup("UTC OFFSET", 140);
  lv_obj_t* h = label(p, "Hours from UTC: New York -5\n(-4 in summer), London 0", FONT_M, pal().dim);
  lv_obj_set_pos(h, 0, 22);
  s_fOff = textField(p, 0, 60, 288, cur, 6);
  lv_textarea_set_accepted_chars(s_fOff, "0123456789+-.:");
  lv_obj_add_event_cb(s_fOff, offOkCb, LV_EVENT_READY, nullptr);
  popupButton(p, 0, 2, "[ OK ]", offOkCb);
  popupButton(p, 1, 2, "[ CANCEL ]", [](lv_event_t*) { popupClose(); });
  lv_group_focus_obj(s_fOff);
  lv_group_set_editing(group(), true);
}

// ---------------------------------------------------------------- page
void buildSetTime(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  char r[32], c[12];
  section(l, "CLOCK");
  clockHeader(c, sizeof c);
  if (clockSource() != CLK_NONE) {
    const DateTime d = clockLocal();
    snprintf(r, sizeof r, "%04u-%02u-%02u %s", d.year, d.month, d.day, c);
  } else snprintf(r, sizeof r, "--");
  row(l, "NOW", r, nullptr);
  lv_obj_t* first = row(l, "FORMAT", prefs().clock24 ? "24 H" : "12 H", [](lv_event_t*) {
    prefs().clock24 = !prefs().clock24;
    savePrefs();
    go(P_SET_TIME);
  });
  row(l, "SET DATE & TIME", ">", [](lv_event_t*) { setTimePopup(); });
  offsetText(prefs().tz_q, r, sizeof r);
  row(l, "UTC OFFSET", r, [](lv_event_t*) { offsetPopup(); });
  row(l, "SOURCE", srcName(clockSource()), nullptr);
  lv_obj_t* h = label(l,
      "No battery-backed clock on the T-Deck\n"
      "Plus: time runs while the deck is on\n"
      "(sleep and lock too). After power-off\n"
      "it starts from the last saved time\n"
      "(saved every 10 min) and shows ~ until\n"
      "you set it again. GPS time: later.",
      FONT_S, pal().dim);
  lv_obj_set_width(h, lv_pct(100));
  lv_obj_set_style_text_line_space(h, 3, 0);
  lv_group_focus_obj(first);
}

}  // namespace sd
