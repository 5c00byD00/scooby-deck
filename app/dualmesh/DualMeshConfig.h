// DualMesh (WadaMesh fork) — persistent settings for the second mesh and the
// time-division radio scheduler. Stored in /dm_cfg.bin on SPIFFS since v1.2.0
// (DualMeshStore.h); the old NVS namespace "dualmesh" is read once to migrate.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace DualMesh {

enum Mode : uint8_t { MODE_DUAL = 0, MODE_MESHCORE = 1, MODE_MESHTASTIC = 2 };

// Meshtastic modem presets (name, BW kHz, SF, CR denominator). PRESET_CUSTOM
// uses the cfg's mt_bw/mt_sf/mt_cr fields.
struct MtPreset { const char* name; float bw; uint8_t sf; uint8_t cr; };
static const uint8_t PRESET_CUSTOM = 0xFF;
const MtPreset* mtPresets();
uint8_t         mtPresetCount();
int             mtPresetFind(const char* name);

static const uint8_t MT_CHANNELS = 8;
struct MtChannel {
    char    name[12];     // Meshtastic allows 11 chars; slot 0 empty = the preset's name
    uint8_t key[32];
    uint8_t keyLen;       // 0 none, 16 AES-128, 32 AES-256
    uint8_t enabled;
};

struct Config {
    uint16_t version;
    uint8_t  mode;            // Mode
    // Meshtastic radio
    uint8_t  mt_preset;       // index into mtPresets(), or PRESET_CUSTOM
    float    mt_bw;           // custom only
    uint8_t  mt_sf;           // custom only
    uint8_t  mt_cr;           // custom only
    uint16_t mt_slot;         // 1-based frequency slot (Meshtastic channel_num); 0 = hash of the primary name
    float    mt_freq;         // MHz override; 0 = from region + slot
    float    mt_region_lo;    // MHz, US 902.0
    float    mt_region_hi;    // MHz, US 928.0
    uint8_t  mt_sync;         // 0x2B
    uint8_t  mt_preamble;     // 16
    uint8_t  mt_hops;         // 3
    int8_t   mt_tx_dbm;       // 22
    uint8_t  mt_announce;     // send NodeInfo
    // Shared spreading factor: 0 = off (each mesh its own), else 5, 7 or 9 for both
    uint8_t  shared_sf;
    // TDM
    uint16_t tdm_home_ms;     // 0 = auto (from the peek profile's preamble)
    uint8_t  tdm_cad_syms;    // 0 = auto (2 at SF>=10, else 4)
    // Identity
    char     long_name[40];   // Meshtastic long_name (39 max); also MeshCore's node name (31 max)
    char     short_name[5];   // 4 max
    // Channels
    MtChannel ch[MT_CHANNELS];
    // PKI identity (Meshtastic DMs)
    uint8_t  mt_priv[32];
    uint8_t  mt_priv_set;
    // ---- v2.0 (appended; an older, shorter blob still loads) ----
    uint8_t  v2;              // 1 once the v2 radio defaults were applied (LongFast, auto slot)
    uint8_t  mt_sf_ovr;       // Meshtastic SF override: 0 = the preset's own SF, else 5-12
};

Config&  cfg();
void     load();
void     save();
void     resetDefaults();
void     storeRetry();               // write to SPIFFS if an earlier save could not

// Derived
float    mtFreqMHz();             // the Meshtastic centre frequency the radio uses
float    mtBw();
uint8_t  mtSf();                  // with the SF override (v2) applied
uint8_t  mtCr();
const char* mtPresetName();       // "MediumSlow" or "Custom"
const char* mtPrimaryName();
uint8_t     mtPrimaryHash();   // channel hash of slot 0 as decode() sees it
uint16_t mtSlotCount();           // slots in the region at the current BW
// Push the channel table into the Meshtastic codec.
void     applyChannels();
// Set one channel from UI text. Returns false for a bad key.
bool     setChannel(uint8_t i, const char* name, const char* keyText, bool enabled);
// Key as hex for display ("" for none).
void     keyHex(const MtChannel& c, char* out, size_t cap);
const char* modeName(uint8_t m);

uint32_t changeCount();           // bumps on every save, for the radio to re-read
}
