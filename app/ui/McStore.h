// Scooby Deck v3 — MeshCore chat history and delivery tracking (RAM, PSRAM ring).
// Copyright (C) 2026 Scooby Deck contributors. GPL-3.0-or-later.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct ContactInfo;

namespace mc {

static const uint16_t MAX_MSGS = 128;
enum State : uint8_t { ST_NONE = 0, ST_PENDING, ST_ACKED, ST_FAILED, ST_SENT };

struct Msg {
  uint32_t ts;          // our RTC (epoch) when stored
  uint32_t ms;          // millis() when stored
  uint32_t ack;         // expected ACK crc (outgoing DM)
  uint32_t deadline;    // millis() the ACK is due by
  uint8_t  chan;        // channel slot, or 0xFF for a DM
  uint8_t  peer[6];     // DM peer public-key prefix
  uint8_t  out;
  uint8_t  state;
  uint8_t  unread;
  uint8_t  attempt;
  uint8_t  hops;
  int8_t   snr4;
  char     name[24];    // sender (channel) / peer (DM)
  char     text[164];
};
static const uint8_t DM = 0xFF;

void     begin();
void     loop(uint32_t now);                         // ACK timeouts and retries
uint32_t seq();                                      // bumps on every change
uint16_t count();
const Msg* at(uint16_t i);                           // 0 = oldest

void     onChannel(uint8_t chan, const char* text, int8_t snr4, uint8_t hops);
void     onDm(const ContactInfo& from, const char* text, int8_t snr4, uint8_t hops);
void     onAck(uint32_t crc);
bool     sendChannel(uint8_t chan, const char* text);
bool     sendDm(const uint8_t* pubKey, const char* text);

uint16_t unreadChannel(uint8_t chan);
uint16_t unreadDm(const uint8_t* prefix6);           // nullptr = all DMs
uint16_t unreadTotal();
uint16_t channelCount(uint8_t chan);
void     markChannelRead(uint8_t chan);
void     markDmRead(const uint8_t* prefix6);
bool     hasDm(const uint8_t* prefix6);               // any DM with this peer stored
void     deleteDm(const uint8_t* prefix6);
void     dropChannel(uint8_t chan);                  // channel removed
uint32_t ackRx();                                    // ACKs matched
uint32_t txCount();

}  // namespace mc
