// Scooby Deck v3 — LilyGO T-Deck Plus board objects.
// Derived from upstream MeshCore variants/lilygo_tdeck/target.cpp (MIT, Scott Powell /
// rippleradios.com). Changes: the radio sits on the global FSPI `SPI` shared with the
// display and SD card (as Faro does), and radio_driver is a DualMeshRadioWrapper.
// GPL-3.0-or-later as part of Scooby Deck.
#include <Arduino.h>
#include "target.h"

TDeckBoard board;

// P_LORA_SCLK is unflagged in envs/scooby.ini: the radio uses `SPI`, begun on 40/38/41 by
// FaroDisplay::begin() before radio_init().
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

DISPLAY_CLASS display;
ColorVal UIColor::window_bkg = 0;   // upstream defines it in the ST7789 driver we do not build

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  Wire.begin(18, 8);
  return radio.std_init();
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
