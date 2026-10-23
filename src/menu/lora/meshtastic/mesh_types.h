#ifndef __MESH_TYPES_H__
#define __MESH_TYPES_H__

#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include <Arduino.h>
#include <stdint.h>

namespace mesh {

static constexpr uint8_t kSyncWord = 0x2b;
static constexpr uint16_t kPreambleLength = 16;
static constexpr uint8_t kHopLimit = 3;
static constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
static constexpr size_t kHeaderLen = 16;
static constexpr size_t kMaxPayload = 255;
static constexpr size_t kMaxFrame = kHeaderLen + kMaxPayload;
static constexpr size_t kMaxNodes = 24;
static constexpr size_t kDedupSlots = 32;
static constexpr size_t kMaxChatLines = 24;
static constexpr uint32_t kNodeInfoIntervalMs = 5UL * 60UL * 1000UL;
static constexpr int8_t kSx1262MaxPower = 22;

// PortNum from meshtastic/portnums.proto
static constexpr uint32_t kPortText = 1;
static constexpr uint32_t kPortNodeInfo = 4;

// PacketHeader flags (RadioInterface.h)
static constexpr uint8_t kFlagHopLimitMask = 0x07;
static constexpr uint8_t kFlagHopStartMask = 0xE0;
static constexpr uint8_t kFlagHopStartShift = 5;

/*
 * 16 bytes of random PSK for the public default channel (AES-128).
 * From meshtastic/firmware src/mesh/Channels.h — keep attribution if copied.
 * PSK index 1 (AQ==) expands to this key; indices 2–255 bump the last byte.
 */
static const uint8_t kDefaultPsk[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

enum class Preset : uint8_t {
    LongFast = 0,
    LongSlow = 1,
    MediumSlow = 2,
    MediumFast = 3,
};

struct PresetParams {
    const char *name;
    float bwKHz;
    uint8_t sf;
    uint8_t cr; // coding-rate denominator (4/cr)
};

struct RegionInfo {
    const char *id;
    const char *label;
    float freqStartMHz;
    float freqEndMHz;
    float dutyCyclePercent;
    int8_t powerLimitDbm;
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t flags;
    uint8_t channel;
    uint8_t next_hop;
    uint8_t relay_node;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");

struct HeardNode {
    uint32_t num = 0;
    char shortName[5] = {};
    char longName[25] = {};
    float snr = 0;
    uint32_t lastHeardMs = 0;
};

inline const PresetParams &presetParams(Preset p) {
    // modemPresetToParams from meshtastic MeshRadio.h (sub-GHz / non-wide)
    static const PresetParams table[] = {
        {"LongFast",   250.0f, 11, 5},
        {"LongSlow",   125.0f, 12, 8},
        {"MediumSlow", 250.0f, 10, 5},
        {"MediumFast", 250.0f,  9, 5},
    };
    uint8_t i = (uint8_t)p;
    if (i >= sizeof(table) / sizeof(table[0])) i = 0;
    return table[i];
}

inline const RegionInfo &regionEu868() {
    static const RegionInfo r = {
        "EU_868", "EU868", 869.4f, 869.65f, 10.0f, 27
    };
    return r;
}

inline uint32_t djb2(const char *str) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*str++) != 0) h = ((h << 5) + h) + (uint32_t)c;
    return h;
}

inline const char *channelHashName(const char *storedName, Preset preset) {
    if (storedName && storedName[0] != '\0') return storedName;
    return presetParams(preset).name;
}

} // namespace mesh

#endif
#endif
