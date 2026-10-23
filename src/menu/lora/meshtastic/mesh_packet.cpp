#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include "mesh_packet.h"
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <string.h>

namespace mesh {

static size_t writeVarint(uint8_t *out, size_t max, uint32_t v) {
    size_t n = 0;
    while (v >= 0x80) {
        if (n >= max) return 0;
        out[n++] = (uint8_t)((v & 0x7f) | 0x80);
        v >>= 7;
    }
    if (n >= max) return 0;
    out[n++] = (uint8_t)v;
    return n;
}

static bool readVarint(const uint8_t *in, size_t inLen, size_t &pos, uint32_t &v) {
    v = 0;
    uint32_t shift = 0;
    while (pos < inLen && shift < 35) {
        uint8_t b = in[pos++];
        v |= (uint32_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

static size_t writeTagVarint(uint8_t *out, size_t max, uint32_t field, uint32_t v) {
    size_t n = writeVarint(out, max, (field << 3) | 0);
    if (!n) return 0;
    size_t m = writeVarint(out + n, max - n, v);
    return m ? n + m : 0;
}

static size_t writeTagBytes(uint8_t *out, size_t max, uint32_t field, const uint8_t *data, size_t len) {
    size_t n = writeVarint(out, max, (field << 3) | 2);
    if (!n) return 0;
    size_t m = writeVarint(out + n, max - n, (uint32_t)len);
    if (!m || n + m + len > max) return 0;
    memcpy(out + n + m, data, len);
    return n + m + len;
}

static size_t writeTagString(uint8_t *out, size_t max, uint32_t field, const char *s) {
    if (!s) s = "";
    return writeTagBytes(out, max, field, (const uint8_t *)s, strlen(s));
}

static uint8_t xorHash(const uint8_t *p, size_t len) {
    uint8_t code = 0;
    for (size_t i = 0; i < len; i++) code ^= p[i];
    return code;
}

CryptoKey expandPsk(const uint8_t *stored, size_t storedLen) {
    CryptoKey k;
    if (!stored || storedLen == 0) {
        k.length = 0;
        return k;
    }
    if (storedLen == 1) {
        uint8_t idx = stored[0];
        if (idx == 0) {
            k.length = 0;
            return k;
        }
        memcpy(k.bytes, kDefaultPsk, sizeof(kDefaultPsk));
        k.length = (int8_t)sizeof(kDefaultPsk);
        k.bytes[sizeof(kDefaultPsk) - 1] = (uint8_t)(k.bytes[sizeof(kDefaultPsk) - 1] + idx - 1);
        return k;
    }
    size_t copy = storedLen > sizeof(k.bytes) ? sizeof(k.bytes) : storedLen;
    memcpy(k.bytes, stored, copy);
    if (copy < 16) k.length = 16;
    else if (copy != 16 && copy < 32) k.length = 32;
    else k.length = (int8_t)copy;
    return k;
}

uint8_t channelHash(const char *nameForHash, const CryptoKey &key) {
    if (!nameForHash) nameForHash = "";
    uint8_t h = xorHash((const uint8_t *)nameForHash, strlen(nameForHash));
    if (key.length > 0) h ^= xorHash(key.bytes, (size_t)key.length);
    return h;
}

bool aesCtrInPlace(const CryptoKey &key, uint32_t fromNode, uint32_t packetId, uint8_t *bytes, size_t len) {
    if (key.length <= 0 || !bytes || len == 0) return key.length == 0;
    if (key.length != 16 && key.length != 32) return false;

    uint8_t nonce[16] = {};
    uint64_t id64 = packetId;
    memcpy(nonce, &id64, sizeof(id64));
    memcpy(nonce + 8, &fromNode, sizeof(fromNode));

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int bits = key.length * 8;
    if (mbedtls_aes_setkey_enc(&ctx, key.bytes, bits) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    size_t nc_off = 0;
    uint8_t stream_block[16] = {};
    uint8_t nonce_counter[16];
    memcpy(nonce_counter, nonce, 16);
    int rc = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce_counter, stream_block, bytes, bytes);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

size_t encodeDataText(uint8_t *out, size_t maxOut, uint32_t portnum, const uint8_t *payload, size_t payloadLen) {
    if (!out || maxOut < 4) return 0;
    size_t n = writeTagVarint(out, maxOut, 1, portnum);
    if (!n) return 0;
    if (payload && payloadLen > 0) {
        size_t m = writeTagBytes(out + n, maxOut - n, 2, payload, payloadLen);
        if (!m) return 0;
        n += m;
    }
    return n;
}

size_t encodeUser(uint8_t *out, size_t maxOut, const char *id, const char *longName, const char *shortName) {
    if (!out) return 0;
    size_t n = 0;
    size_t m = writeTagString(out + n, maxOut - n, 1, id ? id : "");
    if (!m) return 0;
    n += m;
    m = writeTagString(out + n, maxOut - n, 2, longName ? longName : "");
    if (!m) return 0;
    n += m;
    m = writeTagString(out + n, maxOut - n, 3, shortName ? shortName : "");
    if (!m) return 0;
    n += m;
    return n;
}

bool decodeData(const uint8_t *in, size_t inLen, DecodedData &out) {
    out = DecodedData{};
    size_t pos = 0;
    while (pos < inLen) {
        uint32_t tag = 0;
        if (!readVarint(in, inLen, pos, tag)) return false;
        uint32_t field = tag >> 3;
        uint32_t wire = tag & 7;
        if (wire == 0) {
            uint32_t v = 0;
            if (!readVarint(in, inLen, pos, v)) return false;
            if (field == 1) out.portnum = v;
        } else if (wire == 2) {
            uint32_t len = 0;
            if (!readVarint(in, inLen, pos, len)) return false;
            if (pos + len > inLen) return false;
            if (field == 2) {
                size_t copy = len > sizeof(out.payload) ? sizeof(out.payload) : len;
                memcpy(out.payload, in + pos, copy);
                out.payloadLen = copy;
            }
            pos += len;
        } else if (wire == 5) {
            if (pos + 4 > inLen) return false;
            pos += 4;
        } else if (wire == 1) {
            if (pos + 8 > inLen) return false;
            pos += 8;
        } else {
            return false;
        }
    }
    return true;
}

bool decodeUser(const uint8_t *in, size_t inLen, DecodedUser &out) {
    out = DecodedUser{};
    size_t pos = 0;
    while (pos < inLen) {
        uint32_t tag = 0;
        if (!readVarint(in, inLen, pos, tag)) return false;
        uint32_t field = tag >> 3;
        uint32_t wire = tag & 7;
        if (wire == 0) {
            uint32_t v = 0;
            if (!readVarint(in, inLen, pos, v)) return false;
            (void)v;
        } else if (wire == 2) {
            uint32_t len = 0;
            if (!readVarint(in, inLen, pos, len)) return false;
            if (pos + len > inLen) return false;
            auto copyStr = [&](char *dst, size_t dstMax) {
                size_t copy = len;
                if (copy >= dstMax) copy = dstMax - 1;
                memcpy(dst, in + pos, copy);
                dst[copy] = '\0';
            };
            if (field == 1) copyStr(out.id, sizeof(out.id));
            else if (field == 2) copyStr(out.longName, sizeof(out.longName));
            else if (field == 3) copyStr(out.shortName, sizeof(out.shortName));
            pos += len;
        } else if (wire == 5) {
            if (pos + 4 > inLen) return false;
            pos += 4;
        } else if (wire == 1) {
            if (pos + 8 > inLen) return false;
            pos += 8;
        } else {
            return false;
        }
    }
    return true;
}

size_t buildMeshFrame(
    uint8_t *frame, size_t maxFrame, const PacketHeader &hdr, const uint8_t *plainData, size_t plainLen,
    const CryptoKey &key
) {
    if (!frame || maxFrame < kHeaderLen + plainLen) return 0;
    memcpy(frame, &hdr, kHeaderLen);
    if (plainLen > 0) {
        memcpy(frame + kHeaderLen, plainData, plainLen);
        if (key.length > 0) {
            if (!aesCtrInPlace(key, hdr.from, hdr.id, frame + kHeaderLen, plainLen)) return 0;
        }
    }
    return kHeaderLen + plainLen;
}

bool parseMeshFrame(
    const uint8_t *frame, size_t frameLen, PacketHeader &hdr, DecodedData &data, const CryptoKey &key,
    uint8_t expectChannelHash
) {
    if (!frame || frameLen < kHeaderLen) return false;
    memcpy(&hdr, frame, kHeaderLen);
    if (hdr.channel != expectChannelHash) return false;
    size_t encLen = frameLen - kHeaderLen;
    uint8_t buf[kMaxPayload];
    if (encLen > sizeof(buf)) return false;
    if (encLen > 0) {
        memcpy(buf, frame + kHeaderLen, encLen);
        if (key.length > 0) {
            if (!aesCtrInPlace(key, hdr.from, hdr.id, buf, encLen)) return false;
        }
    }
    return decodeData(buf, encLen, data);
}

uint32_t makeNodeNumFromMac() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t nodeNum = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    if (nodeNum == 0 || nodeNum == kBroadcast || nodeNum < 4) {
        nodeNum = (esp_random() & 0x7FFFFFFFu) | 0x100;
        if (nodeNum == kBroadcast) nodeNum = 0x12345678u;
    }
    return nodeNum;
}

uint32_t nextPacketId() {
    uint32_t id = 0;
    do {
        id = esp_random();
    } while (id == 0);
    return id;
}

} // namespace mesh

#endif
