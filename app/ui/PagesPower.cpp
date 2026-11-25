// Scooby Deck v3 — SETUP > BATTERY: level calibration ("reset level counter") and the
// charging screen.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include "Ui.h"
#include "ScoobyVersion.h"

namespace sd {

static void barText(char* out, int cells, int pct) {
  int f = (pct * cells + 50) / 100;
  if (f > cells) f = cells;
  out[0] = '[';
  for (int i = 0; i < cells; i++) out[1 + i] = i < f ? '#' : '-';
  out[1 + cells] = ']';
  out[2 + cells] = 0;
}

// ---------------------------------------------------------------- SETUP > BATTERY
static void resetYes(void*) {
  uint16_t mv = 0;
  char t[96];
  if (battCalibrate(&mv)) {
    snprintf(t, sizeof t, "Level counter reset: %u.%02u V is now 100%%", mv / 1000, mv % 1000 / 10);
  } else {
    snprintf(t, sizeof t, "Not reset: the battery reads %u.%02u V, that is not a full charge", mv / 1000, mv % 1000 / 10);
  }
  toast(t, 2600);
  go(P_SET_BATTERY);
}

void buildSetBattery(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  char r[40];
  const uint16_t mv = battMv();
  section(l, "LEVEL");
  snprintf(r, sizeof r, "%u%%  %u.%02u V", battPct(), mv / 1000, mv % 1000 / 10);
  row(l, "NOW", r, nullptr);
  const uint16_t full = prefs().batt_full_mv;
  if (full) snprintf(r, sizeof r, "%u.%02u V = 100%%", full / 1000, full % 1000 / 10);
  else snprintf(r, sizeof r, "4.20 V (default)");
  row(l, "FULL AT", r, nullptr);
  lv_obj_t* first = row(l, "RESET LEVEL COUNTER", ">", [](lv_event_t*) {
    confirm("Reset counter only when battery fully charged, counter will reset to 100%.",
            "[ RESET ]", resetYes);
  });
  row(l, "CHARGING SCREEN", ">", [](lv_event_t*) { go(P_CHARGING); });
  lv_obj_t* h = label(l,
      "The level comes from the battery\n"
      "voltage (no fuel gauge on the T-Deck\n"
      "Plus). RESET at full charge makes that\n"
      "voltage read 100 %; the rest of the\n"
      "scale follows it.\n"
      "CHARGING SCREEN: 30 % light, no sleep.",
      FONT_S, pal().dim);
  lv_obj_set_width(h, lv_pct(100));
  lv_obj_set_style_text_line_space(h, 3, 0);
  lv_group_focus_obj(first);
}

// ---------------------------------------------------------------- charging screen
static lv_obj_t* s_pct = nullptr;
static lv_obj_t* s_bar = nullptr;
static lv_obj_t* s_mv = nullptr;
void refreshCharging(uint32_t) {
  if (!s_pct) return;
  const uint8_t p = battPct();
  const uint16_t mv = battMv();
  char t[32];
  snprintf(t, sizeof t, "%u%%", p);
  lv_label_set_text(s_pct, t);
  barText(t, 20, p);
  lv_label_set_text(s_bar, t);
  snprintf(t, sizeof t, "%u.%02u V", mv / 1000, mv % 1000 / 10);
  lv_label_set_text(s_mv, t);
}
void buildCharging(lv_obj_t* b) {
  // only the logo, the charge level and the way out (P_CHARGING has no header)
  lv_obj_t* art = label(b, SCOOBY_BANNER, FONT_S);
  lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 18);
  s_pct = label(b, "", FONT_L);
  lv_obj_align(s_pct, LV_ALIGN_TOP_MID, 0, 104);
  s_bar = label(b, "", FONT_M);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 142);
  s_mv = label(b, "", FONT_S, pal().dim);
  lv_obj_align(s_mv, LV_ALIGN_TOP_MID, 0, 164);
  lv_obj_t* n = label(b, "Tap the trackball to exit", FONT_M, pal().dim);
  lv_obj_set_width(n, 320);
  lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -12);
  refreshCharging(millis());
  // re-align after the text is set (centred labels)
  lv_obj_align(s_pct, LV_ALIGN_TOP_MID, 0, 104);
  lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 142);
  lv_obj_align(s_mv, LV_ALIGN_TOP_MID, 0, 164);
}

}  // namespace sd
