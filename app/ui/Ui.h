// Scooby Deck v3 — terminal UI (LVGL 9.3), shared declarations.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <stddef.h>

namespace sd {

// ---------------------------------------------------------------- theme
struct Palette { uint32_t bg, ink, dim, hi, warn; const char* name; };
const Palette& pal();
uint8_t schemeCount();
extern const lv_font_t* const FONT_S;   // unscii 8  (status dump)
extern const lv_font_t* const FONT_M;   // unscii 16 (everything else)

// ---------------------------------------------------------------- UI preferences (/dm_ui3.bin)
struct UiPrefs {
  uint8_t  version;
  uint8_t  brightness;   // 10..100 %
  uint16_t sleep_s;      // 0 = never
  uint8_t  kb_bl;        // 0..255
  uint8_t  lock_on;      // lock screen on wake
  uint8_t  names_done;   // first-boot name setup finished
  uint8_t  scheme;       // colour scheme (green only for now)
  uint8_t  mc_preset;    // last MeshCore NA preset picked (0xFF custom)
  uint8_t  reserved[7];
};
UiPrefs& prefs();
void     loadPrefs();
void     savePrefs();

// ---------------------------------------------------------------- hardware hooks (UITask.cpp)
void     halSetBrightness(uint8_t pct);
void     halSetKbBacklight(uint8_t v);
void     halSleepNow(bool lock);       // screen + keyboard light off now
uint16_t battMv();
uint8_t  battPct();
bool     sdBegin();                    // SD card mounted (lazily)

// ---------------------------------------------------------------- pages
enum Page : uint8_t {
  P_NONE = 0, P_BOOT, P_NAMES_FIRST, P_HOME,
  P_CORE, P_CORE_DMS, P_CORE_EDIT, P_CORE_CHAT, P_CORE_HEARD,
  P_TASTIC, P_MT_DMS, P_MT_EDIT, P_MT_CHAT, P_MT_HEARD,
  P_SETUP, P_SET_NAME, P_SET_MT, P_SET_MC, P_SET_DISPLAY,
  P_COUNT
};
struct Ctx {
  int16_t  chan = -1;         // channel slot of a chat, -1 = DM
  uint8_t  mcPeer[32] = {0};  // MeshCore DM peer (public key)
  uint32_t mtPeer = 0;        // Meshtastic DM peer (node number)
};
extern Ctx ctx;

void        begin();
void        tick(uint32_t now);        // main loop
void        go(Page p);                // switch page on the next tick (never inside an event)
void        back();
Page        page();
lv_group_t* group();
bool        typing();                  // a text field has the focus
bool        handleKey(uint32_t key);   // hotkeys when not typing; true = eaten
void        toast(const char* text, uint32_t ms = 1600);
void        noteActivity();

// lock screen
bool        locked();
void        lockEngage();              // show the lock overlay now
void        lockHold(uint32_t held_ms);// trackball held this long (0 = released)

// ---------------------------------------------------------------- widgets (Term.cpp)
lv_obj_t* frame(const char* title, bool backArrow);    // clean screen + header; returns the body
lv_obj_t* body();                                       // body of the current frame
lv_obj_t* headerRight();                                // clock / battery label
void      setTitle(const char* title);                  // replace the header title (keeps the < arrow)
lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* f = nullptr, uint32_t color = 0xFFFFFFFF);
lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, lv_event_cb_t cb, void* ud = nullptr);
lv_obj_t* list(lv_obj_t* parent, int x, int y, int w, int h);   // scrolling column
lv_obj_t* row(lv_obj_t* list, const char* left, const char* right, lv_event_cb_t cb, void* ud = nullptr, bool hot = false);
lv_obj_t* section(lv_obj_t* list, const char* title);
lv_obj_t* textField(lv_obj_t* parent, int x, int y, int w, const char* text, size_t maxLen);
void      styleFocus(lv_obj_t* o);
void      popupClose();
lv_obj_t* popup(const char* title, int h);                      // modal card on the top layer
lv_obj_t* popupButton(lv_obj_t* card, int slot, int slots, const char* text, lv_event_cb_t cb, void* ud = nullptr);
bool      popupOpen();
void      confirm(const char* question, const char* yes, void (*onYes)(void*), void* ud = nullptr);
void      ageText(uint32_t secs, char* out, size_t cap);

// page builders (Pages*.cpp)
void buildBoot(lv_obj_t* b);      void refreshBoot(uint32_t now);
void buildNamesFirst(lv_obj_t* b);
void buildHome(lv_obj_t* b);      void refreshHome(uint32_t now);
void buildCore(lv_obj_t* b);      void buildCoreDms(lv_obj_t* b);  void buildCoreEdit(lv_obj_t* b);
void buildCoreChat(lv_obj_t* b);  void buildCoreHeard(lv_obj_t* b);
void buildTastic(lv_obj_t* b);    void buildMtDms(lv_obj_t* b);    void buildMtEdit(lv_obj_t* b);
void buildMtChat(lv_obj_t* b);    void buildMtHeard(lv_obj_t* b);
uint32_t meshSignature(Page p);   // changes when the page's data changed
void buildSetup(lv_obj_t* b);     void buildSetName(lv_obj_t* b);  void buildSetMt(lv_obj_t* b);
void buildSetMc(lv_obj_t* b);     void buildSetDisplay(lv_obj_t* b);

// names (PagesSetup.cpp)
void defaultNames(char* longOut, size_t lcap, char* shortOut, size_t scap);
void applyNames(const char* longName, const char* shortName);
bool namesSetupNeeded();

}  // namespace sd
