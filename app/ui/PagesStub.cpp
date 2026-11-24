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
void buildSetDisplay(lv_obj_t* b) { soon(b); }
}  // namespace sd
