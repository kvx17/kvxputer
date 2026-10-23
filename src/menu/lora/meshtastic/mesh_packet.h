#ifndef __MESH_PACKET_H__
#define __MESH_PACKET_H__

#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include "mesh_types.h"

namespace mesh {

struct CryptoKey {
    uint8_t bytes[32] = {};
    int8_t length = 0; // 0 = no crypto, 16 = AES-128, 32 = AES-256
};

struct DecodedData {
    uint32_t portnum = 0;
    uint8_t payload[240] = {};
    size_t payloadLen = 0;
};

struct DecodedUser {
    char id[16] = {};
    char longName[25] = {};
    char shortName[5] = {};
};

// Expand PSK index / hex key from settings into a CryptoKey.
CryptoKey expandPsk(const uint8_t *stored, size_t storedLen);

uint8_t channelHash(const char *nameForHash, const CryptoKey &key);

bool aesCtrInPlace(const CryptoKey &key, uint32_t fromNode, uint32_t packetId, uint8_t *bytes, size_t len);

size_t encodeDataText(uint8_t *out, size_t maxOut, uint32_t portnum, const uint8_t *payload, size_t payloadLen);
size_t encodeUser(uint8_t *out, size_t maxOut, const char *id, const char *longName, const char *shortName);

bool decodeData(const uint8_t *in, size_t inLen, DecodedData &out);
bool decodeUser(const uint8_t *in, size_t inLen, DecodedUser &out);

size_t buildMeshFrame(
    uint8_t *frame, size_t maxFrame, const PacketHeader &hdr, const uint8_t *plainData, size_t plainLen,
    const CryptoKey &key
);

bool parseMeshFrame(
    const uint8_t *frame, size_t frameLen, PacketHeader &hdr, DecodedData &data, const CryptoKey &key,
    uint8_t expectChannelHash
);

uint32_t makeNodeNumFromMac();
uint32_t nextPacketId();

} // namespace mesh

#endif
#endif
