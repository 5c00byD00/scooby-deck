// Scooby Deck v3 — milestone 1 placeholders for the CORE / TASTIC / SETUP pages.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include "Ui.h"
#include "DualMeshConfig.h"

namespace sd {
static void soon(lv_obj_t* b) {
  lv_obj_t* l = label(b, "> COMING IN THE NEXT MILESTONE\n\n  B / hold trackball: back");
  lv_obj_set_pos(l, 8, 20);
}
void buildNamesFirst(lv_obj_t* b) { soon(b); }
void buildCore(lv_obj_t* b) { soon(b); }      void buildCoreDms(lv_obj_t* b) { soon(b); }
void buildCoreEdit(lv_obj_t* b) { soon(b); }  void buildCoreChat(lv_obj_t* b) { soon(b); }
void buildCoreHeard(lv_obj_t* b) { soon(b); }
void buildTastic(lv_obj_t* b) { soon(b); }    void buildMtDms(lv_obj_t* b) { soon(b); }
void buildMtEdit(lv_obj_t* b) { soon(b); }    void buildMtChat(lv_obj_t* b) { soon(b); }
void buildMtHeard(lv_obj_t* b) { soon(b); }
uint32_t meshSignature(Page) { return 0; }
void buildSetup(lv_obj_t* b) { soon(b); }     void buildSetName(lv_obj_t* b) { soon(b); }
void buildSetMt(lv_obj_t* b) { soon(b); }     void buildSetMc(lv_obj_t* b) { soon(b); }
void buildSetDisplay(lv_obj_t* b) { soon(b); }
void defaultNames(char* l, size_t lc, char* s, size_t sc) { if (lc) l[0] = 0; if (sc) s[0] = 0; }
void applyNames(const char*, const char*) {}
bool namesSetupNeeded() { return false; }
}  // namespace sd
