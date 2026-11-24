// Scooby Deck v3 — MeshCore chat history and delivery tracking.
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <MyMesh.h>
#include "McStore.h"

namespace mc {

static Msg* s_ring = nullptr;
static uint16_t s_head = 0, s_count = 0;
static uint32_t s_seq = 0, s_ackRx = 0, s_tx = 0;

void begin() {
  if (s_ring) return;
  s_ring = (Msg*)heap_caps_calloc(MAX_MSGS, sizeof(Msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_ring) s_ring = (Msg*)calloc(MAX_MSGS, sizeof(Msg));
}
uint32_t seq() { return s_seq; }
uint16_t count() { return s_count; }
uint32_t ackRx() { return s_ackRx; }
uint32_t txCount() { return s_tx; }
const Msg* at(uint16_t i) { return (s_ring && i < s_count) ? &s_ring[(s_head + i) % MAX_MSGS] : nullptr; }

static Msg* push() {
  if (!s_ring) return nullptr;
  Msg* m;
  if (s_count < MAX_MSGS) { m = &s_ring[(s_head + s_count) % MAX_MSGS]; s_count++; }
  else { m = &s_ring[s_head]; s_head = (s_head + 1) % MAX_MSGS; }
  memset(m, 0, sizeof *m);
  m->ms = millis();
  m->ts = the_mesh.getRTCClock()->getCurrentTime();
  s_seq++;
  return m;
}

void onChannel(uint8_t chan, const char* text, int8_t snr4, uint8_t hops) {
  Msg* m = push();
  if (!m) return;
  m->chan = chan; m->unread = 1; m->snr4 = snr4; m->hops = hops;
  // MeshCore channel text is "<sender>: <message>"
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < (int)sizeof m->name) {
    memcpy(m->name, text, sep - text);
    strncpy(m->text, sep + 2, sizeof m->text - 1);
  } else {
    strncpy(m->text, text, sizeof m->text - 1);
  }
}

void onDm(const ContactInfo& from, const char* text, int8_t snr4, uint8_t hops) {
  Msg* m = push();
  if (!m) return;
  m->chan = DM; m->unread = 1; m->snr4 = snr4; m->hops = hops;
  memcpy(m->peer, from.id.pub_key, 6);
  strncpy(m->name, from.name, sizeof m->name - 1);
  strncpy(m->text, text, sizeof m->text - 1);
}

void onAck(uint32_t crc) {
  for (uint16_t i = 0; i < s_count; i++) {
    Msg& m = s_ring[(s_head + i) % MAX_MSGS];
    if (m.out && m.chan == DM && m.ack == crc && m.state == ST_PENDING) {
      m.state = ST_ACKED; s_ackRx++; s_seq++;
      return;
    }
  }
}

bool sendChannel(uint8_t chan, const char* text) {
  ChannelDetails cd;
  if (!the_mesh.getChannel(chan, cd) || !cd.name[0]) return false;
  const uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  const char* me = the_mesh.getNodePrefs()->node_name;
  const bool ok = the_mesh.sendGroupMessage(ts, cd.channel, me, text, strlen(text));
  Msg* m = push();
  if (m) {
    m->chan = chan; m->out = 1; m->state = ok ? ST_SENT : ST_FAILED;
    strncpy(m->name, me, sizeof m->name - 1);
    strncpy(m->text, text, sizeof m->text - 1);
  }
  if (ok) s_tx++;
  return ok;
}

static bool transmit(Msg& m) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(m.peer, 6);
  if (!ci) return false;
  uint32_t ack = 0, est = 0;
  const uint32_t ts = the_mesh.getRTCClock()->getCurrentTime();
  const int res = the_mesh.sendMessage(*ci, ts, m.attempt, m.text, ack, est);
  if (res == MSG_SEND_FAILED) return false;
  m.ack = ack;
  m.deadline = millis() + (est ? est : 8000) + 2000;
  s_tx++;
  return true;
}

bool sendDm(const uint8_t* pubKey, const char* text) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(pubKey, 32);
  if (!ci) return false;
  Msg* m = push();
  if (!m) return false;
  m->chan = DM; m->out = 1; m->state = ST_PENDING;
  memcpy(m->peer, pubKey, 6);
  strncpy(m->name, ci->name, sizeof m->name - 1);
  strncpy(m->text, text, sizeof m->text - 1);
  if (!transmit(*m)) { m->state = ST_FAILED; return false; }
  return true;
}

void loop(uint32_t now) {
  for (uint16_t i = 0; i < s_count; i++) {
    Msg& m = s_ring[(s_head + i) % MAX_MSGS];
    if (!m.out || m.state != ST_PENDING || (int32_t)(now - m.deadline) < 0) continue;
    // up to 3 tries, the last one as a flood (path reset), like the MeshCore apps
    if (m.attempt < 2) {
      m.attempt++;
      if (m.attempt == 2) {
        ContactInfo* ci = the_mesh.lookupContactByPubKey(m.peer, 6);
        if (ci) the_mesh.resetPathTo(*ci);
      }
      if (!transmit(m)) m.state = ST_FAILED;
    } else {
      m.state = ST_FAILED;
    }
    s_seq++;
  }
}

static bool peerIs(const Msg& m, const uint8_t* p6) { return m.chan == DM && (!p6 || !memcmp(m.peer, p6, 6)); }

uint16_t unreadChannel(uint8_t chan) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < s_count; i++) { const Msg* m = at(i); if (m->chan == chan && m->unread) n++; }
  return n;
}
uint16_t channelCount(uint8_t chan) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < s_count; i++) if (at(i)->chan == chan) n++;
  return n;
}
uint16_t unreadDm(const uint8_t* p6) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < s_count; i++) { const Msg* m = at(i); if (peerIs(*m, p6) && m->unread) n++; }
  return n;
}
uint16_t unreadTotal() {
  uint16_t n = 0;
  for (uint16_t i = 0; i < s_count; i++) if (at(i)->unread) n++;
  return n;
}
void markChannelRead(uint8_t chan) {
  bool ch = false;
  for (uint16_t i = 0; i < s_count; i++) { Msg& m = s_ring[(s_head + i) % MAX_MSGS]; if (m.chan == chan && m.unread) { m.unread = 0; ch = true; } }
  if (ch) s_seq++;
}
void markDmRead(const uint8_t* p6) {
  bool ch = false;
  for (uint16_t i = 0; i < s_count; i++) { Msg& m = s_ring[(s_head + i) % MAX_MSGS]; if (peerIs(m, p6) && m.unread) { m.unread = 0; ch = true; } }
  if (ch) s_seq++;
}
static void dropIf(bool (*pred)(const Msg&, const void*), const void* arg) {
  if (!s_ring) return;
  uint16_t w = 0;
  for (uint16_t i = 0; i < s_count; i++) {
    const Msg m = s_ring[(s_head + i) % MAX_MSGS];
    if (pred(m, arg)) continue;
    s_ring[(s_head + w) % MAX_MSGS] = m;
    w++;
  }
  s_count = w;
  s_seq++;
}
void deleteDm(const uint8_t* p6) {
  dropIf([](const Msg& m, const void* a) { return peerIs(m, (const uint8_t*)a); }, p6);
}
void dropChannel(uint8_t chan) {
  dropIf([](const Msg& m, const void* a) { return m.chan == *(const uint8_t*)a; }, &chan);
}

}  // namespace mc
