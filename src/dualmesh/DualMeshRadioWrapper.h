// DualMesh — one SX1262, two meshes. A drop-in WRAPPER_CLASS for WadaMesh's
// MeshCore stack: MeshCore's Dispatcher sees an ordinary radio, while a task
// on core 0 owns the chip and time-slices it between the MeshCore profile
// ("home") and the Meshtastic profile ("peek"), the way SquachWatch-CYD's
// T-Deck BOTH mode does (lora_sniffer.cpp homeAndPeek(), GPL-3.0).
//
// Modes: DUAL (home MeshCore + CAD peeks at Meshtastic), MESHCORE only,
// MESHTASTIC only (continuous RX on that profile, the other stack's TX off).
#pragma once
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "lora_pkt.h"

namespace DualMesh {
struct RadioProfile {
    float    freq;       // MHz
    float    bw;         // kHz
    uint8_t  sf;
    uint8_t  cr;         // 5..8
    uint8_t  sync;       // RadioLib one-byte form: 0x12 MeshCore, 0x2B Meshtastic
    uint16_t preamble;   // symbols
    bool operator==(const RadioProfile& o) const {
        return freq == o.freq && bw == o.bw && sf == o.sf && cr == o.cr && sync == o.sync && preamble == o.preamble;
    }
};
enum OnProfile : uint8_t { ON_NONE = 0, ON_MESHCORE = 1, ON_MESHTASTIC = 2, ON_TX = 3, ON_PAUSED = 4 };
struct RadioStats {
    uint32_t mcRx, mtRx, mcTx, mtTx, mcTxFail, mtTxFail, peeks, cadHits, peekFrames, crcErr, txDeferred;
    int16_t  noise;
};
}

class DualMeshRadioWrapper : public CustomSX1262Wrapper {
public:
    DualMeshRadioWrapper(CustomSX1262& radio, mesh::MainBoard& board) : CustomSX1262Wrapper(radio, board) {}

    // mesh::Radio / RadioLibWrapper overrides (MeshCore's side)
    void     begin() override;
    int      recvRaw(uint8_t* bytes, int sz) override;
    uint32_t getEstAirtimeFor(int len_bytes) override;
    bool     startSendRaw(const uint8_t* bytes, int len) override;
    bool     isSendComplete() override;
    void     onSendFinished() override;
    bool     isInRecvMode() const override;
    bool     isReceiving() override;
    void     setParams(float freq, float bw, uint8_t sf, uint8_t cr) override;
    float    getCurrentRSSI() override;
    float    getLastRSSI() const override;
    float    getLastSNR() const override;
    float    packetScore(float snr, int packet_len) override;
    uint8_t  getSpreadingFactor() const override;
    void     powerOff() override;
    void     loop() override;
    void     resetAGC() override;
    bool     setRxBoostedGainMode(bool en) override;
    bool     getRxBoostedGainMode() const override { return _boosted; }
    // Hide the base's direct-SPI helpers: the radio task applies them.
    void     setTxPower(int8_t dbm);
    uint32_t getRngSeed();

    // DualMesh side
    // The spectrum analyser drives the chip itself: park the task first.
    void     pauseForSpectrum(bool on);
    bool     paused() const { return _paused; }
    // A ready Meshtastic frame; false when the queue is full or Meshtastic is off.
    bool     queueMeshtastic(const uint8_t* f, uint16_t n);
    // The next Meshtastic frame heard, false when none.
    bool     popMeshtastic(Lora::Packet& pk);
    // Re-read DualMesh::cfg() (mode, profiles) on the task's next turn.
    void     configChanged() { _cfgDirty = true; }
    uint8_t  onProfile() const { return _on; }
    DualMesh::RadioStats stats() const { return _st; }
    DualMesh::RadioProfile mcProfile() const { return _mc; }
    DualMesh::RadioProfile mtProfile() const { return _mt; }
    uint32_t homeMs() const;

    // internal, used by the task
    void     taskBody();

private:
    bool applyProfile(const DualMesh::RadioProfile& p);
    bool startRx();
    bool waitIrq(uint32_t ms);
    void listen(uint8_t which, uint32_t ms);
    void peek();
    void doTx(uint8_t which);
    void readFrame(uint8_t which);
    void refreshProfiles();
    bool mcTxWanted() const;

    DualMesh::RadioProfile _mc { 910.525f, 62.5f, 7, 5, 0x12, 32 };
    DualMesh::RadioProfile _mt { 913.875f, 250.0f, 10, 5, 0x2B, 16 };
    DualMesh::RadioProfile _applied { 0, 0, 0, 0, 0, 0 };
    float   _mcFreq = 910.525f, _mcBw = 62.5f;
    uint8_t _mcSf = 7, _mcCr = 5;
    volatile bool    _cfgDirty = true;
    volatile bool    _paused = false;
    volatile bool    _parked = false;
    volatile bool    _off = false;
    volatile bool    _agc = false;
    volatile bool    _boosted = false;
    volatile bool    _boostDirty = false;
    volatile int8_t  _txDbm = 22;
    volatile bool    _txDbmDirty = true;
    volatile uint8_t _on = DualMesh::ON_NONE;
    volatile uint8_t _mode = 0;
    volatile uint32_t _busyUntil = 0;
    bool     _rxOn = false;
    float    _lastRssi = 0, _lastSnr = 0;
    volatile float _curRssi = -120;
    DualMesh::RadioStats _st {};
};
