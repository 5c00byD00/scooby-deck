// Scooby Deck v3 — CORE (MeshCore) and TASTIC (Meshtastic) pages: last heard, channels with
// counters, DMs, chats with delivery state, channel add/remove (slot 0 protected).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include <MyMesh.h>
#include <DataStore.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TransportKeyStore.h>
// base64.hpp (densaugeo) is header-only and already compiled into BaseChatMesh.cpp
unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
#include "Ui.h"
#include "McStore.h"
#include "DualMeshConfig.h"
#include "DualMeshMt.h"

extern DataStore store;

namespace sd {

using namespace DualMesh;
static const int kListH = 184, kBtnY = 186, kBtnH = 32;
static char s_draft[200];
static Page s_draftPage = P_NONE;

// ============================================================== shared helpers
static uint32_t nowSecs() { return the_mesh.getRTCClock()->getCurrentTime(); }

static void hex(const uint8_t* b, int n, char* out) {
  for (int i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
}

static char mcTypeChar(uint8_t t) {
  switch (t) { case ADV_TYPE_CHAT: return 'C'; case ADV_TYPE_REPEATER: return 'R';
               case ADV_TYPE_ROOM: return 'S'; case ADV_TYPE_SENSOR: return 'N'; default: return '?'; }
}

// MeshCore contacts, newest first
static const int HEARD_MAX = 48;
static ContactInfo* s_heard[HEARD_MAX];
static int s_nHeard = 0;
static void collectHeard() {
  s_nHeard = 0;
  ContactsIterator it = the_mesh.startContactsIterator();
  ContactInfo tmp;
  for (int i = 0; i < the_mesh.getNumContacts() && s_nHeard < HEARD_MAX; i++) {
    if (!it.hasNext(&the_mesh, tmp)) break;
    ContactInfo* c = the_mesh.lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
    if (c) s_heard[s_nHeard++] = c;
  }
  // insertion sort by lastmod, newest first
  for (int i = 1; i < s_nHeard; i++) {
    ContactInfo* v = s_heard[i];
    int j = i - 1;
    while (j >= 0 && s_heard[j]->lastmod < v->lastmod) { s_heard[j + 1] = s_heard[j]; j--; }
    s_heard[j + 1] = v;
  }
}

uint32_t meshSignature(Page p) {
  if (p >= P_CORE && p <= P_CORE_HEARD) {
    uint32_t s = mc::seq() * 31u + (uint32_t)the_mesh.getNumContacts();
    if (p == P_CORE || p == P_CORE_HEARD) {   // re-sort when someone new was heard
      uint32_t newest = 0;
      ContactsIterator it = the_mesh.startContactsIterator();
      ContactInfo c;
      while (it.hasNext(&the_mesh, c)) if (c.lastmod > newest) newest = c.lastmod;
      s = s * 31u + newest;
    }
    return s;
  }
  return Mt::changeSeq() * 31u + Mt::nodeCount();
}

static void chatList(lv_obj_t* b, lv_obj_t** out) {
  lv_obj_t* l = list(b, 0, 0, 320, 190);
  lv_obj_set_style_pad_row(l, 3, 0);
  *out = l;
}
static void chatLine(lv_obj_t* lst, const char* text, bool out) {
  lv_obj_t* l = label(lst, text, FONT_M, out ? pal().dim : pal().ink);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
}
static void draftCb(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  strncpy(s_draft, lv_textarea_get_text(ta), sizeof s_draft - 1);
}
static lv_obj_t* chatInput(lv_obj_t* b, lv_event_cb_t sendCb, size_t maxLen) {
  if (s_draftPage != page()) { s_draft[0] = 0; s_draftPage = page(); }
  lv_obj_t* ta = textField(b, 0, 192, 320, s_draft, maxLen);
  lv_textarea_set_placeholder_text(ta, "type, ENTER = send");
  lv_obj_add_event_cb(ta, draftCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(ta, sendCb, LV_EVENT_READY, nullptr);
  lv_group_focus_obj(ta);
  lv_group_set_editing(group(), true);
  return ta;
}

// ============================================================== CORE
static void coreHeardCb(lv_event_t* e);
static void coreDmsCb(lv_event_t*)   { go(P_CORE_DMS); }
static void coreEditCb(lv_event_t*)  { go(P_CORE_EDIT); }
static void coreAllCb(lv_event_t*)   { go(P_CORE_HEARD); }
static void coreChanCb(lv_event_t* e) {
  ctx.chan = (int16_t)(intptr_t)lv_event_get_user_data(e);
  go(P_CORE_CHAT);
}

static void heardRows(lv_obj_t* l, int max) {
  collectHeard();
  const uint32_t now = nowSecs();
  int shown = 0;
  for (int i = 0; i < s_nHeard && shown < max; i++, shown++) {
    const ContactInfo* c = s_heard[i];
    char age[8], right[20];
    ageText(now > c->lastmod ? now - c->lastmod : 0, age, sizeof age);
    snprintf(right, sizeof right, "%c %s", mcTypeChar(c->type), age);
    row(l, c->name[0] ? c->name : "(no name)", right, coreHeardCb, (void*)(intptr_t)i);
  }
  if (!s_nHeard) label(l, "  -- nobody heard yet --", FONT_M, pal().dim);
}

void buildCore(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, kListH);
  section(l, "LAST HEARD");
  heardRows(l, 3);
  row(l, "  [ all nodes ]", nullptr, coreAllCb);
  section(l, "CHANNELS");
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd) || !cd.name[0]) continue;
    const uint16_t un = mc::unreadChannel(i);
    char left[40], right[12];
    snprintf(left, sizeof left, "# %s%s", cd.name, i == 0 ? " (primary)" : "");
    snprintf(right, sizeof right, "(%u)", un ? un : mc::channelCount(i));
    row(l, left, right, coreChanCb, (void*)(intptr_t)i, un > 0);
  }
  char dm[24];
  snprintf(dm, sizeof dm, "[ DMs (%u) ]", mc::unreadDm(nullptr));
  button(b, dm, 0, kBtnY, 104, kBtnH, coreDmsCb);
  button(b, "[ ADVERT ]", 108, kBtnY, 104, kBtnH, [](lv_event_t*) { advertPopup(); });
  button(b, "[ EDIT ]", 216, kBtnY, 104, kBtnH, coreEditCb);
}

// ---- advert: FLOOD (repeaters pass it on) or LOCAL (zero hop, direct neighbours only)
bool mcAdvert(bool flood) {
  const NodePrefs* np = the_mesh.getNodePrefs();
  mesh::Packet* pkt = the_mesh.createSelfAdvert(np->node_name);   // no GPS yet: no location
  if (!pkt) return false;
  if (!flood) { the_mesh.sendZeroHop(pkt); return true; }
  // as the companion app's "flood advert" (CMD_SEND_SELF_ADVERT 1): default scope if one is set
  TransportKey scope;
  memcpy(&scope.key, np->default_scope_key, sizeof(scope.key));
  if (scope.isNull()) {
    the_mesh.sendFlood(pkt, (uint32_t)0, (uint8_t)(np->path_hash_mode + 1));
  } else {
    uint16_t codes[2] = { scope.calcTransportCode(pkt), 0 };
    the_mesh.sendFlood(pkt, codes, (uint32_t)0, (uint8_t)(np->path_hash_mode + 1));
  }
  return true;
}
static void advFloodCb(lv_event_t*) { popupClose(); toast(mcAdvert(true) ? "Flood advert sent" : "Advert failed"); }
static void advLocalCb(lv_event_t*) { popupClose(); toast(mcAdvert(false) ? "Local advert sent (zero hop)" : "Advert failed"); }
void advertPopup() {
  lv_obj_t* p = popup("SEND ADVERT", 150);
  lv_obj_t* q = label(p,
      "FLOOD: repeaters pass it on,\n"
      "the whole mesh can learn you.\n"
      "LOCAL: zero hop, only nodes in\n"
      "direct radio range hear it.", FONT_M);
  lv_obj_set_pos(q, 0, 22);
  lv_obj_t* fl = popupButton(p, 0, 3, "[ FLOOD ]", advFloodCb);
  popupButton(p, 1, 3, "[ LOCAL ]", advLocalCb);
  popupButton(p, 2, 3, "[ CANCEL ]", [](lv_event_t*) { popupClose(); });
  lv_group_focus_obj(fl);
}

void buildCoreHeard(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  char t[32];
  snprintf(t, sizeof t, "LAST HEARD (%d)", the_mesh.getNumContacts());
  section(l, t);
  heardRows(l, HEARD_MAX);
}

// node popup
static uint8_t s_popKey[32];
static void popDmCb(lv_event_t*) {
  memcpy(ctx.mcPeer, s_popKey, 32);
  ctx.chan = -1;
  popupClose();
  go(P_CORE_CHAT);
}
static void delNodeYes(void*) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(s_popKey, PUB_KEY_SIZE);
  if (c) { the_mesh.removeContact(*c); store.saveContacts(&the_mesh); toast("Node removed"); }
}
static void popDelCb(lv_event_t*) { confirm("Remove this node? It comes back when heard again.", "[ REMOVE ]", delNodeYes); }
static void popCloseCb(lv_event_t*) { popupClose(); }
static void coreHeardCb(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= s_nHeard) return;
  const ContactInfo* c = s_heard[i];
  memcpy(s_popKey, c->id.pub_key, 32);
  lv_obj_t* p = popup(c->name[0] ? c->name : "NODE", 170);
  char k[17], age[8], buf[200];
  hex(c->id.pub_key, 8, k);
  const uint32_t now = nowSecs();
  ageText(now > c->lastmod ? now - c->lastmod : 0, age, sizeof age);
  const char* type = c->type == ADV_TYPE_CHAT ? "companion" : c->type == ADV_TYPE_REPEATER ? "repeater"
                   : c->type == ADV_TYPE_ROOM ? "room server" : c->type == ADV_TYPE_SENSOR ? "sensor" : "?";
  char path[16];
  if (c->out_path_len == OUT_PATH_UNKNOWN) snprintf(path, sizeof path, "flood");
  else snprintf(path, sizeof path, "%u hops", (unsigned)(c->out_path_len & 63));
  snprintf(buf, sizeof buf, "TYPE  %s\nKEY   %s\nHEARD %s ago\nPATH  %s", type, k, age, path);
  lv_obj_t* t = label(p, buf);
  lv_obj_set_pos(t, 0, 24);
  lv_obj_t* dm = popupButton(p, 0, 3, "[ DM ]", popDmCb);
  popupButton(p, 1, 3, "[ DEL ]", popDelCb);
  popupButton(p, 2, 3, "[ CLOSE ]", popCloseCb);
  if (c->type != ADV_TYPE_CHAT) lv_obj_add_state(dm, LV_STATE_DISABLED);
  lv_group_focus_obj(dm);
}

// DMs list
static uint8_t s_dmPeers[16][6];
static void coreDmRowCb(lv_event_t* e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  ContactInfo* c = the_mesh.lookupContactByPubKey(s_dmPeers[i], 6);
  if (!c) { toast("That node is no longer in the contact list"); return; }
  memcpy(ctx.mcPeer, c->id.pub_key, 32);
  ctx.chan = -1;
  go(P_CORE_CHAT);
}
void buildCoreDms(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  section(l, "DIRECT MESSAGES");
  int n = 0;
  for (int i = (int)mc::count() - 1; i >= 0 && n < 16; i--) {
    const mc::Msg* m = mc::at(i);
    if (m->chan != mc::DM) continue;
    bool dup = false;
    for (int k = 0; k < n; k++) if (!memcmp(s_dmPeers[k], m->peer, 6)) { dup = true; break; }
    if (dup) continue;
    memcpy(s_dmPeers[n], m->peer, 6);
    const uint16_t un = mc::unreadDm(m->peer);
    char right[12];
    snprintf(right, sizeof right, "(%u)", un);
    row(l, m->name, un ? right : nullptr, coreDmRowCb, (void*)(intptr_t)n, un > 0);
    n++;
  }
  if (!n) label(l, "  -- none yet --\n  open a node in LAST HEARD\n  and pick [ DM ]", FONT_M, pal().dim);
}

// chat
static void coreSendCb(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* t = lv_textarea_get_text(ta);
  if (!t || !t[0]) return;
  const bool ok = ctx.chan >= 0 ? mc::sendChannel((uint8_t)ctx.chan, t) : mc::sendDm(ctx.mcPeer, t);
  if (!ok) toast(ctx.chan >= 0 ? "Send failed" : "Send failed: node unknown");
  s_draft[0] = 0;
  lv_textarea_set_text(ta, "");
  go(P_CORE_CHAT);
}
void buildCoreChat(lv_obj_t* b) {
  char title[48];
  ChannelDetails cd;
  const ContactInfo* peer = nullptr;
  if (ctx.chan >= 0) {
    if (!the_mesh.getChannel(ctx.chan, cd) || !cd.name[0]) { go(P_CORE); return; }
    snprintf(title, sizeof title, "CORE  #%s", cd.name);
    mc::markChannelRead((uint8_t)ctx.chan);
  } else {
    peer = the_mesh.lookupContactByPubKey(ctx.mcPeer, PUB_KEY_SIZE);
    snprintf(title, sizeof title, "CORE  @%s", peer ? peer->name : "?");
    mc::markDmRead(ctx.mcPeer);
  }
  setTitle(title);
  lv_obj_t* l;
  chatList(b, &l);
  const uint32_t now = nowSecs();
  int shown = 0;
  for (uint16_t i = 0; i < mc::count(); i++) {
    const mc::Msg* m = mc::at(i);
    const bool mine = ctx.chan >= 0 ? m->chan == (uint8_t)ctx.chan : (m->chan == mc::DM && !memcmp(m->peer, ctx.mcPeer, 6));
    if (!mine) continue;
    char age[8], line[240];
    ageText(now > m->ts ? now - m->ts : 0, age, sizeof age);
    if (m->out) {
      const char* st = m->state == mc::ST_ACKED ? " [ACK]" : m->state == mc::ST_FAILED ? " [FAIL]"
                     : m->state == mc::ST_PENDING ? (m->attempt ? " [RETRY]" : " [..]") : " [SENT]";
      snprintf(line, sizeof line, "ME> %s%s %s", m->text, st, age);
    } else {
      snprintf(line, sizeof line, "<%s> %s  %s %+.1f", m->name[0] ? m->name : "?", m->text, age, m->snr4 / 4.0);
    }
    chatLine(l, line, m->out);
    shown++;
  }
  if (!shown) chatLine(l, "-- no messages yet --", true);
  lv_obj_update_layout(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  chatInput(b, coreSendCb, 150);
}

// channel edit
static lv_obj_t* s_fName = nullptr;
static lv_obj_t* s_fKey = nullptr;
static int s_editSlot = -1;

static bool parseMcKey(const char* name, const char* txt, uint8_t out[32], int& len) {
  memset(out, 0, 32);
  while (*txt == ' ') txt++;
  const size_t n = strlen(txt);
  if (!n) {
    if (name[0] != '#') return false;
    // hashtag channel: the key is the first 16 bytes of sha256("#name"), as the MeshCore apps do
    uint8_t h[32];
    mesh::Utils::sha256(h, 32, (const uint8_t*)name, strlen(name));
    memcpy(out, h, 16); len = 16; return true;
  }
  if (n == 32 || n == 64) {
    bool ok = true;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)txt[i])) { ok = false; break; }
    if (ok) {
      for (size_t i = 0; i < n / 2; i++) { unsigned v; sscanf(txt + i * 2, "%2x", &v); out[i] = (uint8_t)v; }
      len = (int)(n / 2); return true;
    }
  }
  uint8_t tmp[48];
  if (n > 64) return false;
  len = (int)decode_base64((unsigned char*)txt, n, tmp);
  if (len != 16 && len != 32) return false;
  memcpy(out, tmp, len);
  return true;
}
static void mcAddSaveCb(lv_event_t*) {
  char name[32];
  strncpy(name, lv_textarea_get_text(s_fName), sizeof name - 1);
  name[sizeof name - 1] = 0;
  const char* key = lv_textarea_get_text(s_fKey);
  if (!name[0]) { toast("Enter a channel name"); return; }
  ChannelDetails cd;
  memset(&cd, 0, sizeof cd);
  int len = 0;
  if (!strcasecmp(name, "public") && !key[0]) key = "izOH6cXN6mrJ5e26oRXNcg==";
  if (!parseMcKey(name, key, cd.channel.secret, len)) { toast("Bad key: 32/64 hex or base64, or #name with no key"); return; }
  strncpy(cd.name, name, sizeof cd.name - 1);
  int slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails x;
    if (the_mesh.getChannel(i, x) && !x.name[0]) { slot = i; break; }
  }
  if (slot < 0) { toast("All channel slots are in use"); return; }
  the_mesh.setChannel(slot, cd);
  store.saveChannels(&the_mesh);
  popupClose();
  toast("Channel added");
}
static void mcAddCb(lv_event_t*) {
  lv_obj_t* p = popup("ADD MESHCORE CHANNEL", 196);
  lv_obj_t* a = label(p, "NAME (#name = hashtag channel)", FONT_S, pal().dim);
  lv_obj_set_pos(a, 0, 22);
  s_fName = textField(p, 0, 34, 288, "", 31);
  lv_obj_t* k = label(p, "KEY  hex / base64 (blank for #name)", FONT_S, pal().dim);
  lv_obj_set_pos(k, 0, 66);
  s_fKey = textField(p, 0, 78, 288, "", 64);
  lv_obj_t* h = label(p, "ENTER moves on; [ SAVE ] adds it", FONT_S, pal().dim);
  lv_obj_set_pos(h, 0, 108);
  popupButton(p, 0, 2, "[ SAVE ]", mcAddSaveCb);
  popupButton(p, 1, 2, "[ CANCEL ]", popCloseCb);
  lv_group_focus_obj(s_fName);
}
static void mcRemoveYes(void*) {
  if (s_editSlot <= 0) return;
  ChannelDetails empty;
  memset(&empty, 0, sizeof empty);
  the_mesh.setChannel(s_editSlot, empty);
  store.saveChannels(&the_mesh);
  mc::dropChannel((uint8_t)s_editSlot);
  toast("Channel removed");
}
static void mcSlotCb(lv_event_t* e) {
  s_editSlot = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_editSlot == 0) { toast("Slot 0 is the primary channel: it can't be removed"); return; }
  ChannelDetails cd;
  the_mesh.getChannel(s_editSlot, cd);
  char q[80];
  snprintf(q, sizeof q, "Remove #%s (slot %d) and its messages?", cd.name, s_editSlot);
  confirm(q, "[ REMOVE ]", mcRemoveYes);
}
void buildCoreEdit(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, kListH);
  section(l, "CHANNEL SLOTS  (tap = remove)");
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(i, cd)) continue;
    char left[48], right[8];
    snprintf(left, sizeof left, "%d %s%s", i, cd.name[0] ? "# " : "", cd.name[0] ? cd.name : "(empty)");
    snprintf(right, sizeof right, "%s", i == 0 ? "LOCK" : "");
    row(l, left, right, cd.name[0] ? mcSlotCb : nullptr, (void*)(intptr_t)i);
  }
  lv_obj_t* add = button(b, "[ + ADD ]", 0, kBtnY, 158, kBtnH, mcAddCb);
  button(b, "[ DONE ]", 162, kBtnY, 158, kBtnH, [](lv_event_t*) { back(); });
  lv_group_focus_obj(add);
}

// ============================================================== TASTIC
static void mtHeardCb(lv_event_t* e);
static void mtDmsCb(lv_event_t*)  { go(P_MT_DMS); }
static void mtEditCb(lv_event_t*) { go(P_MT_EDIT); }
static void mtAllCb(lv_event_t*)  { go(P_MT_HEARD); }
static void mtChanCb(lv_event_t* e) { ctx.chan = (int16_t)(intptr_t)lv_event_get_user_data(e); go(P_MT_CHAT); }

static const char* mtChanName(uint8_t i) {
  const MtChannel& c = cfg().ch[i];
  return (i == 0 && !c.name[0]) ? mtPrimaryName() : c.name;
}

static void mtHeardRows(lv_obj_t* l, int max) {
  const uint8_t n = Mt::nodeCount();
  for (uint8_t i = 0; i < n && i < max; i++) {
    Mt::Node nd;
    if (!Mt::nodeAt(i, nd)) continue;
    char name[40], left[64], age[8], right[24];
    Mt::nodeName(nd.num, name, sizeof name);
    snprintf(left, sizeof left, "%s !%08lx", name, (unsigned long)nd.num);
    ageText((millis() - nd.lastMs) / 1000, age, sizeof age);
    snprintf(right, sizeof right, "%s %+.1f", age, nd.snr4 / 4.0);
    row(l, left, right, mtHeardCb, (void*)(uintptr_t)nd.num);
  }
  if (!n) label(l, "  -- nobody heard yet --", FONT_M, pal().dim);
}

void buildTastic(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, kListH);
  section(l, "LAST HEARD");
  mtHeardRows(l, 3);
  row(l, "  [ all nodes ]", nullptr, mtAllCb);
  section(l, "CHANNELS");
  for (uint8_t i = 0; i < MT_CHANNELS; i++) {
    const MtChannel& c = cfg().ch[i];
    if (!c.enabled && i != 0) continue;
    const uint16_t un = Mt::unread(i);
    char left[40], right[12];
    snprintf(left, sizeof left, "# %s%s", mtChanName(i), i == 0 ? " (primary)" : "");
    snprintf(right, sizeof right, "(%u)", un ? un : Mt::chanMsgCount(i));
    row(l, left, right, mtChanCb, (void*)(intptr_t)i, un > 0);
  }
  char dm[24];
  snprintf(dm, sizeof dm, "[ DMs (%u) ]", Mt::unread(Mt::DM_CHAN));
  button(b, dm, 0, kBtnY, 158, kBtnH, mtDmsCb);
  button(b, "[ EDIT ]", 162, kBtnY, 158, kBtnH, mtEditCb);
}

void buildMtHeard(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  char t[32];
  snprintf(t, sizeof t, "LAST HEARD (%u)", Mt::nodeCount());
  section(l, t);
  mtHeardRows(l, 64);
}

static uint32_t s_mtPop = 0;
static void mtPopDmCb(lv_event_t*) { ctx.mtPeer = s_mtPop; ctx.chan = -1; popupClose(); go(P_MT_CHAT); }
static void mtDelYes(void*) { Mt::deleteNode(s_mtPop); toast("Node removed"); }
static void mtPopDelCb(lv_event_t*) { confirm("Remove this node? It comes back when heard again.", "[ REMOVE ]", mtDelYes); }
static void mtHeardCb(lv_event_t* e) {
  s_mtPop = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  Mt::Node nd;
  if (!Mt::nodeByNum(s_mtPop, nd)) return;
  char sn[8] = "", ln[40] = "", hw[24], buf[220], age[8], title[48];
  Mt::nodeNames(nd.num, sn, sizeof sn, ln, sizeof ln);
  snprintf(title, sizeof title, "%s !%08lx", ln[0] ? ln : "NODE", (unsigned long)nd.num);
  lv_obj_t* p = popup(title, 180);
  ageText((millis() - nd.lastMs) / 1000, age, sizeof age);
  char hops[8];
  if (nd.hops == 0xFF) snprintf(hops, sizeof hops, "?"); else snprintf(hops, sizeof hops, "%u", nd.hops);
  snprintf(buf, sizeof buf, "SHORT %s  ROLE %s\nHW    %s\nHEARD %s ago  HOPS %s\nRSSI %d  SNR %+.1f  KEY %s",
           sn[0] ? sn : "-", nd.hasUser ? Mt::roleName(nd.role) : "?",
           nd.hasUser ? Mt::hwName(nd.hwModel, hw, sizeof hw) : "?", age, hops,
           nd.rssi, nd.snr4 / 4.0, nd.hasKey ? "yes" : "no");
  lv_obj_t* t = label(p, buf);
  lv_obj_set_width(t, 288);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(t, 0, 22);
  lv_obj_t* dm = popupButton(p, 0, 3, "[ DM ]", mtPopDmCb);
  popupButton(p, 1, 3, "[ DEL ]", mtPopDelCb);
  popupButton(p, 2, 3, "[ CLOSE ]", popCloseCb);
  lv_group_focus_obj(dm);
}

static uint32_t s_mtDmPeers[16];
static void mtDmRowCb(lv_event_t* e) {
  ctx.mtPeer = s_mtDmPeers[(int)(intptr_t)lv_event_get_user_data(e)];
  ctx.chan = -1;
  go(P_MT_CHAT);
}
void buildMtDms(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, 218);
  section(l, "DIRECT MESSAGES");
  int n = 0;
  for (int i = (int)Mt::msgCount() - 1; i >= 0 && n < 16; i--) {
    Mt::Msg m;
    if (!Mt::msgAt(i, m) || m.chan != Mt::DM_CHAN) continue;
    const uint32_t peer = m.outgoing ? m.peer : m.from;
    bool dup = false;
    for (int k = 0; k < n; k++) if (s_mtDmPeers[k] == peer) { dup = true; break; }
    if (dup) continue;
    s_mtDmPeers[n] = peer;
    char name[40], right[12];
    Mt::nodeName(peer, name, sizeof name);
    const uint16_t un = Mt::unreadDm(peer);
    snprintf(right, sizeof right, "(%u)", un);
    row(l, name, un ? right : nullptr, mtDmRowCb, (void*)(intptr_t)n, un > 0);
    n++;
  }
  if (!n) label(l, "  -- none yet --\n  open a node in LAST HEARD\n  and pick [ DM ]", FONT_M, pal().dim);
}

static void mtSendCb(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* t = lv_textarea_get_text(ta);
  if (!t || !t[0]) return;
  bool ok;
  if (ctx.chan >= 0) ok = Mt::sendChannel((uint8_t)ctx.chan, t);
  else ok = Mt::sendDm(ctx.mtPeer, t) != 0;
  if (!ok) toast(cfg().mode == MODE_MESHCORE ? "Send failed: listening to MeshCore only" : "Send failed");
  s_draft[0] = 0;
  lv_textarea_set_text(ta, "");
  go(P_MT_CHAT);
}
void buildMtChat(lv_obj_t* b) {
  char title[48];
  if (ctx.chan >= 0) {
    snprintf(title, sizeof title, "TASTIC  #%s", mtChanName((uint8_t)ctx.chan));
    Mt::markRead((uint8_t)ctx.chan);
  } else {
    char name[32];
    Mt::nodeName(ctx.mtPeer, name, sizeof name);
    snprintf(title, sizeof title, "TASTIC  @%s", name);
    Mt::markReadDm(ctx.mtPeer);
  }
  setTitle(title);
  lv_obj_t* l;
  chatList(b, &l);
  int shown = 0;
  for (uint16_t i = 0; i < Mt::msgCount(); i++) {
    Mt::Msg m;
    if (!Mt::msgAt(i, m)) continue;
    const bool mine = ctx.chan >= 0 ? m.chan == (uint8_t)ctx.chan
                                    : (m.chan == Mt::DM_CHAN && (m.outgoing ? m.peer : m.from) == ctx.mtPeer);
    if (!mine) continue;
    char age[8], line[260];
    ageText((millis() - m.ms) / 1000, age, sizeof age);
    if (m.outgoing) {
      const char* st;
      if (m.chan == Mt::DM_CHAN) st = m.state == 2 ? " [ACK]" : m.state == 3 ? " [FAIL]" : m.state == 4 ? " [NO KEY]" : " [..]";
      else st = m.state == 2 ? " [RELAYED]" : m.state == 3 ? " [FAIL]" : " [SENT]";
      snprintf(line, sizeof line, "ME> %s%s %s", m.text, st, age);
    } else {
      char name[32];
      Mt::nodeName(m.from, name, sizeof name);
      snprintf(line, sizeof line, "<%s> %s  %s %+.1f", name, m.text, age, m.snr4 / 4.0);
    }
    chatLine(l, line, m.outgoing);
    shown++;
  }
  if (!shown) chatLine(l, "-- no messages yet --", true);
  lv_obj_update_layout(l);
  lv_obj_scroll_to_y(l, LV_COORD_MAX, LV_ANIM_OFF);
  chatInput(b, mtSendCb, 200);
}

// channel edit
static void mtAddSaveCb(lv_event_t*) {
  char name[12];
  strncpy(name, lv_textarea_get_text(s_fName), sizeof name - 1);
  name[sizeof name - 1] = 0;
  if (!name[0]) { toast("Enter a channel name (11 max)"); return; }
  int slot = -1;
  for (uint8_t i = 1; i < MT_CHANNELS; i++) if (!cfg().ch[i].enabled) { slot = i; break; }
  if (slot < 0) { toast("All 8 channel slots are in use"); return; }
  const char* key = lv_textarea_get_text(s_fKey);
  if (!setChannel((uint8_t)slot, name, key[0] ? key : "AQ==", true)) { toast("Bad key: base64, hex, or blank = default"); return; }
  save();
  applyChannels();
  popupClose();
  toast("Channel added");
}
static void mtAddCb(lv_event_t*) {
  lv_obj_t* p = popup("ADD MESHTASTIC CHANNEL", 196);
  lv_obj_t* a = label(p, "NAME (11 max)", FONT_S, pal().dim);
  lv_obj_set_pos(a, 0, 22);
  s_fName = textField(p, 0, 34, 288, "", 11);
  lv_obj_t* k = label(p, "PSK  base64 / hex (blank = default AQ==)", FONT_S, pal().dim);
  lv_obj_set_pos(k, 0, 66);
  s_fKey = textField(p, 0, 78, 288, "", 64);
  popupButton(p, 0, 2, "[ SAVE ]", mtAddSaveCb);
  popupButton(p, 1, 2, "[ CANCEL ]", popCloseCb);
  lv_group_focus_obj(s_fName);
}
static void mtRemoveYes(void*) {
  if (s_editSlot <= 0) return;
  memset(&cfg().ch[s_editSlot], 0, sizeof(MtChannel));
  save();
  applyChannels();
  toast("Channel removed");
}
static void mtSlotCb(lv_event_t* e) {
  s_editSlot = (int)(intptr_t)lv_event_get_user_data(e);
  if (s_editSlot == 0) { toast("Slot 0 is the primary channel: it can't be removed"); return; }
  char q[80];
  snprintf(q, sizeof q, "Remove #%s (slot %d)?", cfg().ch[s_editSlot].name, s_editSlot);
  confirm(q, "[ REMOVE ]", mtRemoveYes);
}
void buildMtEdit(lv_obj_t* b) {
  lv_obj_t* l = list(b, 0, 0, 320, kListH);
  section(l, "CHANNEL SLOTS  (tap = remove)");
  for (uint8_t i = 0; i < MT_CHANNELS; i++) {
    const MtChannel& c = cfg().ch[i];
    const bool used = i == 0 || c.enabled;
    char left[48], kh[8];
    snprintf(kh, sizeof kh, "%s", c.keyLen == 32 ? "AES256" : c.keyLen == 16 ? "AES128" : c.keyLen ? "PSK" : "OPEN");
    snprintf(left, sizeof left, "%u %s%s", i, used ? "# " : "", used ? mtChanName(i) : "(empty)");
    row(l, left, i == 0 ? "LOCK" : used ? kh : "", used ? mtSlotCb : nullptr, (void*)(intptr_t)i);
  }
  lv_obj_t* add = button(b, "[ + ADD ]", 0, kBtnY, 158, kBtnH, mtAddCb);
  button(b, "[ DONE ]", 162, kBtnY, 158, kBtnH, [](lv_event_t*) { back(); });
  lv_group_focus_obj(add);
}

}  // namespace sd
