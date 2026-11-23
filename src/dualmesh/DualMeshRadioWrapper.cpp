// DualMesh — the time-division radio scheduler. See DualMeshRadioWrapper.h.
//
// Timing model (ported from SquachWatch-CYD src/lora_sniffer.cpp, GPL-3.0):
//  - home: plain continuous RX on the MeshCore profile; a look never starts
//    while a frame is arriving (PREAMBLE/HEADER_VALID extend the stay).
//  - peek: one CAD on the Meshtastic profile (2 symbols at SF>=10, else 4,
//    detection peak SF+16 at SF>=10); on a hit, RX there until the preamble,
//    header and frame land or their deadlines pass; then home again.
//  - homeMs(): how long to stay home between looks so a look lands inside
//    the peek profile's preamble with room for CAD + lock.
//  - TX: MeshCore's Dispatcher already does LBT through isReceiving(); its
//    frame goes out at the next turn of the loop. Meshtastic frames get a CAD
//    on their own profile and a random backoff (up to 4 tries) first.
#include <Arduino.h>
#include "DualMeshRadioWrapper.h"
#include "DualMeshConfig.h"
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
uint32_t headerDeadlineMs(const RadioProfile& p) { return (symUs(p) * (uint32_t)(p.preamble + 13)) / 1000 + 20; }
uint32_t maxFrameMs(const RadioProfile& p) {
    return Lora::timeOnAirUs(p.sf, bw10(p.bw), 8, p.preamble, 255, true, false, false) / 1000 + 50;
}
uint32_t airMs(const RadioProfile& p, uint16_t len) {
    return Lora::timeOnAirUs(p.sf, bw10(p.bw), p.cr, p.preamble, (uint8_t)(len > 255 ? 255 : len), true, false, false) / 1000;
}
inline CustomSX1262* chip(PhysicalLayer* r) { return (CustomSX1262*)r; }

void taskEntry(void* arg) { static_cast<DualMeshRadioWrapper*>(arg)->taskBody(); }
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

uint32_t DualMeshRadioWrapper::homeMs() const {
    const Config& c = DualMesh::cfg();
    if (c.tdm_home_ms >= 10) return c.tdm_home_ms;
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
    const bool cal = fabsf(p.freq - _applied.freq) > 30.0f;
    int16_t st = r->setFrequency(p.freq, !cal);
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
    const int16_t st = chip(_radio)->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, RX_IRQS, RX_IRQS, 0);
    _rxOn = st == RADIOLIB_ERR_NONE;
    return _rxOn;
}

bool DualMeshRadioWrapper::waitIrq(uint32_t ms) {
    return xSemaphoreTake(s_irq, pdMS_TO_TICKS(ms ? ms : 1)) == pdTRUE;
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
    if (st != RADIOLIB_ERR_NONE || (flags & IRQ_CRC_ERR)) { _st.crcErr++; n_recv_errors++; return; }
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
        if (!waitIrq(until - now)) continue;
        radioAcquire();
        const uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
        const uint32_t t = millis();
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
        } else {
            chip(_radio)->clearIrqFlags(f & (IRQ_PREAMBLE | IRQ_HDR_VALID));
        }
        radioRelease();
    }
    _busyUntil = 0;
}

// One look at the Meshtastic profile: CAD, and on a hit, receive the frame.
void DualMeshRadioWrapper::peek() {
    const RadioProfile& p = _mt;
    _st.peeks++;
    radioAcquire();
    if (!applyProfile(p)) { radioRelease(); return; }
    _on = ON_MESHTASTIC;
    ChannelScanConfig_t cfgScan;
    memset(&cfgScan, 0, sizeof cfgScan);
    const uint8_t syms = cadSymbols(p);
    cfgScan.cad.symNum   = syms <= 2 ? RADIOLIB_SX126X_CAD_ON_2_SYMB : syms >= 8 ? RADIOLIB_SX126X_CAD_ON_8_SYMB : RADIOLIB_SX126X_CAD_ON_4_SYMB;
    cfgScan.cad.detPeak  = cadPeak(p) ? cadPeak(p) : RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfgScan.cad.detMin   = RADIOLIB_SX126X_CAD_PARAM_DEFAULT;
    cfgScan.cad.exitMode = RADIOLIB_SX126X_CAD_GOTO_STDBY;
    cfgScan.cad.timeout  = 0;
    cfgScan.cad.irqFlags = CAD_IRQS;
    cfgScan.cad.irqMask  = CAD_IRQS;
    xSemaphoreTake(s_irq, 0);
    int16_t st = chip(_radio)->startChannelScan(cfgScan);
    radioRelease();
    if (st != RADIOLIB_ERR_NONE) return;
    waitIrq(cadMs(p) + 6);
    radioAcquire();
    uint16_t f = (uint16_t)chip(_radio)->getIrqFlags();
    chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    const bool hit = (f & IRQ_CAD_DET) != 0;
    if (!hit) { radioRelease(); return; }
    _st.cadHits++;
    startRx();
    radioRelease();
    // A real frame raises PREAMBLE within a few symbols; then HEADER, then DONE.
    uint32_t deadline = millis() + preambleDeadlineMs(p);
    bool gotPre = false, gotHdr = false;
    for (;;) {
        const uint32_t now = millis();
        if ((int32_t)(now - deadline) >= 0 || _paused || _off) break;
        if (!waitIrq(deadline - now)) continue;
        radioAcquire();
        f = (uint16_t)chip(_radio)->getIrqFlags();
        if ((f & IRQ_PREAMBLE) && !gotPre) { gotPre = true; deadline = millis() + headerDeadlineMs(p); _busyUntil = deadline; }
        if ((f & IRQ_HDR_VALID) && !gotHdr) { gotHdr = true; deadline = millis() + maxFrameMs(p); _busyUntil = deadline; }
        if (f & (IRQ_RX_DONE | IRQ_CRC_ERR)) {
            readFrame(ON_MESHTASTIC);
            _st.peekFrames++;
            chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
            radioRelease();
            break;
        }
        if (f & (IRQ_HDR_ERR | IRQ_TIMEOUT)) { chip(_radio)->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL); radioRelease(); break; }
        chip(_radio)->clearIrqFlags(f & (IRQ_PREAMBLE | IRQ_HDR_VALID));
        radioRelease();
    }
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
        // 2. receive
        if (mode == MODE_MESHCORE)        listen(ON_MESHCORE, 100);
        else if (mode == MODE_MESHTASTIC) listen(ON_MESHTASTIC, 100);
        else {
            listen(ON_MESHCORE, homeMs());
            if (!mcTxWanted() && !_paused && !_off && !_cfgDirty) peek();
        }
    }
}
