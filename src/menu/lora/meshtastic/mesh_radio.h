#ifndef __MESH_RADIO_H__
#define __MESH_RADIO_H__

#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include "mesh_packet.h"
#include "mesh_types.h"

namespace mesh {

struct MeshConfig {
    Preset preset = Preset::LongFast;
    char channelName[12] = "";
    uint8_t pskStored[32] = {1}; // default index 1
    size_t pskStoredLen = 1;
    char shortName[5] = "";
    char longName[25] = "";
    uint32_t nodeNum = 0;
};

struct RxEvent {
    PacketHeader hdr{};
    DecodedData data{};
    float snr = 0;
    bool ok = false;
};

bool startMeshRadio(const MeshConfig &cfg);
void stopMeshRadio();
bool meshRadioActive();

float meshFrequencyMHz();
int8_t meshTxPowerDbm();

bool meshSendRaw(const uint8_t *frame, size_t len, uint32_t airtimeMsEstimate);
bool meshSendText(const MeshConfig &cfg, const char *text);
bool meshSendNodeInfo(const MeshConfig &cfg);

// Non-blocking: returns true when a decoded channel packet is available.
bool meshPollRx(const MeshConfig &cfg, RxEvent &ev);

bool meshDutyAllows(uint32_t airtimeMs);
void meshDutyRecord(uint32_t airtimeMs);
uint32_t estimateAirtimeMs(size_t frameLen, const PresetParams &pp);

bool isDuplicate(uint32_t from, uint32_t id);
void rememberPacket(uint32_t from, uint32_t id);

} // namespace mesh

#endif
#endif
