// DualMesh — the Meshtastic client stack on top of the SquachWatch codec:
// decode, names, node list, channel chats, PKI DMs, NodeInfo, client-mute
// (this node never rebroadcasts other nodes' packets).
#pragma once
#include <stdint.h>
#include <stddef.h>

class DualMeshRadioWrapper;

namespace DualMesh {
namespace Mt {

static const uint8_t DM_CHAN = 0xFF;      // Msg::chan of a direct message
static const uint16_t MAX_MSGS = 96;

struct Msg {
    uint32_t ms;          // millis() when stored
    uint32_t epoch;       // 0 = unknown
    uint32_t from;        // node number (ours for outgoing)
    uint32_t peer;        // DM: the other node; channel: 0
    uint32_t id;          // packet id
    uint8_t  chan;        // 0..7 or DM_CHAN
    uint8_t  outgoing;
    uint8_t  state;       // outgoing DM: Dm::State; channel: 1 sent, 3 failed
    int8_t   snr4;
    int16_t  rssi;
    uint8_t  unread;      // incoming, not yet seen (v1.2.0)
    char     text[200];
};

struct Node {
    uint32_t num;
    uint32_t lastMs;
    int16_t  rssi;
    int8_t   snr4;
    uint8_t  hops;        // 0xFF unknown
    bool     hasKey;
    // From NodeInfo / Position / device Telemetry when heard (v1.2.0)
    bool     hasUser;
    uint8_t  role;        // Meshtastic Config.DeviceConfig.Role
    uint16_t hwModel;
    bool     hasPos;
    int32_t  latI, lonI;  // 1e-7 degrees
    uint8_t  battery;     // 0 unknown, 1-100 %, 101 powered
};

void     begin(DualMeshRadioWrapper* radio);
void     poll();                          // main loop: drain RX, run DM jobs, save
uint32_t myNum();
const char* longName();                   // effective
void     shortName(char out[5]);          // effective

// Chats
bool     sendChannel(uint8_t chan, const char* text);
uint32_t sendDm(uint32_t to, const char* text);
uint16_t msgCount();
bool     msgAt(uint16_t i, Msg& out);     // 0 = oldest
uint16_t unread(uint8_t chan);            // DM_CHAN for all DMs
uint16_t unreadTotal();
void     markRead(uint8_t chan);          // DM_CHAN: every DM
void     markReadDm(uint32_t peer);       // one DM conversation
uint16_t unreadDm(uint32_t peer);
uint16_t chanMsgCount(uint8_t chan);      // stored messages on a channel
void     deleteDm(uint32_t peer);         // drop a DM conversation
bool     deleteNode(uint32_t num);        // drop a node from the heard list (it comes back when heard)
uint32_t changeSeq();                     // bumps when anything changes

// Nodes
uint8_t  nodeCount();
bool     nodeAt(uint8_t i, Node& out);    // newest first
void     nodeName(uint32_t num, char* out, size_t cap);
bool     nodeNames(uint32_t num, char* shortOut, size_t sCap, char* longOut, size_t lCap);   // false if none learned
bool     nodeByNum(uint32_t num, Node& out);
const char* roleName(uint8_t role);
const char* hwName(uint16_t model, char* buf, size_t cap);

// RX diagnostics (v0.1.1): what became of each Meshtastic frame the radio handed up.
struct RxDiag {
    uint32_t frames;       // frames popped from the radio
    uint32_t badHeader;    // too short / id 0
    uint32_t decoded;      // a key opened it (channel, fallback or PKI)
    uint32_t fallback;     // opened by a key whose hash did not match
    uint32_t noKey;        // no enabled channel has this hash, and no key opened it
    uint32_t badDecrypt;   // a channel hash matched but the Data did not parse
    uint32_t pkiOther;     // PKI to another node / unknown sender key
    uint32_t dupes;        // relayed copies
    uint32_t self;         // our own packets heard back (a neighbour relayed them), dropped
    uint32_t ackTx;        // ROUTING_APP ACKs/NAKs we sent for want_ack packets to us
    uint32_t ackRx;        // ACKs received for our own DMs (and implicit ACKs: our channel msgs relayed)
    uint8_t  lastHash;     // channel hash of the last frame
    uint8_t  lastPort;
    uint32_t lastFrom;
    uint32_t lastMs;
};
RxDiag   rxDiag();

// Identity changed: the next text announces it again.
void     identityChanged();
// v2.0: the Meshtastic PKI key (DM encryption). privateKey is for SD backup only.
const uint8_t* publicKey();
const uint8_t* privateKey();
bool     setPrivateKey(const uint8_t* priv32);   // nullptr = make a new random key; saves + re-announces
}

// Implemented in UITask.cpp: WadaMesh's node name, used when no long name is set.
const char* wadaNodeName();
}
