#if defined(SCOOBY_DECK)
// DualMesh — settings store. See DualMeshConfig.h.
#include "DualMeshConfig.h"
#include "lora_meshtastic.h"
#include "lora_crypto.h"
#include "DualMeshStore.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <strings.h>
#if defined(ESP32)
#include <Preferences.h>
#endif

namespace DualMesh {

static const uint16_t CFG_VERSION = 1;

static const MtPreset PRESETS[] = {
    { "LongFast",     250.0f, 11, 5 },
    { "LongSlow",     125.0f, 12, 8 },
    { "LongModerate", 125.0f, 11, 8 },
    { "LongTurbo",    500.0f, 11, 8 },
    { "MediumSlow",   250.0f, 10, 5 },
    { "MediumFast",   250.0f,  9, 5 },
    { "ShortSlow",    250.0f,  8, 5 },
    { "ShortFast",    250.0f,  7, 5 },
    { "ShortTurbo",   500.0f,  7, 5 },
};
const MtPreset* mtPresets() { return PRESETS; }
uint8_t mtPresetCount() { return sizeof PRESETS / sizeof PRESETS[0]; }
int mtPresetFind(const char* name) {
    for (uint8_t i = 0; i < mtPresetCount(); i++) if (strcasecmp(PRESETS[i].name, name) == 0) return i;
    return -1;
}

static Config   s_cfg;
static uint32_t s_changes = 0;
Config& cfg() { return s_cfg; }
uint32_t changeCount() { return s_changes; }

void resetDefaults() {
    memset(&s_cfg, 0, sizeof s_cfg);
    s_cfg.version      = CFG_VERSION;
    s_cfg.mode         = MODE_DUAL;          // the user asked for dual mode out of the box
    // v2.0: stock Meshtastic out of the box: LongFast on the default slot
    // (0 = the hash of the primary channel name, as stock firmware does; US
    // LongFast lands on slot 20, 906.875 MHz).
    s_cfg.mt_preset    = (uint8_t)mtPresetFind("LongFast");
    s_cfg.mt_bw        = 250.0f;
    s_cfg.mt_sf        = 11;
    s_cfg.mt_cr        = 5;
    s_cfg.mt_slot      = 0;
    s_cfg.mt_freq      = 0.0f;
    s_cfg.mt_region_lo = 902.0f;
    s_cfg.mt_region_hi = 928.0f;
    s_cfg.mt_sync      = 0x2B;
    s_cfg.mt_preamble  = 16;
    s_cfg.mt_hops      = 3;
    s_cfg.mt_tx_dbm    = 22;
    s_cfg.mt_announce  = 1;
    s_cfg.shared_sf    = 0;
    s_cfg.tdm_home_ms  = 0;
    s_cfg.tdm_cad_syms = 0;
    s_cfg.long_name[0] = '\0';               // empty = WadaMesh's node name
    s_cfg.short_name[0]= '\0';
    s_cfg.v2           = 1;
    s_cfg.mt_sf_ovr    = 0;                  // the preset's SF (LongFast: SF11)
    // Slot 0: the primary. Empty name = the preset's own name, which is what a
    // stock node on that preset hashes its default channel under; key AQ== (the
    // published default key).
    s_cfg.ch[0].name[0] = '\0';
    Meshtastic::expandPsk(1, s_cfg.ch[0].key);
    s_cfg.ch[0].keyLen  = 16;
    s_cfg.ch[0].enabled = 1;
}

// v1.2.0: the settings live in /dm_cfg.bin on SPIFFS (see DualMeshStore.h),
// which a full 0x0 reflash does not touch. Load order: the file; else the old
// NVS blob (migrated to the file once); else defaults (Mt::begin then makes a
// new PKI key and saves it).
static bool s_fileOk = false;   // the current settings are in the file
static bool s_noStoreAtLoad = false;   // SPIFFS was not mounted when load() ran

static bool valid(const Config& t) { return t.version == CFG_VERSION; }
// v1.x blobs end at mt_priv_set; v2 appended fields after it.
static const size_t CFG_V1_SIZE = offsetof(Config, mt_priv_set) + 1;
static bool getCfg(Config& tmp) {
    memset(&tmp, 0, sizeof tmp);
    const long n = Store::get("cfg", &tmp, sizeof tmp);
    // A shorter (v1) blob: its trailing padding byte must not read as a v2 field.
    if (n >= (long)CFG_V1_SIZE && n < (long)sizeof tmp)
        memset((uint8_t*)&tmp + CFG_V1_SIZE, 0, sizeof tmp - CFG_V1_SIZE);
    return n >= (long)CFG_V1_SIZE && n <= (long)sizeof tmp && valid(tmp);
}

void load() {
    resetDefaults();
    Config tmp;
    const char* from = "defaults";
    s_noStoreAtLoad = !Store::ready();
    if (getCfg(tmp)) {
        s_cfg = tmp; s_fileOk = true; from = "SPIFFS /dm_cfg.bin";
    }
#if defined(ESP32)
    else {
        Preferences p;
        if (p.begin("dualmesh", true)) {
            memset(&tmp, 0, sizeof tmp);
            const size_t n = p.getBytes("cfg", &tmp, sizeof tmp);
            if (n >= CFG_V1_SIZE && n < sizeof tmp) memset((uint8_t*)&tmp + CFG_V1_SIZE, 0, sizeof tmp - CFG_V1_SIZE);
            if (n >= CFG_V1_SIZE && valid(tmp)) { s_cfg = tmp; from = "NVS (migrating to SPIFFS)"; }
            p.end();
        }
        if (strcmp(from, "defaults") != 0) s_fileOk = Store::put("cfg", &s_cfg, sizeof s_cfg);
    }
#endif
    Serial.printf("[dm-cfg] settings from %s, PKI key %s\n", from, s_cfg.mt_priv_set ? "kept" : "not set yet");
    s_cfg.long_name[sizeof s_cfg.long_name - 1] = 0;
    s_cfg.short_name[sizeof s_cfg.short_name - 1] = 0;
    if (s_cfg.mode > MODE_MESHTASTIC) s_cfg.mode = MODE_DUAL;
    // v2.0: one SF setting per protocol (Meshtastic: mt_sf_ovr here; MeshCore:
    // its own radio settings), so the v1 shared SF is retired.
    s_cfg.shared_sf = 0;
    if (s_cfg.mt_sf_ovr && (s_cfg.mt_sf_ovr < 5 || s_cfg.mt_sf_ovr > 12)) s_cfg.mt_sf_ovr = 0;
    if (!s_cfg.v2) {
        // First boot on v2 with v1 settings: move the Meshtastic radio to the v2
        // defaults (LongFast, default slot, preset SF); names, channels, PKI key
        // and the mesh mode are kept.
        s_cfg.mt_preset = (uint8_t)mtPresetFind("LongFast");
        s_cfg.mt_slot = 0; s_cfg.mt_freq = 0.0f; s_cfg.mt_sf_ovr = 0;
        s_cfg.v2 = 1;
        Serial.println("[dm-cfg] v1 settings: Meshtastic radio moved to the v2 default (LongFast)");
        s_fileOk = Store::put("cfg", &s_cfg, sizeof s_cfg);
    }
    applyChannels();
}

void save() {
    s_fileOk = Store::put("cfg", &s_cfg, sizeof s_cfg);
#if defined(ESP32)
    // NVS only as a fallback when SPIFFS could not take it (a reflash wipes NVS,
    // and the 20 KB partition is shared with WadaMesh's boot counters).
    if (!s_fileOk) {
        Preferences p;
        if (p.begin("dualmesh", false)) { p.putBytes("cfg", &s_cfg, sizeof s_cfg); p.end(); }
    }
#endif
    applyChannels();
    s_changes++;
}

// From Mt::poll: settings that could only go to NVS (SPIFFS not mounted yet at
// the time) are written to the file once it is.
void storeRetry() {
    if (s_fileOk || !Store::ready()) return;
    if (s_noStoreAtLoad) {
        // Never overwrite a good file we simply could not read at boot.
        Config tmp;
        s_noStoreAtLoad = false;
        if (getCfg(tmp)) {
            Serial.println("[dm-cfg] SPIFFS mounted late: kept /dm_cfg.bin (restart to use it)");
            s_fileOk = true;
            return;
        }
    }
    s_fileOk = Store::put("cfg", &s_cfg, sizeof s_cfg);
}

const char* mtPresetName() {
    if (s_cfg.mt_preset < mtPresetCount()) return PRESETS[s_cfg.mt_preset].name;
    return "Custom";
}
const char* mtPrimaryName() {
    if (s_cfg.ch[0].name[0]) return s_cfg.ch[0].name;
    return s_cfg.mt_preset < mtPresetCount() ? PRESETS[s_cfg.mt_preset].name : "Custom";
}
uint8_t mtPrimaryHash() { return Meshtastic::channel(0).hash; }
float mtBw() { return s_cfg.mt_preset < mtPresetCount() ? PRESETS[s_cfg.mt_preset].bw : s_cfg.mt_bw; }
uint8_t mtSf() {
    if (s_cfg.mt_sf_ovr >= 5 && s_cfg.mt_sf_ovr <= 12) return s_cfg.mt_sf_ovr;
    return s_cfg.mt_preset < mtPresetCount() ? PRESETS[s_cfg.mt_preset].sf : s_cfg.mt_sf;
}
uint8_t mtCr() { return s_cfg.mt_preset < mtPresetCount() ? PRESETS[s_cfg.mt_preset].cr : s_cfg.mt_cr; }

uint16_t mtSlotCount() {
    const float span = s_cfg.mt_region_hi - s_cfg.mt_region_lo;
    const float bwMHz = mtBw() / 1000.0f;
    if (bwMHz <= 0 || span <= 0) return 1;
    const uint16_t n = (uint16_t)floorf(span / bwMHz + 0.0001f);
    return n ? n : 1;
}

float mtFreqMHz() {
    if (s_cfg.mt_freq > 100.0f) return s_cfg.mt_freq;
    const float bwMHz = mtBw() / 1000.0f;
    const uint16_t n = mtSlotCount();
    uint32_t idx;
    if (s_cfg.mt_slot >= 1) idx = (uint32_t)((s_cfg.mt_slot - 1) % n);
    else idx = LoraCrypto::djb2(mtPrimaryName()) % n;   // the firmware's hash rule for channel_num 0
    return s_cfg.mt_region_lo + bwMHz / 2.0f + (float)idx * bwMHz;
}

void applyChannels() {
    for (uint8_t i = 0; i < MT_CHANNELS; i++) {
        const MtChannel& c = s_cfg.ch[i];
        const char* nm = (i == 0 && !c.name[0]) ? mtPrimaryName() : c.name;
        Meshtastic::setChannelSlot(i, nm, c.key, c.keyLen, c.enabled && nm[0]);
    }
}

bool setChannel(uint8_t i, const char* name, const char* keyText, bool enabled) {
    if (i >= MT_CHANNELS) return false;
    uint8_t k[32];
    const int n = Meshtastic::parseKey(keyText, k);
    if (n < 0) return false;
    MtChannel& c = s_cfg.ch[i];
    memset(&c, 0, sizeof c);
    if (name) { strncpy(c.name, name, sizeof c.name - 1); c.name[sizeof c.name - 1] = 0; }
    memcpy(c.key, k, 32);
    c.keyLen = (uint8_t)n;
    c.enabled = enabled ? 1 : 0;
    if (i != 0 && !c.name[0]) c.enabled = 0;   // a secondary channel needs a name
    return true;
}

void keyHex(const MtChannel& c, char* out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (!c.keyLen || cap < (size_t)c.keyLen * 2 + 1) return;
    LoraCrypto::toHex(c.key, c.keyLen, out);
}

const char* modeName(uint8_t m) {
    switch (m) {
        case MODE_DUAL: return "Dual (MeshCore + Meshtastic)";
        case MODE_MESHCORE: return "MeshCore only";
        case MODE_MESHTASTIC: return "Meshtastic only";
    }
    return "?";
}

}
#endif  // SCOOBY_DECK
