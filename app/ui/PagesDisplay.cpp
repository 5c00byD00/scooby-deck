// Scooby Deck v3 — SETUP > DISPLAY: brightness, sleep timer, keyboard light, lock screen.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include "Ui.h"

namespace sd {

static void brightCb(lv_event_t*) {
  UiPrefs& p = prefs();
  p.brightness = p.brightness >= 100 ? 10 : (uint8_t)(p.brightness + 10);
  savePrefs();
  halSetBrightness(p.brightness);   // live
  go(P_SET_DISPLAY);
}

static const uint16_t kSleep[] = { 0, 15, 30, 60, 120, 300, 600 };
static const int kSleepN = sizeof kSleep / sizeof kSleep[0];
static const char* sleepName(uint16_t s) {
  static char b[12];
  if (!s) return "NEVER";
  if (s < 60) snprintf(b, sizeof b, "%us", s);
  else snprintf(b, sizeof b, "%um", s / 60);
  return b;
}
static void sleepCb(lv_event_t*) {
  UiPrefs& p = prefs();
  int i = 0;
  for (; i < kSleepN; i++) if (kSleep[i] == p.sleep_s) break;
  p.sleep_s = kSleep[(i + 1) % kSleepN];   // unknown value -> NEVER -> 15s ...
  savePrefs();
  go(P_SET_DISPLAY);
}

static const uint8_t kKb[] = { 0, 64, 127, 255 };
static const char* kbName(uint8_t v) {
  switch (v) { case 0: return "OFF"; case 64: return "LOW"; case 127: return "MEDIUM"; case 255: return "HIGH"; }
  static char b[8]; snprintf(b, sizeof b, "%u", v); return b;
}
static void kbCb(lv_event_t*) {
  UiPrefs& p = prefs();
  int i = 0;
  for (; i < 4; i++) if (kKb[i] == p.kb_bl) break;
  p.kb_bl = kKb[(i + 1) % 4];
  savePrefs();
  halSetKbBacklight(p.kb_bl);       // live
  go(P_SET_DISPLAY);
}

static void lockCb(lv_event_t*) {
  UiPrefs& p = prefs();
  p.lock_on = !p.lock_on;
  savePrefs();
  go(P_SET_DISPLAY);
}

void buildSetDisplay(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  const UiPrefs& p = prefs();
  char r[16];
  section(l, "SCREEN");
  snprintf(r, sizeof r, "%u%%", p.brightness);
  lv_obj_t* first = row(l, "BRIGHTNESS", r, brightCb);
  row(l, "SLEEP AFTER", sleepName(p.sleep_s), sleepCb);
  row(l, "KEYBOARD LIGHT", kbName(p.kb_bl), kbCb);
  row(l, "COLOURS", "GREEN", [](lv_event_t*) { toast("More colour schemes later", 1500); });
  section(l, "LOCK");
  row(l, "LOCK SCREEN", p.lock_on ? "ON" : "OFF", lockCb);
  lv_obj_t* h = label(l,
      "LOCK SCREEN ON: the deck locks when\n"
      "the screen sleeps. Unlock: hold the\n"
      "trackball for 3 s.\n"
      "SPACE (not while typing): sleep + lock\n"
      "after 1 s, whatever the setting; any\n"
      "other key in that second cancels.",
      FONT_S, pal().dim);
  lv_obj_set_width(h, lv_pct(100));
  lv_group_focus_obj(first);
}

}  // namespace sd
