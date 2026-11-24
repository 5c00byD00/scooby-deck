#if defined(SCOOBY_DECK)
// DualMesh — Meshtastic client stack. See DualMeshMt.h.
// Frame building, crypto and DMs come from SquachWatch-CYD (GPL-3.0), whose
// codec is a reimplementation of Meshtastic's (GPL-3.0) wire format.
#include <Arduino.h>
#include "DualMeshMt.h"
#include "DualMeshConfig.h"
#include "DualMeshRadioWrapper.h"
#include "lora_meshtastic.h"
#include "lora_dm.h"
#include "lora_names.h"
#include "lora_myname.h"
#include <Preferences.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

using namespace Meshtastic;

namespace DualMesh {
namespace Mt {

static DualMeshRadioWrapper* s_radio = nullptr;
static uint32_t s_me = 0;
static uint32_t s_seq = 0;
static SemaphoreHandle_t s_lock = nullptr;

// Messages: a ring in PSRAM
static Msg*     s_msgs = nullptr;
static uint16_t s_head = 0, s_count = 0;
static uint16_t s_unread[MT_CHANNELS + 1];   // [8] = DMs

// Nodes
static const uint8_t MAX_NODES = 64;
static Node    s_nodes[MAX_NODES];
static uint8_t s_nNodes = 0;

// Duplicate suppression (from, id)
static const uint8_t SEEN = 64;
static uint32_t s_seenFrom[SEEN], s_seenId[SEEN];
static uint8_t  s_seenAt = 0;

// NodeInfo rhythm
static bool     s_announced = false;
static uint32_t s_announceAt = 0;
static const uint32_t ANNOUNCE_EVERY_MS = 3u * 3600u * 1000u;
static uint32_t s_saveAt = 0;

static void rnd(uint8_t* out, size_t n) { esp_fill_random(out, n); }

static inline void lock()   { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
static inline void unlock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }

uint32_t myNum() { return s_me; }

const char* longName() {
    const Config& c = cfg();
    if (c.long_name[0]) return c.long_name;
    const char* w = wadaNodeName();
    return Lora::MyName::longName(nullptr, w && w[0] ? w : nullptr);
}
void shortName(char out[5]) {
    Lora::MyName::shortName(cfg().short_name, longName(), out, 5);
}
void identityChanged() { s_announced = false; }

static void storeMsg(uint32_t from, uint32_t peer, uint32_t id, uint8_t chan, bool out, uint8_t state,
                     const char* text, int16_t rssi, int8_t snr4) {
    if (!s_msgs) return;
    lock();
    const uint16_t slot = (uint16_t)((s_head + s_count) % MAX_MSGS);
    Msg& m = s_msgs[s_count < MAX_MSGS ? slot : s_head];
    if (s_count >= MAX_MSGS) s_head = (uint16_t)((s_head + 1) % MAX_MSGS); else s_count++;
    memset(&m, 0, sizeof m);
    m.ms = millis(); m.from = from; m.peer = peer; m.id = id; m.chan = chan; m.outgoing = out ? 1 : 0;
    m.state = state; m.rssi = rssi; m.snr4 = snr4;
    strncpy(m.text, text ? text : "", sizeof m.text - 1);
    if (!out) s_unread[chan == DM_CHAN ? MT_CHANNELS : (chan < MT_CHANNELS ? chan : 0)]++;
    s_seq++;
    unlock();
}

static void noteNode(const Lora::Packet& pk, const Header& h) {
    if (!h.from || h.from == s_me) return;
    lock();
    int at = -1;
    for (uint8_t i = 0; i < s_nNodes; i++) if (s_nodes[i].num == h.from) { at = i; break; }
    if (at < 0) {
        if (s_nNodes < MAX_NODES) at = s_nNodes++;
        else {   // replace the oldest
            at = 0;
            for (uint8_t i = 1; i < s_nNodes; i++) if (s_nodes[i].lastMs < s_nodes[at].lastMs) at = i;
        }
        memset(&s_nodes[at], 0, sizeof s_nodes[at]);
        s_nodes[at].num = h.from;
    }
    Node& n = s_nodes[at];
    n.lastMs = millis(); n.rssi = pk.rssi; n.snr4 = pk.snr4; n.hops = hopsAway(h);
    n.hasKey = Lora::Dm::hasKey(h.from);
    s_seq++;
    unlock();
}

static bool seen(uint32_t from, uint32_t id) {
    for (uint8_t i = 0; i < SEEN; i++) if (s_seenFrom[i] == from && s_seenId[i] == id) return true;
    s_seenFrom[s_seenAt] = from; s_seenId[s_seenAt] = id;
    s_seenAt = (uint8_t)((s_seenAt + 1) % SEEN);
    return false;
}

// Our User protobuf: id, long name, short name, hw_model PRIVATE_HW, public key.
static size_t ourUser(uint8_t* user) {
    char idStr[12], sn[5];
    snprintf(idStr, sizeof idStr, "!%08lx", (unsigned long)s_me);
    const char* name = longName();
    shortName(sn);
    size_t ln = strlen(name); if (ln > 39) ln = 39;
    const size_t snl = strlen(sn);
    size_t u = 0;
    user[u++] = 0x0A; user[u++] = (uint8_t)strlen(idStr); memcpy(user + u, idStr, strlen(idStr)); u += strlen(idStr);
    user[u++] = 0x12; user[u++] = (uint8_t)ln;  memcpy(user + u, name, ln); u += ln;
    user[u++] = 0x1A; user[u++] = (uint8_t)snl; memcpy(user + u, sn, snl); u += snl;
    user[u++] = 0x28; user[u++] = 0xFF; user[u++] = 0x01;   // hw_model 255 PRIVATE_HW
    user[u++] = 0x38; user[u++] = 0x01;                     // role 1 = CLIENT_MUTE
    if (Lora::Dm::ready()) { user[u++] = 0x42; user[u++] = 32; memcpy(user + u, Lora::Dm::publicKey(), 32); u += 32; }
    return u;
}

static Channel primary() { return channel(0); }

static void announceIfDue() {
    if (!cfg().mt_announce) return;
    const uint32_t now = millis();
    if (s_announced && now - s_announceAt < ANNOUNCE_EVERY_MS) return;
    const Channel ch = primary();
    if (!canSend(ch)) return;
    uint8_t user[128], f[256];
    const size_t u = ourUser(user);
    const uint16_t n = buildFrame(ch, s_me, esp_random() | 1u, cfg().mt_hops, (uint8_t)PORT_NODEINFO, user, u, f);
    if (n && s_radio && s_radio->queueMeshtastic(f, n)) { s_announced = true; s_announceAt = now; }
}

void begin(DualMeshRadioWrapper* radio) {
    s_radio = radio;
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_msgs) {
        s_msgs = (Msg*)heap_caps_calloc(MAX_MSGS, sizeof(Msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_msgs) s_msgs = (Msg*)calloc(32, sizeof(Msg));
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_me = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    Config& c = cfg();
    const bool fresh = Lora::Dm::begin(s_me, c.mt_priv_set ? c.mt_priv : nullptr, rnd);
    if (fresh) { memcpy(c.mt_priv, Lora::Dm::privateKey(), 32); c.mt_priv_set = 1; save(); }
    setPkiOpener(&Lora::Dm::open);
    // Learned names and keys
    Preferences p;
    if (p.begin("dualmesh", true)) {
        size_t n = p.getBytesLength("names");
        if (n && n < 8192) { uint8_t* b = (uint8_t*)malloc(n); if (b) { p.getBytes("names", b, n); Lora::Names::load(b, n); free(b); } }
        n = p.getBytesLength("keys");
        if (n && n < 8192) { uint8_t* b = (uint8_t*)malloc(n); if (b) { p.getBytes("keys", b, n); Lora::Dm::loadKeys(b, n); free(b); } }
        p.end();
    }
}

static void saveIfDirty() {
    const uint32_t now = millis();
    if (now - s_saveAt < 60000) return;
    s_saveAt = now;
    if (!Lora::Names::dirty() && !Lora::Dm::keysDirty()) return;
    Preferences p;
    if (!p.begin("dualmesh", false)) return;
    if (Lora::Names::dirty()) {
        const size_t n = Lora::Names::bytesNeeded();
        uint8_t* b = (uint8_t*)malloc(n ? n : 1);
        if (b) { const size_t w = Lora::Names::save(b, n); p.putBytes("names", b, w); free(b); Lora::Names::markSaved(); }
    }
    if (Lora::Dm::keysDirty()) {
        const size_t n = Lora::Dm::keysBytesNeeded();
        uint8_t* b = (uint8_t*)malloc(n ? n : 1);
        if (b) { const size_t w = Lora::Dm::saveKeys(b, n); p.putBytes("keys", b, w); free(b); Lora::Dm::keysSaved(); }
    }
    p.end();
}

static RxDiag s_diag;
RxDiag rxDiag() { return s_diag; }

static void handle(const Lora::Packet& pk) {
    static Decoded d;   // ~1 KB: keep it off the loop task's stack
    s_diag.frames++;
    if (!decode(pk, d)) { s_diag.badHeader++; Serial.printf("[mt] bad header len=%u\n", (unsigned)pk.len); return; }
    const Header& h = d.hdr;
    if (h.from == s_me) {
        // Our own frame, relayed back by a neighbour: proof the neighbour heard
        // us and that we hear it, but not traffic. Counted, never shown/stored.
        s_diag.self++;
        // ReliableRouter::shouldFilterReceived: hearing our own packet relayed is
        // its implicit ACK. Mark our channel message as relayed (UI: [RELAYED]).
        lock();
        for (uint16_t i = 0; i < s_count; i++) {
            Msg& m = s_msgs[(s_head + i) % MAX_MSGS];
            if (m.outgoing && m.chan != DM_CHAN && m.id == h.id && m.state != 2) { m.state = 2; s_seq++; s_diag.ackRx++; }
        }
        unlock();
        Serial.printf("[mt] SELF echo id=%08lx hash=0x%02x rssi=%d (relayed by a neighbour)\n",
                      (unsigned long)h.id, h.channelHash, (int)pk.rssi);
        return;
    }
    s_diag.lastHash = h.channelHash; s_diag.lastFrom = h.from; s_diag.lastMs = millis();
    if (d.haveData) {
        s_diag.decoded++;
        if (d.viaFallback) s_diag.fallback++;
        s_diag.lastPort = (uint8_t)d.data.portnum;
    } else if (h.channelHash == 0 && h.to != BROADCAST) {
        s_diag.pkiOther++;
    } else {
        bool match = false;
        for (uint8_t i = 0; i < channelCount(); i++) { const Channel c = channel(i); if (c.enabled && c.hash == h.channelHash) match = true; }
        if (match) s_diag.badDecrypt++; else s_diag.noKey++;
    }
    Serial.printf("[mt] from=!%08lx to=!%08lx id=%08lx hash=0x%02x %s port=%u ch=%u%s\n",
                  (unsigned long)h.from, (unsigned long)h.to, (unsigned long)h.id, h.channelHash,
                  d.haveData ? "OK" : "NO-KEY", d.haveData ? (unsigned)d.data.portnum : 0u,
                  (unsigned)d.channelIdx, d.viaFallback ? " (fallback key)" : "");
    if (h.from == s_me) return;              // our own, through a relay
    noteNode(pk, h);
    if (seen(h.from, h.id)) { s_diag.dupes++; return; }   // a relayed copy
    const uint32_t now = millis();
    lock();
    Lora::Dm::onFrame(d, now, cfg().mt_hops);
    unlock();
    if (!d.haveData) return;
    switch (d.data.portnum) {
        case PORT_NODEINFO:
            Lora::Names::learn(h.from, d.user.shortName, d.user.longName);
            s_seq++;
            break;
        case PORT_TEXT:
            if (d.pki) storeMsg(h.from, h.from, h.id, DM_CHAN, false, 0, d.text, pk.rssi, pk.snr4);
            else if (h.to == BROADCAST || h.to == s_me)
                storeMsg(h.from, 0, h.id, d.channelIdx, false, 0, d.text, pk.rssi, pk.snr4);
            break;
        default: break;
    }
    // Client-mute: nothing is ever rebroadcast.
}

static void dmJobs() {
    if (!Lora::Dm::ready() || !s_radio) return;
    const uint32_t now = millis();
    for (int k = 0; k < 2; k++) {
        Lora::Dm::Out o;
        lock();
        const bool got = Lora::Dm::next(now, cfg().mt_hops, o);
        unlock();
        if (!got) return;
        uint8_t f[256], data[220];
        uint16_t n = 0;
        const Channel ch = primary();
        switch (o.job) {
            case Lora::Dm::J_DM: announceIfDue(); memcpy(f, o.frame, o.len); n = o.len; break;
            case Lora::Dm::J_ACK: case Lora::Dm::J_NAK: {
                const uint8_t routing[2] = { 0x18, (uint8_t)(o.job == Lora::Dm::J_ACK ? 0 : o.err) };
                const size_t dn = encodeData((uint8_t)PORT_ROUTING, routing, 2, false, o.id, data);
                n = buildChannelFrame(ch, s_me, o.to, esp_random() | 1u, o.hops, false, data, dn, f);
                break;
            }
            case Lora::Dm::J_NODEINFO: {
                uint8_t user[128];
                const size_t u = ourUser(user);
                const size_t dn = encodeData((uint8_t)PORT_NODEINFO, user, u, o.wantResponse, 0, data);
                n = buildChannelFrame(ch, s_me, o.to, esp_random() | 1u, o.hops, false, data, dn, f);
                break;
            }
            default: break;
        }
        const bool ok = n && s_radio->queueMeshtastic(f, n);
        if (ok && (o.job == Lora::Dm::J_ACK || o.job == Lora::Dm::J_NAK)) {
            s_diag.ackTx++;
            Serial.printf("[mt] %s -> !%08lx req=%08lx hops=%u\n", o.job == Lora::Dm::J_ACK ? "ACK" : "NAK",
                          (unsigned long)o.to, (unsigned long)o.id, (unsigned)o.hops);
        }
        lock();
        Lora::Dm::sent(o, ok, millis());
        unlock();
    }
}

static void dmState(uint32_t id, uint32_t /*to*/, Lora::Dm::State st, uint8_t /*err*/) {
    if (!s_msgs) return;
    lock();
    for (uint16_t i = 0; i < s_count; i++) {
        Msg& m = s_msgs[(s_head + i) % MAX_MSGS];
        if (m.outgoing && m.chan == DM_CHAN && m.id == id) {
            if (st == Lora::Dm::ST_DELIVERED && m.state != Lora::Dm::ST_DELIVERED) s_diag.ackRx++;
            m.state = (uint8_t)st; s_seq++;
        }
    }
    unlock();
}

void poll() {
    if (!s_radio) return;
    static bool hooked = false;
    if (!hooked) { Lora::Dm::onState(dmState); hooked = true; }
    Lora::Packet pk;
    for (int k = 0; k < 4 && s_radio->popMeshtastic(pk); k++) handle(pk);
    dmJobs();
    // First NodeInfo ~30 s after boot, then every 3 h (not in MeshCore-only mode).
    if (cfg().mode != MODE_MESHCORE && millis() > 30000) announceIfDue();
    saveIfDirty();
}

bool sendChannel(uint8_t chan, const char* text) {
    if (!text || !*text || chan >= MT_CHANNELS || !s_radio) return false;
    const Channel ch = channel(chan);
    if (!canSend(ch)) return false;
    announceIfDue();
    uint8_t f[256];
    const uint32_t id = esp_random() | 1u;
    const uint16_t n = buildFrame(ch, s_me, id, cfg().mt_hops, (uint8_t)PORT_TEXT, (const uint8_t*)text, strlen(text), f);
    const bool ok = n && s_radio->queueMeshtastic(f, n);
    storeMsg(s_me, 0, id, chan, true, ok ? 1 : 3, text, 0, 0);
    return ok;
}

uint32_t sendDm(uint32_t to, const char* text) {
    if (!text || !*text || !Lora::Dm::ready()) return 0;
    lock();
    const uint32_t id = Lora::Dm::send(to, text, esp_random() | 1u, millis());
    const uint8_t st = id ? (uint8_t)Lora::Dm::state(id) : (uint8_t)Lora::Dm::ST_FAILED;
    unlock();
    storeMsg(s_me, to, id, DM_CHAN, true, st, text, 0, 0);
    return id;
}

uint16_t msgCount() { return s_count; }
bool msgAt(uint16_t i, Msg& out) {
    if (!s_msgs || i >= s_count) return false;
    lock(); out = s_msgs[(s_head + i) % MAX_MSGS]; unlock();
    return true;
}
uint16_t unread(uint8_t chan) { return s_unread[chan == DM_CHAN ? MT_CHANNELS : (chan < MT_CHANNELS ? chan : 0)]; }
uint16_t unreadTotal() { uint16_t t = 0; for (uint16_t u : s_unread) t += u; return t; }
void markRead(uint8_t chan) { s_unread[chan == DM_CHAN ? MT_CHANNELS : (chan < MT_CHANNELS ? chan : 0)] = 0; s_seq++; }
uint32_t changeSeq() { return s_seq; }

uint8_t nodeCount() { return s_nNodes; }
bool nodeAt(uint8_t i, Node& out) {
    if (i >= s_nNodes) return false;
    lock();
    // newest first: a selection by rank (small table)
    bool used[MAX_NODES] = {false};
    int pick = -1;
    for (uint8_t r = 0; r <= i; r++) {
        pick = -1;
        for (uint8_t k = 0; k < s_nNodes; k++)
            if (!used[k] && (pick < 0 || s_nodes[k].lastMs > s_nodes[pick].lastMs)) pick = k;
        if (pick >= 0) used[pick] = true;
    }
    if (pick >= 0) out = s_nodes[pick];
    unlock();
    return pick >= 0;
}

void nodeName(uint32_t num, char* out, size_t cap) {
    if (!out || !cap) return;
    char tag[8], name[40];
    if (num == s_me) { snprintf(out, cap, "%s", longName()); return; }
    if (Lora::Names::lookup(num, tag, sizeof tag, name, sizeof name) && (name[0] || tag[0]))
        snprintf(out, cap, "%s", name[0] ? name : tag);
    else snprintf(out, cap, "!%08lx", (unsigned long)num);
}

}
}
#endif  // SCOOBY_DECK
