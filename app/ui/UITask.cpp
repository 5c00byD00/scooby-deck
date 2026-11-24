// Scooby Deck v3 — UITask: hardware, LVGL, input devices, screen sleep, mesh events.
// The LVGL bring-up and input-device wiring follow Faro's app/ui/UITask.cpp
// (radeksixi/sixi-tdeck-plus, Apache-2.0); the rest is ours. GPL-3.0-or-later.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <lvgl.h>
#include "FaroDisplay.h"
#include "TouchGT911.h"
#include "KeyboardBBQ10.h"
#include "Trackball.h"
#include "PowerBoard.h"
#include "UITask.h"
#include <MyMesh.h>
#include <target.h>
#include "Ui.h"
#include "McStore.h"
#include "Clock.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

struct Dev {
  FaroDisplay* display = nullptr;
  faro::hal::TouchGT911 touch;
  faro::hal::KeyboardBBQ10 keyboard;
  faro::hal::Trackball trackball;
  faro::hal::PowerBoard power;
  uint16_t buf1[320 * 24];
  uint32_t lastMs = 0, lastInput = 0, lastSleepPoll = 0;
  bool off = false;
  bool charging = false;   // SETUP > BATTERY > CHARGING SCREEN is up
  uint32_t kbKey = 0; bool kbPressed = false;
  bool encPressed = false;
  bool sdMounted = false; uint32_t sdFailMs = 0;
  uint16_t mv = 0; uint8_t pct = 0; uint32_t battMs = 0;
};
Dev* g = nullptr;

// Wake the screen; true if it was off (the input that woke it is swallowed).
bool wake() {
  g->lastInput = millis();
  sd::noteActivity();
  if (!g->off) return false;
  g->off = false;
  if (g->display) {
    g->display->setBacklight(g->charging ? 30 : sd::prefs().brightness);
    g->display->turnOn();
  }
  g->keyboard.setBacklight(sd::prefs().kb_bl);
  setCpuFrequencyMhz(240);
  if (lv_screen_active()) lv_obj_invalidate(lv_screen_active());
  return true;
}

void screenOff(bool lock) {
  if (lock || sd::prefs().lock_on) sd::lockEngage();
  lv_refr_now(nullptr);
  if (g->display) g->display->turnOff();
  g->keyboard.setBacklight(0);
  g->off = true;
  setCpuFrequencyMhz(80);   // the radio task keeps running; UI work is idle
}

void readTouch(lv_indev_t*, lv_indev_data_t* d) {
  int16_t x = 0, y = 0;
  if (g->touch.read(x, y)) {
    d->point.x = x; d->point.y = y;
    d->state = wake() || sd::locked() ? LV_INDEV_STATE_RELEASED : LV_INDEV_STATE_PRESSED;
  } else {
    d->point.x = x; d->point.y = y;
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

void readTrackball(lv_indev_t*, lv_indev_data_t* d) {
  d->enc_diff = 0;
  if (g->encPressed) { g->encPressed = false; d->state = LV_INDEV_STATE_RELEASED; return; }
  d->state = LV_INDEV_STATE_RELEASED;
  int16_t dx = 0, dy = 0;
  bool clicked = false, longPressed = false;
  g->trackball.poll(dx, dy, clicked, longPressed);
  const uint32_t held = g->trackball.heldMs();
  if (sd::locked()) {
    if (dx || dy || clicked || held) wake();
    sd::lockHold(held);
    return;
  }
  if (!(dx || dy || clicked || longPressed)) return;
  if (wake()) return;
  if (sd::page() == sd::P_CHARGING) {   // "Tap the trackball to exit"; rolling does nothing
    if (clicked || longPressed) sd::back();
    return;
  }
  if (longPressed) { sd::back(); return; }
  d->enc_diff = (int16_t)(dy + dx);
  if (clicked) { d->state = LV_INDEV_STATE_PRESSED; g->encPressed = true; }
}

void readKeyboard(lv_indev_t*, lv_indev_data_t* d) {
  d->key = g->kbKey;
  d->state = LV_INDEV_STATE_RELEASED;
  if (g->kbPressed) { g->kbPressed = false; return; }
  const uint8_t k = g->keyboard.readKey();
  if (!k) return;
  if (wake()) return;
  if (sd::handleKey(k)) return;
  uint32_t lk = k;
  if (k == 0x0D || k == 0x0A) lk = LV_KEY_ENTER;
  else if (k == 0x08) lk = LV_KEY_BACKSPACE;
  else if (k == 0x1B) lk = LV_KEY_ESC;
  g->kbKey = lk; g->kbPressed = true;
  d->key = lk;
  d->state = LV_INDEV_STATE_PRESSED;
}

}  // namespace

// ------------------------------------------------------------------ hooks for the UI
namespace sd {
void halSetBrightness(uint8_t pct) { if (g && g->display && !g->off) g->display->setBacklight(pct); }
void halSetKbBacklight(uint8_t v) { if (g && !g->off) g->keyboard.setBacklight(v); }
void halSleepNow(bool lock) { if (g && !g->off) screenOff(lock); }
static void battRead() {
  if (!g) return;
  const uint32_t now = millis();
  if (g->mv && now - g->battMs < 5000) return;
  g->battMs = now;
  g->mv = g->power.readBattMilliVolts();
  // The T-Deck Plus has no fuel gauge: the level comes from the voltage alone. A calibration
  // (SETUP > BATTERY > RESET LEVEL COUNTER at full charge) scales the reading so that the
  // voltage seen at full charge counts as 4.20 V, i.e. 100 %; this takes out the divider /
  // ADC gain error of this board.
  uint32_t mv = g->mv;
  const uint16_t full = sd::prefs().batt_full_mv;
  if (full >= 3800 && full <= 4500) mv = mv * 4200u / full;
  g->pct = g->power.readBattPercent((uint16_t)(mv > 65000 ? 65000 : mv));
}
uint16_t battMv() { battRead(); return g ? g->mv : 0; }
bool battCalibrate(uint16_t* mvOut) {
  if (!g) return false;
  // PowerBoard's reading is already an average of a DMA burst (cached up to 2 s)
  const uint16_t mv = g->power.readBattMilliVolts();
  if (mvOut) *mvOut = mv;
  if (mv < 3800 || mv > 4500) return false;   // not near full (or no battery): keep the old value
  prefs().batt_full_mv = mv;
  savePrefs();
  g->mv = 0;   // next read recomputes the %
  return true;
}
void halChargingMode(bool on) {
  if (!g) return;
  g->charging = on;
  g->lastInput = millis();
  if (g->display && !g->off) g->display->setBacklight(on ? 30 : prefs().brightness);
}
uint8_t battPct() { battRead(); return g ? g->pct : 0; }
bool sdBegin() {
  if (!g) return false;
  if (g->sdMounted && SD.cardType() != CARD_NONE) return true;
  if (g->sdFailMs && millis() - g->sdFailMs < 5000) return false;
  // SX1262 (CS 9), ST7789 (CS 12) and the card (CS 39) share SCLK 40 / MISO 38 / MOSI 41 on `SPI`
  // (after Faro's SdCard.cpp): deselect the others first.
  digitalWrite(P_LORA_NSS, HIGH);
  digitalWrite(12, HIGH);
  pinMode(39, OUTPUT); digitalWrite(39, HIGH);
  g->sdMounted = SD.begin(39, SPI, 10000000);
  if (!g->sdMounted) g->sdFailMs = millis();
  if (g->display) g->display->reinitSpi();
  return g->sdMounted;
}
}  // namespace sd

// ------------------------------------------------------------------ UITask
void UITask::begin(DisplayDriver* display, SensorManager*, NodePrefs*) {
  if (g) return;
  g = new Dev();
  g->display = static_cast<FaroDisplay*>(display);
  g->power.begin();
  if (g->display) g->display->reinitSpi();
  g->touch.begin();
  g->keyboard.begin();
  g->trackball.begin();
  mc::begin();
  sd::clockBegin();

  lv_init();
  lv_display_t* disp = lv_display_create(320, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g->buf1, nullptr, sizeof(g->buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, [](lv_display_t* dd, const lv_area_t* a, uint8_t* px) {
    if (g && g->display) {
      g->display->flush(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, reinterpret_cast<const uint16_t*>(px));
    }
    lv_display_flush_ready(dd);
  });
  lv_display_set_theme(disp, nullptr);

  sd::begin();   // loads the UI prefs, makes the group, queues the boot screen

  lv_indev_t* t = lv_indev_create();
  lv_indev_set_type(t, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(t, readTouch);
  lv_indev_t* e = lv_indev_create();
  lv_indev_set_type(e, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(e, readTrackball);
  lv_indev_set_group(e, sd::group());
  lv_indev_t* k = lv_indev_create();
  lv_indev_set_type(k, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(k, readKeyboard);
  lv_indev_set_group(k, sd::group());

  if (g->display) g->display->setBacklight(sd::prefs().brightness);
  g->keyboard.setBacklight(sd::prefs().kb_bl);
  g->lastMs = g->lastInput = millis();
  sd::tick(g->lastMs);
  lv_refr_now(nullptr);
}

void UITask::notify(UIEventType) {}

void UITask::loop() {
  if (!g) return;
  const uint32_t now = millis();
  lv_tick_inc(now - g->lastMs);
  g->lastMs = now;
  mc::loop(now);
  sd::clockTick(now);
  sd::tick(now);

  const uint32_t sleepMs = (uint32_t)sd::prefs().sleep_s * 1000u;
  if (!g->off && sleepMs && !g->charging && now - g->lastInput > sleepMs && sd::page() != sd::P_BOOT) screenOff(false);

  if (g->off) {
    // panel asleep: no redraws, but keep polling the inputs that wake it
    if (now - g->lastSleepPoll >= 30) {
      g->lastSleepPoll = now;
      for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) lv_indev_read(i);
    }
    if (g->off) { delay(5); return; }
  }
  lv_timer_handler();
}

// ------------------------------------------------------------------ MyMesh::Listener
void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t, const char* text) {
  if (!text || txt_type != TXT_TYPE_PLAIN) return;
  mc::onDm(from, text, pkt ? (int8_t)pkt->_snr : 0, pkt ? pkt->getPathHashCount() : 0);
}
void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& cd, const char* text) {
  if (!text) return;
  const int idx = the_mesh.findChannelIdx(cd.channel);
  mc::onChannel(idx >= 0 ? (uint8_t)idx : 0, text, pkt ? (int8_t)pkt->_snr : 0, pkt ? pkt->getPathHashCount() : 0);
}
void UITask::onQueueSizeChanged(int) {}
void UITask::onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) {}
void UITask::onACKRecv(uint32_t crc) { mc::onAck(crc); }
