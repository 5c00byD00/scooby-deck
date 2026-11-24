// Scooby Deck v3 — wall clock: manual time, 12/24 h, UTC offset, best-effort persistence.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
//
// The T-Deck Plus has no battery-backed RTC chip. The time lives in the ESP32-S3 system
// clock (which MeshCore's rtc_clock also reads), so it keeps running while the deck is
// powered (screen sleep, lock, soft resets) but is lost at power-off. We save it to flash
// every few minutes and restore the newest value at boot, marked as an estimate ("~")
// until it is set again.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace sd {

enum ClockSrc : uint8_t {
  CLK_NONE = 0,      // never set: the header shows --:--
  CLK_RESTORED,      // restored after power-off from the last saved time: an estimate
  CLK_MANUAL,        // set by hand on this power-up (or kept across a soft reset)
  CLK_GPS,           // set from GPS (hook for later)
};

struct DateTime { uint16_t year; uint8_t month, day, hour, minute, second; };

void     clockBegin();                       // once, after rtc_clock.begin()
void     clockTick(uint32_t nowMs);          // main loop: periodic save
ClockSrc clockSource();
uint32_t clockUtc();                         // Unix seconds (UTC), what MeshCore uses
int32_t  clockOffsetS();                     // local = UTC + offset (from SETUP > TIME)
DateTime clockLocal();
void     clockSetLocal(const DateTime& local);              // manual entry (local wall time)
void     clockSetUtc(uint32_t utc, ClockSrc src);           // GPS hook: clockSetUtc(fixUtc, CLK_GPS)
void     clockHeader(char* out, size_t cap);                // "3:42PM" / "15:42" / "--:--" / "~3:42PM"
void     clockSave();                                        // persist now

uint32_t epochFromCivil(const DateTime& d);                 // naive (no zone)
DateTime civilFromEpoch(uint32_t t);

}  // namespace sd
