// Scooby Deck v3 — SETUP: listening mode, device names (incl. first boot), Meshtastic radio
// and PKI key on SD, MeshCore radio (North America presets + custom).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <ctype.h>
#include <esp_mac.h>
#include <MyMesh.h>
#include <target.h>
#include "Ui.h"
#include "DualMeshConfig.h"
#include "DualMeshMt.h"
#include "ScoobyVersion.h"
#include "Clock.h"
#include "McStore.h"
#include <DataStore.h>
extern DataStore store;

namespace sd {

using namespace DualMesh;

static void radioChanged() {
  save();
  applyChannels();
  radio_driver.configChanged();
}

// ============================================================== names
void defaultNames(char* l, size_t lc, char* s, size_t sc) {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  // the device ID: the Meshtastic node number (MAC-derived), as Meshtastic shows it
  snprintf(l, lc, "!%08lx", (unsigned long)Mt::myNum());
  snprintf(s, sc, "%02x%02x", mac[4], mac[5]);
}
bool namesSetupNeeded() {
  if (prefs().names_done) return false;
  if (cfg().long_name[0]) { prefs().names_done = 1; savePrefs(); return false; }   // names from an earlier version
  return true;
}
void applyNames(const char* longName, const char* shortName) {
  Config& c = cfg();
  strncpy(c.long_name, longName, sizeof c.long_name - 1);
  c.long_name[sizeof c.long_name - 1] = 0;
  strncpy(c.short_name, shortName, sizeof c.short_name - 1);
  c.short_name[sizeof c.short_name - 1] = 0;
  save();
  Mt::identityChanged();
  NodePrefs* np = the_mesh.getNodePrefs();
  strncpy(np->node_name, longName, sizeof np->node_name - 1);
  np->node_name[sizeof np->node_name - 1] = 0;
  the_mesh.savePrefs();
}

static lv_obj_t* s_fLong = nullptr;
static lv_obj_t* s_fShort = nullptr;
static void namesSave() {
  char l[40], s[8], dl[40], ds[8];
  defaultNames(dl, sizeof dl, ds, sizeof ds);
  strncpy(l, lv_textarea_get_text(s_fLong), sizeof l - 1); l[sizeof l - 1] = 0;
  strncpy(s, lv_textarea_get_text(s_fShort), sizeof s - 1); s[sizeof s - 1] = 0;
  applyNames(l[0] ? l : dl, s[0] ? s : ds);
  prefs().names_done = 1;
  savePrefs();
  toast("Names saved");
  go(page() == P_NAMES_FIRST ? P_HOME : P_SETUP);
}
static void longReadyCb(lv_event_t*) { lv_group_focus_obj(s_fShort); lv_group_set_editing(group(), true); }
static void shortReadyCb(lv_event_t*) { namesSave(); }
static void namesSaveCb(lv_event_t*) { namesSave(); }
static void namesDefaultCb(lv_event_t*) {
  char dl[40], ds[8];
  defaultNames(dl, sizeof dl, ds, sizeof ds);
  lv_textarea_set_text(s_fLong, dl);
  lv_textarea_set_text(s_fShort, ds);
}
static void namesForm(lv_obj_t* b, bool first) {
  char dl[40], ds[8], cur[40];
  defaultNames(dl, sizeof dl, ds, sizeof ds);
  lv_obj_t* h = label(b, first ? "> NAME THIS DEVICE\n  ENTER, ENTER keeps the defaults" : "> DEVICE NAME", FONT_M);
  lv_obj_set_pos(h, 4, 2);
  lv_obj_t* a = label(b, "LONG  (MeshCore + Meshtastic, 31 max)", FONT_S, pal().dim);
  lv_obj_set_pos(a, 4, first ? 40 : 24);
  snprintf(cur, sizeof cur, "%s", cfg().long_name[0] ? cfg().long_name : dl);
  s_fLong = textField(b, 4, first ? 52 : 36, 312, cur, 31);
  lv_obj_t* c = label(b, "SHORT (Meshtastic only, 4 max)", FONT_S, pal().dim);
  lv_obj_set_pos(c, 4, first ? 84 : 68);
  s_fShort = textField(b, 4, first ? 96 : 80, 120, cfg().short_name[0] ? cfg().short_name : ds, 4);
  lv_obj_add_event_cb(s_fLong, longReadyCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_fShort, shortReadyCb, LV_EVENT_READY, nullptr);
  lv_obj_t* note = label(b, "NOTE: the LONG name is shared by CORE\n(MeshCore node name) and TASTIC\n(Meshtastic long name). The SHORT name\nis used by TASTIC only.", FONT_S, pal().warn);
  lv_obj_set_pos(note, 4, first ? 128 : 112);
  button(b, "[ SAVE ]", 0, 186, 158, 32, namesSaveCb);
  button(b, "[ DEFAULTS ]", 162, 186, 158, 32, namesDefaultCb);
  lv_group_focus_obj(s_fLong);
  lv_group_set_editing(group(), true);
}
void buildNamesFirst(lv_obj_t* b) { namesForm(b, true); }
void buildSetName(lv_obj_t* b) { namesForm(b, false); }

// ============================================================== SETUP list
static const char* listenName(uint8_t m) {
  return m == MODE_MESHTASTIC ? "MESHTASTIC" : m == MODE_MESHCORE ? "MESHCORE" : "BOTH";
}
static void listenCb(lv_event_t*) {
  // MESHTASTIC > MESHCORE > BOTH > ...
  Config& c = cfg();
  c.mode = c.mode == MODE_MESHTASTIC ? MODE_MESHCORE : c.mode == MODE_MESHCORE ? MODE_DUAL : MODE_MESHTASTIC;
  radioChanged();
  go(P_SETUP);
}
struct McPreset { const char* name; float freq, bw; uint8_t sf, cr; };
// North America only (keeps it small); anything else is CUSTOM.
static const McPreset kMcNa[] = {
  { "US/Canada",       910.525f, 62.5f, 7, 5 },
  { "USA: SoCal",      927.875f, 62.5f, 7, 5 },
  { "USA: Sacramento", 909.875f, 62.5f, 9, 5 },
};
static const int kMcNaN = sizeof kMcNa / sizeof kMcNa[0];
static int mcPresetNow() {
  const NodePrefs* np = the_mesh.getNodePrefs();
  for (int i = 0; i < kMcNaN; i++)
    if (fabsf(np->freq - kMcNa[i].freq) < 0.002f && fabsf(np->bw - kMcNa[i].bw) < 0.01f &&
        np->sf == kMcNa[i].sf && np->cr == kMcNa[i].cr) return i;
  return -1;
}

void buildSetup(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 186);
  char hdr[44];
  snprintf(hdr, sizeof hdr, "SETUP              v%s", SCOOBY_DECK_VERSION);
  section(l, hdr);
  char r[48];
  lv_obj_t* first = row(l, "1 LISTENING TO", listenName(cfg().mode), listenCb, nullptr, true);
  snprintf(r, sizeof r, "%.10s/%s >", Mt::longName(), cfg().short_name[0] ? cfg().short_name : "-");
  row(l, "2 DEVICE NAME", r, [](lv_event_t*) { go(P_SET_NAME); });
  snprintf(r, sizeof r, "%s >", mtPresetName());
  row(l, "3 MESHTASTIC", r, [](lv_event_t*) { go(P_SET_MT); });
  const int mp = mcPresetNow();
  snprintf(r, sizeof r, "%s >", mp >= 0 ? kMcNa[mp].name : "CUSTOM");
  row(l, "4 MESHCORE", r, [](lv_event_t*) { go(P_SET_MC); });
  snprintf(r, sizeof r, "%u%%%s >", prefs().brightness, prefs().lock_on ? " LOCK" : "");
  row(l, "5 DISPLAY", r, [](lv_event_t*) { go(P_SET_DISPLAY); });
  snprintf(r, sizeof r, "%u%% %u.%02uV >", battPct(), battMv() / 1000, battMv() % 1000 / 10);
  row(l, "6 BATTERY", r, [](lv_event_t*) { go(P_SET_BATTERY); });
  char c[12];
  clockHeader(c, sizeof c);
  snprintf(r, sizeof r, "%s %s >", c, prefs().clock24 ? "24H" : "12H");
  row(l, "7 TIME", r, [](lv_event_t*) { go(P_SET_TIME); });
  lv_obj_t* help = label(l, "LISTENING TO cycles MESHTASTIC >\nMESHCORE > BOTH (BOTH = time-sliced\non one radio).", FONT_S, pal().dim);
  lv_obj_set_width(help, lv_pct(100));
  lv_obj_t* foot = label(b, "tap: cycle / open    B: home", FONT_S, pal().dim);
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_group_focus_obj(first);
}

// ============================================================== number popup
typedef void (*NumFn)(const char* text);
static NumFn s_numFn = nullptr;
static lv_obj_t* s_numField = nullptr;
static void numOk() {
  char t[24];
  strncpy(t, lv_textarea_get_text(s_numField), sizeof t - 1);
  t[sizeof t - 1] = 0;
  NumFn fn = s_numFn;
  popupClose();
  if (fn) fn(t);
}
static void numOkCb(lv_event_t*) { numOk(); }
static void numCancelCb(lv_event_t*) { popupClose(); }
static void numPopup(const char* title, const char* hint, const char* cur, NumFn fn) {
  s_numFn = fn;
  lv_obj_t* p = popup(title, 130);
  lv_obj_t* h = label(p, hint, FONT_S, pal().dim);
  lv_obj_set_pos(h, 0, 22);
  s_numField = textField(p, 0, 36, 288, cur, 12);
  lv_textarea_set_accepted_chars(s_numField, "0123456789.");
  lv_obj_add_event_cb(s_numField, numOkCb, LV_EVENT_READY, nullptr);
  popupButton(p, 0, 2, "[ OK ]", numOkCb);
  popupButton(p, 1, 2, "[ CANCEL ]", numCancelCb);
  lv_group_focus_obj(s_numField);
  lv_group_set_editing(group(), true);
}

// ============================================================== MESHTASTIC
static const char* kKeyPath = "/scooby_mt_key.txt";
static void keyBackup() {
  if (!sdBegin()) { toast("No SD card"); return; }
  File f = SD.open(kKeyPath, FILE_WRITE);
  if (!f) { toast("SD write failed"); return; }
  const uint8_t* pr = Mt::privateKey();
  const uint8_t* pu = Mt::publicKey();
  char h[72];
  f.printf("# Scooby Deck Meshtastic PKI key. KEEP PRIVATE: it decrypts your DMs.\n");
  f.printf("node=!%08lx\n", (unsigned long)Mt::myNum());
  for (int i = 0; i < 32; i++) snprintf(h + i * 2, 3, "%02x", pr[i]);
  f.printf("private=%s\n", h);
  for (int i = 0; i < 32; i++) snprintf(h + i * 2, 3, "%02x", pu[i]);
  f.printf("public=%s\n", h);
  f.close();
  toast("Key saved to SD: /scooby_mt_key.txt");
}
static void keyRestoreYes(void*) {
  if (!sdBegin()) { toast("No SD card"); return; }
  File f = SD.open(kKeyPath, FILE_READ);
  if (!f) { toast("No /scooby_mt_key.txt on the SD card"); return; }
  uint8_t key[32];
  bool got = false;
  char line[100];
  while (f.available() && !got) {
    const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
    line[n] = 0;
    if (strncmp(line, "private=", 8)) continue;
    const char* h = line + 8;
    int i = 0;
    for (; i < 32 && isxdigit((unsigned char)h[i * 2]) && isxdigit((unsigned char)h[i * 2 + 1]); i++) {
      unsigned v; sscanf(h + i * 2, "%2x", &v); key[i] = (uint8_t)v;
    }
    got = i == 32;
  }
  f.close();
  if (!got || !Mt::setPrivateKey(key)) { toast("No valid private= line in the file"); return; }
  toast("Key restored from SD");
  go(P_SET_MT);
}
static void keyNewYes(void*) {
  Mt::setPrivateKey(nullptr);
  toast("New key made: peers relearn it from the next NodeInfo", 2200);
  go(P_SET_MT);
}

static void mtPresetCb(lv_event_t*) {
  Config& c = cfg();
  c.mt_preset = (c.mt_preset == PRESET_CUSTOM) ? 0 : (uint8_t)((c.mt_preset + 1) % mtPresetCount());
  if (c.mt_slot > mtSlotCount()) c.mt_slot = 0;
  radioChanged();
  go(P_SET_MT);
}
static void mtSlotSet(const char* t) {
  const long v = atol(t);
  if (v < 0 || v > mtSlotCount()) { char b[48]; snprintf(b, sizeof b, "Slot 0 (auto) to %u", mtSlotCount()); toast(b); return; }
  cfg().mt_slot = (uint16_t)v;
  radioChanged();
  go(P_SET_MT);
}
static void mtSlotCb(lv_event_t*) {
  char cur[8], hint[64];
  snprintf(cur, sizeof cur, "%u", cfg().mt_slot);
  snprintf(hint, sizeof hint, "0 = auto (by channel name), 1-%u", mtSlotCount());
  numPopup("MESHTASTIC SLOT", hint, cur, mtSlotSet);
}
static void mtSfCb(lv_event_t*) {
  static const uint8_t seq[] = { 0, 7, 8, 9, 10, 11, 12 };
  uint8_t& o = cfg().mt_sf_ovr;
  int i = 0;
  for (; i < (int)sizeof seq; i++) if (seq[i] == o) break;
  o = seq[(i + 1) % sizeof seq];
  radioChanged();
  if (o) toast("SF override: nodes on the stock preset SF won't hear you", 2200);
  go(P_SET_MT);
}
static void mtHopsCb(lv_event_t*) {
  uint8_t& h = cfg().mt_hops;
  h = h >= 7 ? 1 : h + 1;
  save();
  go(P_SET_MT);
}
static void mtClearYes(void*) {
  int n = 0;
  Mt::Node nd;
  for (int guard = 0; guard < 512 && Mt::nodeCount() && Mt::nodeAt(0, nd); guard++) {
    if (!Mt::deleteNode(nd.num)) break;
    n++;
  }
  char t[40];
  snprintf(t, sizeof t, "Cleared %d node%s", n, n == 1 ? "" : "s");
  toast(t);
  go(P_SET_MT);
}
void buildSetMt(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  const Config& c = cfg();
  char r[40];
  section(l, "RADIO");
  lv_obj_t* first = row(l, "PRESET", mtPresetName(), mtPresetCb);
  if (c.mt_slot) snprintf(r, sizeof r, "%u  %.3f", c.mt_slot, (double)mtFreqMHz());
  else snprintf(r, sizeof r, "AUTO  %.3f", (double)mtFreqMHz());
  row(l, "SLOT", r, mtSlotCb);
  if (c.mt_sf_ovr) snprintf(r, sizeof r, "SF%u (override)", c.mt_sf_ovr);
  else snprintf(r, sizeof r, "PRESET (SF%u)", mtSf());
  row(l, "SPREADING FACTOR", r, mtSfCb);
  snprintf(r, sizeof r, "%u", c.mt_hops);
  row(l, "HOPS", r, mtHopsCb);
  row(l, "ROLE", "CLIENT_MUTE", [](lv_event_t*) { toast("CLIENT_MUTE: this node never rebroadcasts others", 2000); });
  section(l, "PKI KEY (DMs)");
  char pk[20];
  const uint8_t* pu = Mt::publicKey();
  snprintf(pk, sizeof pk, "%02x%02x%02x%02x%02x%02x..", pu[0], pu[1], pu[2], pu[3], pu[4], pu[5]);
  row(l, "PUBLIC", pk, nullptr);
  row(l, "BACK UP TO SD", ">", [](lv_event_t*) { keyBackup(); });
  row(l, "RESTORE FROM SD", ">", [](lv_event_t*) {
    confirm("Replace the key with /scooby_mt_key.txt from the SD card?", "[ RESTORE ]", keyRestoreYes); });
  row(l, "GENERATE NEW", ">", [](lv_event_t*) {
    confirm("Make a new key? Back up first: old DMs to you can't be read with the new one.", "[ NEW KEY ]", keyNewYes); });
  section(l, "NODES");
  snprintf(r, sizeof r, "%u nodes >", Mt::nodeCount());
  row(l, "CLEAR LAST HEARD", r, [](lv_event_t*) {
    confirm("Clear Meshtastic last heard? Nodes return when heard again.", "[ CLEAR ]", mtClearYes); });
  lv_group_focus_obj(first);
}

// ============================================================== MESHCORE
static void mcApply(float f, float bw, uint8_t sf, uint8_t cr) {
  NodePrefs* np = the_mesh.getNodePrefs();
  np->freq = f; np->bw = bw; np->sf = sf; np->cr = cr;
  the_mesh.savePrefs();
  radio_driver.setParams(f, bw, sf, cr);
}
static void mcPresetCb(lv_event_t*) {
  const int i = mcPresetNow();
  const McPreset& p = kMcNa[(i + 1) % kMcNaN];
  mcApply(p.freq, p.bw, p.sf, p.cr);
  go(P_SET_MC);
}
static void mcFreqSet(const char* t) {
  const float f = atof(t);
  if (f < 902.0f || f > 928.0f) { toast("North America: 902 - 928 MHz"); return; }
  const NodePrefs* np = the_mesh.getNodePrefs();
  mcApply(f, np->bw, np->sf, np->cr);
  go(P_SET_MC);
}
static void mcFreqCb(lv_event_t*) {
  char cur[16];
  snprintf(cur, sizeof cur, "%.3f", (double)the_mesh.getNodePrefs()->freq);
  numPopup("MESHCORE FREQUENCY", "MHz, North America: 902 - 928", cur, mcFreqSet);
}
static void mcBwCb(lv_event_t*) {
  static const float bws[] = { 62.5f, 125.0f, 250.0f, 500.0f };
  const NodePrefs* np = the_mesh.getNodePrefs();
  int i = 0;
  for (; i < 4; i++) if (fabsf(bws[i] - np->bw) < 0.01f) break;
  mcApply(np->freq, bws[(i + 1) % 4], np->sf, np->cr);
  go(P_SET_MC);
}
static void mcSfCb(lv_event_t*) {
  const NodePrefs* np = the_mesh.getNodePrefs();
  mcApply(np->freq, np->bw, np->sf >= 12 ? 5 : np->sf + 1, np->cr);
  go(P_SET_MC);
}
static void mcCrCb(lv_event_t*) {
  const NodePrefs* np = the_mesh.getNodePrefs();
  mcApply(np->freq, np->bw, np->sf, np->cr >= 8 ? 5 : np->cr + 1);
  go(P_SET_MC);
}
static void mcTxCb(lv_event_t*) {
  NodePrefs* np = the_mesh.getNodePrefs();
  static const int8_t pw[] = { 22, 20, 17, 14, 10, 5 };
  int i = 0;
  for (; i < 6; i++) if (pw[i] == np->tx_power_dbm) break;
  np->tx_power_dbm = pw[(i + 1) % 6];
  the_mesh.savePrefs();
  radio_driver.setTxPower(np->tx_power_dbm);
  go(P_SET_MC);
}
static void mcAdvertCb(lv_event_t*) { advertPopup(); }
// Clear last heard = remove the MeshCore contacts, except favourites and anyone with a DM
// thread (they hold the key a reply needs). Contacts come back with their next advert.
static void mcClearYes(void*) {
  static uint8_t keys[MAX_CONTACTS][PUB_KEY_SIZE];
  int nk = 0;
  ContactsIterator it = the_mesh.startContactsIterator();
  ContactInfo c;
  while (nk < MAX_CONTACTS && it.hasNext(&the_mesh, c)) {
    if (c.flags & 0x01) continue;                       // favourite
    if (mc::hasDm(c.id.pub_key)) continue;
    memcpy(keys[nk++], c.id.pub_key, PUB_KEY_SIZE);
  }
  int n = 0;
  for (int i = 0; i < nk; i++) {
    ContactInfo* p = the_mesh.lookupContactByPubKey(keys[i], PUB_KEY_SIZE);
    if (p && the_mesh.removeContact(*p)) n++;
  }
  if (n) store.saveContacts(&the_mesh);
  char t[48];
  snprintf(t, sizeof t, "Cleared %d contact%s", n, n == 1 ? "" : "s");
  toast(t);
  go(P_SET_MC);
}
void buildSetMc(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  const NodePrefs* np = the_mesh.getNodePrefs();
  const int mp = mcPresetNow();
  char r[32];
  section(l, "RADIO (North America)");
  lv_obj_t* first = row(l, "PRESET", mp >= 0 ? kMcNa[mp].name : "CUSTOM", mcPresetCb);
  snprintf(r, sizeof r, "%.3f MHz", (double)np->freq);
  row(l, "FREQUENCY", r, mcFreqCb);
  snprintf(r, sizeof r, "%g kHz", (double)np->bw);
  row(l, "BANDWIDTH", r, mcBwCb);
  snprintf(r, sizeof r, "SF%u", np->sf);
  row(l, "SPREADING FACTOR", r, mcSfCb);
  snprintf(r, sizeof r, "4/%u", np->cr);
  row(l, "CODING RATE", r, mcCrCb);
  snprintf(r, sizeof r, "%d dBm", np->tx_power_dbm);
  row(l, "TX POWER", r, mcTxCb);
  section(l, "NODE");
  row(l, "NAME", np->node_name, nullptr);
  row(l, "SEND ADVERT", "FLOOD / LOCAL >", mcAdvertCb);
  snprintf(r, sizeof r, "%d contacts >", the_mesh.getNumContacts());
  row(l, "CLEAR LAST HEARD", r, [](lv_event_t*) {
    confirm("Clear MeshCore last heard? Favourites and DM partners stay.", "[ CLEAR ]", mcClearYes); });
  lv_obj_t* h = label(l, "Editing FREQ/BW/SF/CR makes it CUSTOM.\nThe SF here is MeshCore's own; TASTIC\nhas its own SF override.", FONT_S, pal().dim);
  lv_obj_set_width(h, lv_pct(100));
  lv_group_focus_obj(first);
}

}  // namespace sd
