// DualMesh — persistent blobs on the internal SPIFFS partition (v1.2.0).
//
// Why not NVS: the merged image flashed at 0x0 covers 0x9000-0xE000 with 0xFF,
// so every full flash wiped the "dualmesh" NVS namespace (Meshtastic PKI key,
// names, channels, mode, radio). The spiffs partition (0xC90000) is far past
// the end of the merged image, so files there survive a reflash, like
// WadaMesh's own /new_prefs, contacts and identity.
//
// One file per blob: /dm_<name>.bin = "SDM1" | u32 len | u32 crc32 | payload.
// Writes go to /dm_<name>.tmp first and are renamed over the old file, so a
// reset mid-write leaves either the old file or a complete .tmp, never a torn
// one; a load that finds no valid .bin falls back to a valid .tmp.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace DualMesh {
namespace Store {

bool ready();                                            // SPIFFS mounted (mounts read/write, never formats)
bool put(const char* name, const void* data, size_t len);  // atomic; false if not stored
long length(const char* name);                           // payload bytes of a valid blob, -1 if none
long get(const char* name, void* buf, size_t cap);       // bytes read, -1 if none / bad / too big

}
}
