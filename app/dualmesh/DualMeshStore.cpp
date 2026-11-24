#if defined(SCOOBY_DECK)
// DualMesh — atomic blob files on SPIFFS. See DualMeshStore.h.
#include "DualMeshStore.h"
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <string.h>
#include <stdio.h>

namespace DualMesh {
namespace Store {

static const uint8_t MAGIC[4] = { 'S', 'D', 'M', '1' };
static const size_t  HDR = 12;
static const size_t  MAX_BLOB = 16384;
static bool     s_ok = false;
static uint32_t s_triedAt = 0;
static bool     s_tried = false;

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

bool ready() {
    if (s_ok) return true;
    const uint32_t now = millis();
    if (s_tried && now - s_triedAt < 5000) return false;   // not formatted yet: main.cpp may still fix it
    s_tried = true; s_triedAt = now;
    // Same arguments as main.cpp's mount, so whichever runs first wins and the
    // other is a no-op ("already mounted" returns true). Never formats.
    s_ok = SPIFFS.begin(false);
    if (!s_ok) Serial.println("[dm-store] SPIFFS not mounted yet");
    return s_ok;
}

static void path(char* out, size_t cap, const char* name, const char* ext) {
    snprintf(out, cap, "/dm_%s.%s", name, ext);
}

// Validates a file; returns payload length (and leaves `f` positioned at the
// payload) or -1.
static long check(File& f, uint8_t* buf, size_t cap, bool wantData) {
    if (!f) return -1;
    uint8_t h[HDR];
    if (f.read(h, HDR) != HDR || memcmp(h, MAGIC, 4) != 0) return -1;
    uint32_t len, crc;
    memcpy(&len, h + 4, 4); memcpy(&crc, h + 8, 4);
    if (len > MAX_BLOB) return -1;
    // Read through to check the CRC (into buf when it fits, else in chunks).
    uint32_t c = 0xFFFFFFFFu;
    uint8_t chunk[128];
    size_t left = len, at = 0;
    while (left) {
        const size_t n = left < sizeof chunk ? left : sizeof chunk;
        if (f.read(chunk, n) != n) return -1;
        for (size_t i = 0; i < n; i++) {
            c ^= chunk[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        if (wantData) { if (at + n > cap) return -1; memcpy(buf + at, chunk, n); }
        at += n; left -= n;
    }
    if (~c != crc) return -1;
    return (long)len;
}

static long load(const char* name, void* buf, size_t cap, bool wantData) {
    if (!ready()) return -1;
    char p[40], t[40];
    path(p, sizeof p, name, "bin");
    path(t, sizeof t, name, "tmp");
    { File f = SPIFFS.open(p, "r"); const long n = check(f, (uint8_t*)buf, cap, wantData); if (f) f.close(); if (n >= 0) return n; }
    File f = SPIFFS.open(t, "r");
    const long n = check(f, (uint8_t*)buf, cap, wantData);
    if (f) f.close();
    if (n >= 0) {   // a complete write whose rename never happened: finish it
        SPIFFS.remove(p);
        SPIFFS.rename(t, p);
    }
    return n;
}

long length(const char* name) { return load(name, nullptr, 0, false); }
long get(const char* name, void* buf, size_t cap) { return load(name, buf, cap, true); }

bool put(const char* name, const void* data, size_t len) {
    if (!ready() || len > MAX_BLOB || (!data && len)) return false;
    char p[40], t[40];
    path(p, sizeof p, name, "bin");
    path(t, sizeof t, name, "tmp");
    uint8_t h[HDR];
    memcpy(h, MAGIC, 4);
    const uint32_t l = (uint32_t)len, c = crc32((const uint8_t*)data, len);
    memcpy(h + 4, &l, 4); memcpy(h + 8, &c, 4);
    File f = SPIFFS.open(t, "w");
    if (!f) { Serial.printf("[dm-store] open %s failed\n", t); return false; }
    bool ok = f.write(h, HDR) == HDR && f.write((const uint8_t*)data, len) == len;
    f.close();
    if (ok) {   // read it back before it replaces the old copy
        File v = SPIFFS.open(t, "r");
        ok = check(v, nullptr, 0, false) == (long)len;
        if (v) v.close();
    }
    if (!ok) { SPIFFS.remove(t); Serial.printf("[dm-store] write %s failed\n", t); return false; }
    // SPIFFS rename refuses an existing target: drop the old copy first. If a
    // reset lands between the two, load() finds the valid .tmp and finishes.
    if (SPIFFS.exists(p)) SPIFFS.remove(p);
    if (!SPIFFS.rename(t, p)) { Serial.printf("[dm-store] rename %s failed (kept .tmp)\n", t); return true; }
    return true;
}

}
}
#endif  // SCOOBY_DECK
