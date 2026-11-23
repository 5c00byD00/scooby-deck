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
    char     text[200];
};

struct Node {
    uint32_t num;
    uint32_t lastMs;
    int16_t  rssi;
    int8_t   snr4;
    uint8_t  hops;        // 0xFF unknown
    bool     hasKey;
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
void     markRead(uint8_t chan);
uint32_t changeSeq();                     // bumps when anything changes

// Nodes
uint8_t  nodeCount();
bool     nodeAt(uint8_t i, Node& out);    // newest first
void     nodeName(uint32_t num, char* out, size_t cap);

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
    uint8_t  lastHash;     // channel hash of the last frame
    uint8_t  lastPort;
    uint32_t lastFrom;
    uint32_t lastMs;
};
RxDiag   rxDiag();

// Identity changed: the next text announces it again.
void     identityChanged();
}

// Implemented in UITask.cpp: WadaMesh's node name, used when no long name is set.
const char* wadaNodeName();
}
