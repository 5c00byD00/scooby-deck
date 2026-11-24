// Scooby Deck v3 — wall clock (see Clock.h).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <target.h>
#include "Clock.h"
#include "Ui.h"
#include "DualMeshStore.h"

namespace sd {

// Survives soft resets (not power-off): the last known time, refreshed every minute.
static RTC_NOINIT_ATTR uint32_t s_noinitUtc;
static RTC_NOINIT_ATTR uint32_t s_noinitMagic;
static RTC_NOINIT_ATTR uint8_t  s_noinitSrc;
static const uint32_t kMagic = 0x5C00C10Cu;
static const uint32_t kMinValid = 1767225600u;   // 2026-01-01: anything older = not set

struct Saved { uint8_t version; uint8_t src; uint16_t pad; uint32_t utc; };
static ClockSrc s_src = CLK_NONE;
static uint32_t s_lastSaveMs = 0, s_lastNoinitMs = 0;

// days <-> civil (Howard Hinnant's algorithms), no time zones involved
static int32_t daysFromCivil(int32_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}
uint32_t epochFromCivil(const DateTime& d) {
  return (uint32_t)daysFromCivil(d.year, d.month, d.day) * 86400u + d.hour * 3600u + d.minute * 60u + d.second;
}
DateTime civilFromEpoch(uint32_t t) {
  DateTime r;
  int32_t z = (int32_t)(t / 86400u) + 719468;
  const uint32_t secs = t % 86400u;
  const int32_t era = z / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  r.year = (uint16_t)(yoe + era * 400 + (m <= 2));
  r.month = (uint8_t)m; r.day = (uint8_t)d;
  r.hour = secs / 3600; r.minute = secs / 60 % 60; r.second = secs % 60;
  return r;
}

uint32_t clockUtc() { return rtc_clock.getCurrentTime(); }
ClockSrc clockSource() { return s_src; }
int32_t clockOffsetS() { return (int32_t)prefs().tz_q * 900; }
DateTime clockLocal() { return civilFromEpoch((uint32_t)((int64_t)clockUtc() + clockOffsetS())); }

void clockSave() {
  if (s_src == CLK_NONE) return;
  Saved s = { 1, (uint8_t)s_src, 0, clockUtc() };
  DualMesh::Store::put("clk3", &s, sizeof s);
  s_lastSaveMs = millis();
}

static void noinitUpdate() {
  s_noinitUtc = clockUtc();
  s_noinitSrc = (uint8_t)s_src;
  s_noinitMagic = kMagic;
  s_lastNoinitMs = millis();
}

void clockSetUtc(uint32_t utc, ClockSrc src) {
  rtc_clock.setCurrentTime(utc);   // system time: MeshCore timestamps use it too
  s_src = src;
  noinitUpdate();
  clockSave();
}

void clockSetLocal(const DateTime& local) {
  clockSetUtc((uint32_t)((int64_t)epochFromCivil(local) - clockOffsetS()), CLK_MANUAL);
}

void clockBegin() {
  const uint32_t sys = clockUtc();
  const esp_reset_reason_t why = esp_reset_reason();
  const bool soft = why == ESP_RST_SW || why == ESP_RST_PANIC || why == ESP_RST_INT_WDT ||
                    why == ESP_RST_TASK_WDT || why == ESP_RST_WDT || why == ESP_RST_DEEPSLEEP;
  // 1) a soft reset keeps our RTC-memory copy (at most a minute old)
  if (soft && s_noinitMagic == kMagic && s_noinitUtc > kMinValid && s_noinitSrc != CLK_NONE) {
    const uint32_t t = s_noinitUtc > sys ? s_noinitUtc : sys;
    rtc_clock.setCurrentTime(t);
    s_src = (ClockSrc)s_noinitSrc;
    noinitUpdate();
    return;
  }
  // 2) power-on: the last time saved to flash, as an estimate (we can't know how long it was off)
  Saved s;
  if (DualMesh::Store::get("clk3", &s, sizeof s) == (long)sizeof s && s.version == 1 &&
      s.src != CLK_NONE && s.utc > kMinValid) {
    rtc_clock.setCurrentTime(s.utc > sys ? s.utc : sys);
    s_src = CLK_RESTORED;
    noinitUpdate();
    return;
  }
  s_src = CLK_NONE;   // MeshCore's seed time keeps running underneath
}

void clockTick(uint32_t now) {
  if (s_src == CLK_NONE) return;
  if (now - s_lastNoinitMs >= 60000u) noinitUpdate();
  if (now - s_lastSaveMs >= 600000u) clockSave();   // flash: every 10 min
}

void clockHeader(char* out, size_t cap) {
  if (s_src == CLK_NONE) { snprintf(out, cap, "--:--"); return; }
  const DateTime d = clockLocal();
  const char* est = s_src == CLK_RESTORED ? "~" : "";
  if (prefs().clock24) snprintf(out, cap, "%s%02u:%02u", est, d.hour, d.minute);
  else {
    const unsigned h = d.hour % 12 ? d.hour % 12 : 12;
    snprintf(out, cap, "%s%u:%02u%s", est, h, d.minute, d.hour < 12 ? "AM" : "PM");
  }
}

}  // namespace sd
