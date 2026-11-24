// Scooby Deck v3 — LilyGO T-Deck Plus board objects.
// Derived from upstream MeshCore variants/lilygo_tdeck/target.h (MIT, Scott Powell /
// rippleradios.com): same objects, but the radio wrapper is DualMeshRadioWrapper and the
// display is Faro's LVGL ST7789 driver. GPL-3.0-or-later as part of Scooby Deck.
#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "../../external/MeshCore/variants/lilygo_tdeck/TDeckBoard.h"
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include "DualMeshRadioWrapper.h"
#include "FaroDisplay.h"
#include "helpers/sensors/EnvironmentSensorManager.h"
#include "helpers/sensors/MicroNMEALocationProvider.h"

extern TDeckBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern DISPLAY_CLASS display;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
