#if defined(SCOOBY_DECK)
// DualMesh — the time-division radio scheduler. See DualMeshRadioWrapper.h.
//
// Timing model (ported from SquachWatch-CYD src/lora_sniffer.cpp, GPL-3.0):
//  - home: plain continuous RX on the MeshCore profile; a look never starts
//    while a frame is arriving (PREAMBLE/HEADER_VALID extend the stay).
//  - peek: one CAD on the Meshtastic profile (2 symbols at SF>=10, else 4,
//    detection peak SF+16 at SF>=10) that exits straight into RX (CAD_GOTO_RX,
//    as SquachWatch does): no standby + SPI + startReceive gap eating the
//    preamble. On a hit, RX there until the preamble, header and frame land or
//    their deadlines pass; then home again.
//  - IRQs are serviced on the DIO1 LEVEL, not only its rising edge: v0.1.0
//    waited for an edge only, so a flag that stayed latched (two events inside
//    one SPI read/clear) left DIO1 high with no new edge and the radio deaf
//    until something else re-armed it -- forever in a single-profile mode.
//  - homeMs(): how long to stay home between looks so a look lands inside
//    the peek profile's preamble with room for CAD + lock.
//  - TX: MeshCore's Dispatcher already does LBT through isReceiving(); its
//    frame goes out at the next turn of the loop. Meshtastic frames get a CAD
//    on their own profile and a random backoff (up to 4 tries) first.
#include <Arduino.h>
#include "DualMeshRadioWrapper.h"
#include "DualMeshConfig.h"
#include "DualMeshMt.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <math.h>

using namespace DualMesh;

namespace {
const uint16_t IRQ_TX_DONE   = 0x0001, IRQ_RX_DONE = 0x0002, IRQ_PREAMBLE = 0x0004,
               IRQ_HDR_VALID = 0x0010, IRQ_HDR_ERR = 0x0020, IRQ_CRC_ERR = 0x0040,
               IRQ_CAD_DONE  = 0x0080, IRQ_CAD_DET = 0x0100, IRQ_TIMEOUT = 0x0200;

const RadioLibIrqFlags_t RX_IRQS =
    (1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT) | (1UL << RADIOLIB_IRQ_CRC_ERR) |
    (1UL << RADIOLIB_IRQ_HEADER_VALID) | (1UL << RADIOLIB_IRQ_HEADER_ERR) | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);
const RadioLibIrqFlags_t CAD_IRQS = (1UL << RADIOLIB_IRQ_CAD_DONE) | (1UL << RADIOLIB_IRQ_CAD_DETECTED);
// A look's CAD exits into RX, so its RX events must reach DIO1 too.
const RadioLibIrqFlags_t LOOK_IRQS = RX_IRQS | CAD_IRQS;

inline bool dio1High() { return digitalRead(P_LORA_DIO_1) == HIGH; }

SemaphoreHandle_t s_irq = nullptr;
SemaphoreHandle_t s_q   = nullptr;     // guards the rings below
TaskHandle_t      s_task = nullptr;
DualMeshRadioWrapper* s_self = nullptr;

void IRAM_ATTR dmIsr() {
    BaseType_t hpw = pdFALSE;
    if (s_irq) xSemaphoreGiveFromISR(s_irq, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}

// MeshCore RX ring (DRAM, small)
struct McSlot { uint8_t buf[256]; uint16_t len; float snr, rssi; };
const uint8_t MC_SLOTS = 6;
McSlot  s_mcRx[MC_SLOTS];
uint8_t s_mcW = 0, s_mcR = 0;

// Meshtastic RX ring (PSRAM when there is any)
const uint8_t MT_SLOTS = 8;
Lora::Packet* s_mtRx = nullptr;
uint8_t s_mtW = 0, s_mtR = 0;

// MeshCore TX: one frame at a time, as the Dispatcher sends
enum : uint8_t { TX_NONE = 0, TX_PENDING, TX_SENDING, TX_OK, TX_FAIL };
struct TxSlot { uint8_t buf[256]; uint16_t len; volatile uint8_t state; };
TxSlot s_mcTx;
// Meshtastic TX queue
const uint8_t MTQ = 6;
TxSlot  s_mtTx[MTQ];
uint8_t s_mtTxW = 0, s_mtTxR = 0;

inline uint16_t bw10(float bw) { return (uint16_t)lroundf(bw * 10.0f); }
uint32_t symUs(const RadioProfile& p) { return Lora::symbolUs(p.sf, bw10(p.bw)); }
uint8_t  cadSymbols(const RadioProfile& p) {
    const uint8_t c = DualMesh::cfg().tdm_cad_syms;
    if (c == 2 || c == 4 || c == 8) return c;
    return p.sf >= 10 ? 2 : 4;
}
uint8_t  cadPeak(const RadioProfile& p) { return p.sf >= 10 ? (uint8_t)(p.sf + 16) : 0; }
uint32_t cadMs(const RadioProfile& p) { return (symUs(p) * (cadSymbols(p) + 1)) / 1000 + 4; }
uint32_t preambleMs(const RadioProfile& p) { return (symUs(p) * ((uint32_t)p.preamble + 4)) / 1000; }
uint32_t preambleDeadlineMs(const RadioProfile& p) { return (symUs(p) * 8u) / 1000 + 8; }
// After a look's CAD hit: SquachWatch bench-measured PREAMBLE 21-26 ms after a
// real hit at MediumSlow/LongFast and drops a silent hit after 4 symbols + 6 ms;
// 6 symbols + 6 ms here leaves a little more room.
uint32_t quickPreambleMs(const RadioProfile& p) { return (symUs(p) * 6u) / 1000 + 6; }
uint32_t headerDeadlineMs(const RadioProfile& p) { return (symUs(p) * (uint32_t)(p.preamble + 13)) / 1000 + 20; }
uint32_t maxFrameMs(const RadioProfile& p) {
    return Lora::timeOnAirUs(p.sf, bw10(p.bw), 8, p.preamble, 255, true, false, false) / 1000 + 50;
}
uint32_t airMs(const RadioProfile& p, uint16_t len) {
    return Lora::timeOnAirUs(p.sf, bw10(p.bw), p.cr, p.preamble, (uint8_t)(len > 255 ? 255 : len), true, false, false) / 1000;
}
inline CustomSX1262* chip(PhysicalLayer* r) { return (CustomSX1262*)r; }

SemaphoreHandle_t s_chip = nullptr;   // recursive: chip access outside the radio task

void taskEntry(void* arg) { static_cast<DualMeshRadioWrapper*>(arg)->taskBody(); }
}

void DualMeshRadioWrapper::radioAcquire() {
    if (!s_chip) s_chip = xSemaphoreCreateRecursiveMutex();
    if (s_chip) xSemaphoreTakeRecursive(s_chip, portMAX_DELAY);
}
void DualMeshRadioWrapper::radioRelease() {
    if (s_chip) xSemaphoreGiveRecursive(s_chip);
}

// ---------------------------------------------------------------------------
// MeshCore-facing API
// ---------------------------------------------------------------------------
void DualMeshRadioWrapper::begin() {
    CustomSX1262Wrapper::begin();               // counters, noise-floor state
    s_self = this;
    if (!s_irq) s_irq = xSemaphoreCreateBinary();
    if (!s_q)   s_q   = xSemaphoreCreateMutex();
    if (!s_mtRx) {
        s_mtRx = (Lora::Packet*)heap_caps_calloc(MT_SLOTS, sizeof(Lora::Packet), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_mtRx) s_mtRx = (Lora::Packet*)calloc(MT_SLOTS, sizeof(Lora::Packet));
    }
    DualMesh::load();
    _mode = DualMesh::cfg().mode;
    chip(_radio)->standby();
    chip(_radio)->setPacketReceivedAction(dmIsr);   // ours, not the base's setFlag
    _rxOn = false;
    DualMesh::Mt::begin(this);
    if (!s_task) xTaskCreatePinnedToCore(taskEntry, "dualmesh", 6144, this, 10, &s_task, 0);
}

void DualMeshRadioWrapper::setParams(float freq, float bw, uint8_t sf, uint8_t cr) {
    _mcFreq = freq; _mcBw = bw; _mcSf = sf; _mcCr = cr;
    _cfgDirty = true;
    if (_paused) CustomSX1262Wrapper::setParams(freq, bw, sf, cr);   // the spectrum sweep owns the chip
}

int DualMeshRadioWrapper::recvRaw(uint8_t* bytes, int sz) {
    if (_paused) return CustomSX1262Wrapper::recvRaw(bytes, sz);
    int len = 0;
    if (xSemaphoreTake(s_q, pdMS_TO_TICKS(5)) != pdTRUE) return 0;
    if (s_mcR != s_mcW) {
        McSlot& s = s_mcRx[s_mcR];
        len = s.len > sz ? sz : s.len;
        memcpy(bytes, s.buf, len);
        _lastSnr = s.snr; _lastRssi = s.rssi;
        s_mcR = (uint8_t)((s_mcR + 1) % MC_SLOTS);
        n_recv++;
    }
    xSemaphoreGive(s_q);
    return len;
}

uint32_t DualMeshRadioWrapper::getEstAirtimeFor(int len_bytes) { return airMs(_mc, (uint16_t)len_bytes); }

bool DualMeshRadioWrapper::startSendRaw(const uint8_t* bytes, int len) {
    if (_mode == MODE_MESHTASTIC || len <= 0 || len > 255) return false;   // MeshCore TX is off in Meshtastic-only
    if (s_mcTx.state == TX_PENDING || s_mcTx.state == TX_SENDING) return false;
    memcpy(s_mcTx.buf, bytes, len);
    s_mcTx.len = (uint16_t)len;
    s_mcTx.state = TX_PENDING;
    if (s_irq) xSemaphoreGive(s_irq);    // wake the task out of its listen
    return true;
}

bool DualMeshRadioWrapper::isSendComplete() {
    const uint8_t st = s_mcTx.state;
    if (st == TX_OK || st == TX_FAIL) {
        if (st == TX_OK) n_sent++;
        s_mcTx.state = TX_NONE;
        return true;
    }
    return false;
}

void DualMeshRadioWrapper::onSendFinished() {
    // The Dispatcher gave up (expiry) or is done: drop a frame not yet on the air.
    if (s_mcTx.state == TX_PENDING) s_mcTx.state = TX_NONE;
    else if (s_mcTx.state == TX_OK || s_mcTx.state == TX_FAIL) s_mcTx.state = TX_NONE;
}

bool DualMeshRadioWrapper::isInRecvMode() const {
    return s_mcTx.state == TX_NONE || s_mcTx.state == TX_OK || s_mcTx.state == TX_FAIL;
}

bool DualMeshRadioWrapper::isReceiving() {
    // LBT for the Dispatcher: busy while ANY frame is arriving (either mesh),
    // or while a Meshtastic frame is on the air.
    return (int32_t)(_busyUntil - millis()) > 0 || _on == ON_TX;
}

float DualMeshRadioWrapper::getCurrentRSSI() {
    if (_paused) return CustomSX1262Wrapper::getCurrentRSSI();
    return _curRssi;
}
float DualMeshRadioWrapper::getLastRSSI() const { return _lastRssi; }
float DualMeshRadioWrapper::getLastSNR() const { return _lastSnr; }
float DualMeshRadioWrapper::packetScore(float snr, int packet_len) { return packetScoreInt(snr, _mc.sf, packet_len); }
uint8_t DualMeshRadioWrapper::getSpreadingFactor() const { return _mc.sf; }

void DualMeshRadioWrapper::powerOff() {
    _off = true;
    for (int k = 0; k < 100 && !_parked; k++) delay(5);
    radioAcquire();
    chip(_radio)->sleep(false);
    radioRelease();
}

void DualMeshRadioWrapper::loop() {
    // Noise floor as MeshCore's Dispatcher wants it, from the task's samples.
    _noise_floor = _st.noise;
    if (!_paused) DualMesh::Mt::poll();
}

void DualMeshRadioWrapper::resetAGC() { _agc = true; }
bool DualMeshRadioWrapper::setRxBoostedGainMode(bool en) { _boosted = en; _boostDirty = true; return true; }
void DualMeshRadioWrapper::setTxPower(int8_t dbm) { _txDbm = dbm; _txDbmDirty = true; }
uint32_t DualMeshRadioWrapper::getRngSeed() { return s_task ? esp_random() : CustomSX1262Wrapper::getRngSeed(); }

void DualMeshRadioWrapper::pauseForSpectrum(bool on) {
    if (on) {
        _paused = true;
        for (int k = 0; k < 200 && !_parked; k++) delay(5);
        _rxOn = false;
        _applied = RadioProfile{0, 0, 0, 0, 0, 0};
        chip(_radio)->setPacketReceivedAction(dmIsr);
    } else {
        chip(_radio)->setPacketReceivedAction(dmIsr);
        _applied = RadioProfile{0, 0, 0, 0, 0, 0};
        _rxOn = false;
        _cfgDirty = true;
        _paused = false;
    }
}

bool DualMeshRadioWrapper::queueMeshtastic(const uint8_t* f, uint16_t n) {
    if (_mode == MODE_MESHCORE || !n || n > 255) return false;
    if (xSemaphoreTake(s_q, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const uint8_t nw = (uint8_t)((s_mtTxW + 1) % MTQ);
    bool ok = false;
    if (nw != s_mtTxR) {
        memcpy(s_mtTx[s_mtTxW].buf, f, n);
        s_mtTx[s_mtTxW].len = n;
        s_mtTx[s_mtTxW].state = TX_PENDING;
        s_mtTxW = nw;
        ok = true;
    }
    xSemaphoreGive(s_q);
    return ok;
}

bool DualMeshRadioWrapper::popMeshtastic(Lora::Packet& pk) {
    if (!s_mtRx) return false;
    if (xSemaphoreTake(s_q, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    bool got = false;
    if (s_mtR != s_mtW) { pk = s_mtRx[s_mtR]; s_mtR = (uint8_t)((s_mtR + 1) % MT_SLOTS); got = true; }
    xSemaphoreGive(s_q);
    return got;
}

// ---- Dual mode, v0.1.2: RX windows ------------------------------------------
// v0.1.0/v0.1.1 looked at Meshtastic with a 2-symbol CAD (peak SF+16). On the
// T-Deck Plus that CAD fired on noise on more than half the looks and almost
// never on the real preamble (bench, 2026-11-23: CAD 388/687, PRE 2 in minutes,
// a Heltec 1-2 m away missed). Now Dual alternates two plain RX windows and lets
// the SX1262's own preamble detector decide, the way Meshtastic-only mode
// listens. A window that hears a preamble stays until the header / frame.
//
// Guarantee: a preamble of P ms overlaps a window of W repeating every T by at
// least W + P - T. With MediumSlow (P = 16 x 4.1 = 65.5 ms) and MeshCore SF7/
// BW62.5 (P = 32 x 2.05 = 65.5 ms), MT 40 ms + MC 31 ms + ~2 x 3 ms switching
// gives T ~ 77: >= 28 ms (~7 symbols) of every Meshtastic preamble and >= 19 ms
// (~9 symbols) of every MeshCore preamble land inside a window.
static const uint32_t SWITCH_MS = 3;
static uint32_t detectMs(const RadioProfile& p, uint32_t syms) { return (symUs(p) * syms) / 1000; }
uint32_t DualMeshRadioWrapper::mtWindowMs() const {
    const uint32_t dMt = detectMs(_mt, 6), dMc = detectMs(_mc, 6);
    const uint32_t pMc = detectMs(_mc, _mc.preamble);
    uint32_t w = dMt + 15;                                  // MS: 24.6 + 15 = 40 ms
    const uint32_t mcCap = pMc > dMc + 2 * SWITCH_MS ? pMc - dMc - 2 * SWITCH_MS : 0;
    if (w > mcCap) w = mcCap > dMt + 6 ? mcCap : dMt + 6;  // keep MeshCore's preambles covered when possible
    return w < 20 ? 20 : w;
}
bool DualMeshRadioWrapper::cadLooks() const {
    const uint8_t c = DualMesh::cfg().tdm_cad_syms;
    return c == 2 || c == 4 || c == 8;                      // legacy CAD peeks, opt-in
}
uint32_t DualMeshRadioWrapper::homeMs() const {
    const Config& c = DualMesh::cfg();
    if (c.tdm_home_ms >= 10) return c.tdm_home_ms;
    if (!cadLooks()) {
        const uint32_t pMt = detectMs(_mt, _mt.preamble), dMt = detectMs(_mt, 6);
        const int32_t w = (int32_t)pMt - (int32_t)dMt - (int32_t)(2 * SWITCH_MS) - 4;   // MS: 31 ms
        return w < 20 ? 20u : (uint32_t)w;
    }
    const RadioProfile& pk = _mt;
    const uint32_t lock = (symUs(pk) * (cadSymbols(pk) + 8u)) / 1000;
    const int32_t ms = (int32_t)preambleMs(pk) - (int32_t)lock - 15 - (int32_t)cadMs(pk) - 6;
    return ms < 20 ? 20u : (uint32_t)ms;
}

// ---------------------------------------------------------------------------
// The task
// ---------------------------------------------------------------------------
void DualMeshRadioWrapper::refreshProfiles() {
    const Config& c = DualMesh::cfg();
    _mode = c.mode;
    RadioProfile mc { _mcFreq, _mcBw, _mcSf, _mcCr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 0 };
    if (c.shared_sf) mc.sf = c.shared_sf;
    mc.preamble = preambleLengthForSF(mc.sf);
    RadioProfile mt { DualMesh::mtFreqMHz(), DualMesh::mtBw(), DualMesh::mtSf(), DualMesh::mtCr(), c.mt_sync,
                      (uint16_t)(c.mt_preamble ? c.mt_preamble : 16) };
    _mc = mc; _mt = mt;
    _applied = RadioProfile{0, 0, 0, 0, 0, 0};   // force a re-apply
    _rxOn = false;
    _cfgDirty = false;
}

bool DualMeshRadioWrapper::applyProfile(const RadioProfile& p) {
    if (_applied == p) return true;
    CustomSX1262* r = chip(_radio);
    r->standby();
    _rxOn = false;
    // Image calibration is per band: hops inside 902-928 (or 863-870) need none.
    // Track the calibrated band separately: v0.1.0 compared with _applied, which
    // is zeroed on every config change / AGC reset, so each one re-ran it.
    const bool cal = _calFreq == 0 || fabsf(p.freq - _calFreq) > 30.0f;
    int16_t st = r->setFrequency(p.freq, !cal);
    if (st == RADIOLIB_ERR_NONE && cal) _calFreq = p.freq;
    if (st == RADIOLIB_ERR_NONE) st = r->setBandwidth(p.bw);
    if (st == RADIOLIB_ERR_NONE) st = r->setSpreadingFactor(p.sf);
    if (st == RADIOLIB_ERR_NONE) st = r->setCodingRate(p.cr);
    if (st == RADIOLIB_ERR_NONE) st = r->setSyncWord(p.sync);
    if (st == RADIOLIB_ERR_NONE) st = r->setPreambleLength(p.preamble);
    if (st == RADIOLIB_ERR_NONE) st = r->setCRC(2);
    if (st == RADIOLIB_ERR_NONE) st = r->invertIQ(false);
    if (st == RADIOLIB_ERR_NONE) st = r->explicitHeader();
    if (st == RADIOLIB_ERR_NONE) st = r->autoLDRO();
    if (st != RADIOLIB_ERR_NONE) { _applied = RadioProfile{0, 0, 0, 0, 0, 0}; return false; }
    _applied = p;
    return true;
}

bool DualMeshRadioWrapper::startRx() {
    xSemaphoreTake(s_irq, 0);   // drop a stale wake
    const int16_t st = static_cast<PhysicalLayer*>(chip(_radio))->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, RX_IRQS, RX_IRQS, 0);
    _rxOn = st == RADIOLIB_ERR_NONE;
    return _rxOn;
}

// True on a DIO1 edge (or a TX wake from startSendRaw), or when DIO1 is HIGH:
// a latched IRQ that produced no fresh edge is still serviced within ~4 ms.
bool DualMeshRadioWrapper::waitIrq(uint32_t ms) {
    const uint32_t t0 = millis();
    for (;;) {
        if (dio1High()) {
            if (xSemaphoreTake(s_irq, 0) != pdTRUE) _st.levelIrqs++;
            return true;
        }
        const uint32_t el = millis() - t0;
        if (el >= ms) return false;
        uint32_t slice = ms - el;
        if (slice > 4) slice = 4;
        if (xSemaphoreTake(s_irq, pdMS_TO_TICKS(slice) ? pdMS_TO_TICKS(slice) : 1) == pdTRUE) return true;
    }
}

bool DualMeshRadioWrapper::mcTxWanted() const { return s_mcTx.state == TX_PENDING; }

void DualMeshRadioWrapper::readFrame(uint8_t which) {
    CustomSX1262* r = chip(_radio);
    const uint16_t flags = (uint16_t)r->getIrqFlags();
    size_t len = r->getPacketLength(true);
    if (len > 255) len = 255;
    uint8_t buf[256];
    const int16_t st = r->readData(buf, len);
    const float rssi = r->getRSSI(), snr = r->getSNR();
    _lastIrqMs = millis();
    if (which == ON_MESHTASTIC) {
        _st.mtLastRssi = (int16_t)lroundf(rssi); _st.mtLastSnr4 = (int16_t)lroundf(snr * 4.0f);
        _st.mtLastLen = (uint8_t)len; _st.mtLastMs = millis();
    }
    if (st != RADIOLIB_ERR_NONE || (flags & IRQ_CRC_ERR)) {
        _st.crcErr++; n_recv_errors++;
        if (which == ON_MESHTASTIC) _st.mtCrcErr++; else _st.mcCrcErr++;
        Serial.printf("[dualmesh] %s rx drop: st=%d flags=0x%04x len=%u rssi=%.0f snr=%.1f\n",
                      which == ON_MESHTASTIC ? "MT" : "MC", st, flags, (unsigned)len, rssi, snr);
        return;
    }
    if (!len) return;
    xSemaphoreTake(s_q, portMAX_DELAY);
    if (which == ON_MESHCORE) {
        const uint8_t nw = (uint8_t)((s_mcW + 1) % MC_SLOTS);
        if (nw != s_mcR) {
            McSlot& s = s_mcRx[s_mcW];
            memcpy(s.buf, buf, len); s.len = (uint16_t)len; s.snr = snr; s.rssi = rssi;
            s_mcW = nw;
            _st.mcRx++;
        }
    } else if (s_mtRx) {
        const uint8_t nw = (uint8_t)((s_mtW + 1) % MT_SLOTS);
        if (nw != s_mtR) {
            Lora::Packet& pk = s_mtRx[s_mtW];
            memset(&pk, 0, sizeof pk);
            memcpy(pk.data, buf, len); pk.len = (uint8_t)len;
            pk.ms = millis();
            pk.rssi = (int16_t)lroundf(rssi); pk.snr4 = (int8_t)lroundf(snr * 4.0f);
            pk.freqHz = (uint32_t)lroundf(_mt.freq * 1e6f); pk.bwKhz10 = bw10(_mt.bw); pk.sf = _mt.sf; pk.sync = _mt.sync;
            pk.flags = Lora::PK_CRC_PRESENT | Lora::PK_CRC_OK | Lora::PK_LIVE;
            pk.proto = Lora::Proto::MESHTASTIC;
            s_mtW = nw;
            _st.mtRx++;
            Serial.printf("[dualmesh] MT rx len=%u rssi=%.0f snr=%.1f hash=0x%02x\n",
                          (unsigned)len, rssi, snr, len > 13 ? buf[13] : 0);
        }
    }
    xSemaphoreGive(s_q);
}

// Plain RX on one profile for `ms` (longer while a frame is arriving). Returns
// early when a MeshCore frame is waiting to go out and nothing is arriving.
void DualMeshRadioWrapper::listen(uint8_t which, uint32_t ms) {
    const RadioProfile& p = which == ON_MESHCORE ? _mc : _mt;
    radioAcquire();
    if (_on != which || !_rxOn) {
        if (!applyProfile(p) || !startRx()) { radioRelease(); vTaskDelay(pdMS_TO_TICKS(50)); return; }
        _on = which;
    }
    radioRelease();
    const uint32_t start = millis();
    uint32_t lastNoise = 0;
    _busyUntil = 0;
    for (;;) {
        if (_paused || _off || _cfgDirty) break;
        const uint32_t now = millis();
        const bool busy = (int32_t)(_busyUntil - now) > 0;
        if (!busy && (mcTxWanted() || (s_mtTxR != s_mtTxW && _mode != MODE_MESHCORE))) break;
        uint32_t until = start + ms;
        if ((int32_t)(_busyUntil - until) > 0) until = _busyUntil;
        if ((int32_t)(now - until) >= 0) break;
        if (!busy && now - lastNoise > 1000) {
            lastNoise = now;
            radioAcquire();
            const float v = chip(_radio)->getRSSI(false);
            radioRelease();
            _curRssi = v;
            if (which == ON_MESHCORE && v < -40 && v > -140) {
                _st.noise = _st.noise == 0 ? (int16_t)v : (int16_t)((_st.noise * 7 + (int16_t)v) / 8);
            }
        }
        if (!waitIrq(until - now)) {
            // A started frame whose events never came (header deadline passed):
            // re-arm so the receiver is not left mid-frame.
            if (busy && (int32_t)(millis() - _busyUntil) >= 0) {
                radioAcquire();
                chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
                startRx();
                radioRelease();
                _busyUntil = 0;
                _st.rearms++;
            }
            continue;
        }
        radioAcquire();
        const uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
        const uint32_t t = millis();
        if (f) _lastIrqMs = t;
        if (which == ON_MESHTASTIC) {
            if (f & IRQ_PREAMBLE)  _st.mtPre++;
            if (f & IRQ_HDR_VALID) _st.mtHdr++;
            if (f & IRQ_HDR_ERR)   _st.mtHdrErr++;
        }
        if (f & IRQ_PREAMBLE)  _busyUntil = t + headerDeadlineMs(p);
        if (f & IRQ_HDR_VALID) _busyUntil = t + maxFrameMs(p);
        if (f & (IRQ_RX_DONE | IRQ_CRC_ERR)) {
            readFrame(which);
            _busyUntil = 0;
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            startRx();
        } else if (f & (IRQ_HDR_ERR | IRQ_TIMEOUT)) {
            _busyUntil = 0;
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            startRx();
        } else if (f) {
            // Clear exactly what was read, so DIO1 drops and the next event
            // makes a fresh edge (and the level poll sees it either way).
            chip(_radio)->clearIrqFlags(f);
        } else if (dio1High()) {
            // DIO1 high with no flag we know: clear all and re-arm.
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            startRx();
            _st.rearms++;
        }
        radioRelease();
    }
    _busyUntil = 0;
}

// One look at the Meshtastic profile: a CAD that exits straight into RX on a
// detection (SquachWatch's startCad(..., gotoRx=true, maxFrameMs)), then the
// frame if one follows. Ported from homeAndPeek() + catchAfterHit(quick).
void DualMeshRadioWrapper::peek() {
    const RadioProfile& p = _mt;
    _st.peeks++;
    radioAcquire();
    if (!applyProfile(p)) { radioRelease(); return; }
    _on = ON_MESHTASTIC;
    _rxOn = false;
    ChannelScanConfig_t cfgScan;
    memset(&cfgScan, 0, sizeof cfgScan);
    const uint8_t syms = cadSymbols(p);
    const uint32_t frameMs = maxFrameMs(p);
    cfgScan.cad.symNum   = syms <= 2 ? RADIOLIB_SX126X_CAD_ON_2_SYMB : syms >= 8 ? RADIOLIB_SX126X_CAD_ON_8_SYMB : RADIOLIB_SX126X_CAD_ON_4_SYMB;
    cfgScan.cad.detPeak  = cadPeak(p) ? cadPeak(p) : RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfgScan.cad.detMin   = RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfgScan.cad.exitMode = RADIOLIB_SX126X_CAD_GOTO_RX;
    cfgScan.cad.timeout  = (frameMs > 262000u ? 262000u : frameMs) * 1000u;   // us; RadioLib divides by 15.625
    cfgScan.cad.irqFlags = LOOK_IRQS;
    cfgScan.cad.irqMask  = LOOK_IRQS;
    xSemaphoreTake(s_irq, 0);
    int16_t st = chip(_radio)->startChannelScan(cfgScan);
    radioRelease();
    if (st != RADIOLIB_ERR_NONE) { vTaskDelay(pdMS_TO_TICKS(20)); return; }
    if (!waitIrq(cadMs(p) + 30)) {
        radioAcquire(); chip(_radio)->standby(); chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL); radioRelease();
        return;
    }
    radioAcquire();
    uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
    if (!(f & IRQ_CAD_DET)) {
        // Nothing there: the chip exits CAD to standby by itself.
        chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        radioRelease();
        return;
    }
    _st.cadHits++;
    _lastIrqMs = millis();
    // The chip is already in RX. Clear only the CAD bits: a PREAMBLE that came
    // in the same breath stays for the loop below.
    chip(_radio)->clearIrqFlags(IRQ_CAD_DET | IRQ_CAD_DONE);
    radioRelease();
    const uint32_t t0 = millis();
    const uint32_t preBy = quickPreambleMs(p), hdrBy = headerDeadlineMs(p);
    bool gotPre = (f & IRQ_PREAMBLE) != 0, gotHdr = (f & IRQ_HDR_VALID) != 0;
    if (gotPre) _st.mtPre++;
    if (gotHdr) _st.mtHdr++;
    _busyUntil = t0 + (gotHdr ? frameMs : hdrBy);
    bool done = false;
    while (!done && millis() - t0 < frameMs + 50 && !_paused && !_off) {
        const uint32_t el = millis() - t0;
        uint32_t wait = frameMs + 50 - el;
        if (!gotPre) {
            if (el >= preBy) break;
            if (wait > preBy - el) wait = preBy - el;
        }
        if (!gotHdr) {
            if (el >= hdrBy) break;
            if (wait > hdrBy - el) wait = hdrBy - el;
        }
        if (!waitIrq(wait)) continue;
        radioAcquire();
        f = (uint16_t)chip(_radio)->getIrqFlags();
        if ((f & IRQ_PREAMBLE) && !gotPre) { gotPre = true; _st.mtPre++; }
        if ((f & IRQ_HDR_VALID) && !gotHdr) { gotHdr = true; gotPre = true; _st.mtHdr++; _busyUntil = millis() + frameMs; }
        if (f & (IRQ_RX_DONE | IRQ_CRC_ERR)) {
            readFrame(ON_MESHTASTIC);
            _st.peekFrames++;
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            done = true;
        } else if (f & (IRQ_HDR_ERR | IRQ_TIMEOUT)) {
            if (f & IRQ_HDR_ERR) _st.mtHdrErr++;
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            done = true;
        } else if (f) {
            chip(_radio)->clearIrqFlags(f);
        } else if (dio1High()) {
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        }
        radioRelease();
    }
    if (!gotPre && !gotHdr) _st.cadMiss++;
    radioAcquire();
    chip(_radio)->standby();
    chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    radioRelease();
    _busyUntil = 0;
    _rxOn = false;
}

void DualMeshRadioWrapper::doTx(uint8_t which) {
    TxSlot* slot = nullptr;
    if (which == ON_MESHCORE) slot = &s_mcTx;
    else {
        xSemaphoreTake(s_q, portMAX_DELAY);
        if (s_mtTxR != s_mtTxW) slot = &s_mtTx[s_mtTxR];
        xSemaphoreGive(s_q);
    }
    if (!slot || slot->state != TX_PENDING) return;
    const RadioProfile& p = which == ON_MESHCORE ? _mc : _mt;
    const int8_t dbm = which == ON_MESHCORE ? _txDbm : DualMesh::cfg().mt_tx_dbm;
    radioAcquire();
    if (!applyProfile(p)) { radioRelease(); slot->state = TX_FAIL; goto done; }
    // Meshtastic: listen before talk on its own profile, with a random backoff.
    if (which == ON_MESHTASTIC) {
        for (int tries = 0; tries < 4; tries++) {
            ChannelScanConfig_t c;
            memset(&c, 0, sizeof c);
            c.cad.symNum = RADIOLIB_SX126X_CAD_ON_4_SYMB;
            c.cad.detPeak = RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
            c.cad.detMin = RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
            c.cad.exitMode = RADIOLIB_SX126X_CAD_GOTO_STDBY;
            c.cad.irqFlags = CAD_IRQS; c.cad.irqMask = CAD_IRQS;
            xSemaphoreTake(s_irq, 0);
            if (chip(_radio)->startChannelScan(c) != RADIOLIB_ERR_NONE) break;
            radioRelease();
            waitIrq((symUs(p) * 5) / 1000 + 8);
            radioAcquire();
            const uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            if (!(f & IRQ_CAD_DET)) break;
            _st.txDeferred++;
            radioRelease();
            vTaskDelay(pdMS_TO_TICKS(40 + (esp_random() % 160) * (tries + 1)));
            radioAcquire();
        }
    }
    {
        if (_txDbmDirty || which == ON_MESHTASTIC) { chip(_radio)->setOutputPower(dbm); _txDbmDirty = which == ON_MESHTASTIC; }
        _on = ON_TX;
        slot->state = TX_SENDING;
        _board->onBeforeTransmit();
        xSemaphoreTake(s_irq, 0);
        int16_t st = chip(_radio)->startTransmit(slot->buf, slot->len);
        radioRelease();
        bool ok = false;
        if (st == RADIOLIB_ERR_NONE) {
            const uint32_t limit = airMs(p, slot->len) * 3 / 2 + 200;
            const uint32_t t0 = millis();
            while (millis() - t0 < limit) {
                if (!waitIrq(20)) continue;
                radioAcquire();
                const uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
                radioRelease();
                if (f & IRQ_TX_DONE) { ok = true; break; }
            }
        }
        radioAcquire();
        chip(_radio)->finishTransmit();
        chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        radioRelease();
        _board->onAfterTransmit();
        _rxOn = false;
        _on = ON_NONE;
        slot->state = ok ? TX_OK : TX_FAIL;
        if (which == ON_MESHCORE) { if (ok) _st.mcTx++; else _st.mcTxFail++; }
        else                      { if (ok) _st.mtTx++; else _st.mtTxFail++; }
    }
done:
    if (which == ON_MESHTASTIC) {
        xSemaphoreTake(s_q, portMAX_DELAY);
        s_mtTx[s_mtTxR].state = TX_NONE;
        s_mtTxR = (uint8_t)((s_mtTxR + 1) % MTQ);
        xSemaphoreGive(s_q);
    }
}

void DualMeshRadioWrapper::taskBody() {
    for (;;) {
        if (_paused || _off) {
            if (!_parked) { radioAcquire(); chip(_radio)->standby(); radioRelease(); _on = ON_PAUSED; _rxOn = false; }
            _parked = true;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        _parked = false;
        if (_cfgDirty) refreshProfiles();
        if (_boostDirty) {
            radioAcquire(); chip(_radio)->setRxBoostedGainMode(_boosted); radioRelease();
            _boostDirty = false; _rxOn = false;
        }
        if (_agc) {
            radioAcquire(); doResetAGC(); radioRelease();
            _agc = false; _rxOn = false; _applied = RadioProfile{0, 0, 0, 0, 0, 0};
        }
        const uint8_t mode = _mode;
        // 1. transmit what is waiting: MeshCore first (its Dispatcher times it)
        if (mcTxWanted() && mode != MODE_MESHTASTIC) { doTx(ON_MESHCORE); continue; }
        if (s_mtTxR != s_mtTxW) {
            if (mode == MODE_MESHCORE) {   // Meshtastic TX off: drop the queue
                xSemaphoreTake(s_q, portMAX_DELAY); s_mtTxR = s_mtTxW; xSemaphoreGive(s_q);
            } else { doTx(ON_MESHTASTIC); continue; }
        }
        if (s_mcTx.state == TX_PENDING && mode == MODE_MESHTASTIC) s_mcTx.state = TX_FAIL;
        // 2. receive. Single-profile modes: a watchdog re-arms RX when the chip
        // has been silent for 20 s (no event at all, not even a noise preamble),
        // so a wedged receiver costs seconds, not the rest of the session.
        if (mode != MODE_DUAL && _rxOn && millis() - _lastIrqMs > 20000u) {
            _lastIrqMs = millis();
            _rxOn = false;
            _st.rearms++;
        }
        if (mode == MODE_MESHCORE)        listen(ON_MESHCORE, 100);
        else if (mode == MODE_MESHTASTIC) listen(ON_MESHTASTIC, 100);
        else {
            listen(ON_MESHCORE, homeMs());
            if (!mcTxWanted() && !_paused && !_off && !_cfgDirty) {
                if (cadLooks()) peek();
                else { _st.peeks++; listen(ON_MESHTASTIC, mtWindowMs()); }
            }
        }
    }
}
#endif  // SCOOBY_DECK
