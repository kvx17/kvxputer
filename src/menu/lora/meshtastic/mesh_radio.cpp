#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include "mesh_radio.h"
#include "menu/lora/LoRaRF.h"
#include "root/config/configPins.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <math.h>

extern KvxputerConfigPins kvxConfigPins;

namespace mesh {

static bool gActive = false;
static float gFreqMHz = 869.525f;
static int8_t gPower = 22;
static float gDutyPercent = 10.0f;
static PresetParams gPreset = presetParams(Preset::LongFast);

static constexpr size_t kDutySlots = 64;
static uint32_t gDutyMs[kDutySlots] = {};
static uint32_t gDutyAt[kDutySlots] = {};
static size_t gDutyHead = 0;

static uint32_t gDedupFrom[kDedupSlots] = {};
static uint32_t gDedupId[kDedupSlots] = {};
static size_t gDedupHead = 0;

static void holdSdCsHigh() {
#ifdef SDCARD_CS
    if (SDCARD_CS >= 0) {
        pinMode(SDCARD_CS, OUTPUT);
        digitalWrite(SDCARD_CS, HIGH);
    }
#endif
    int cs = getLoraCsPin();
    if (cs >= 0) {
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);
    }
}

static float computeFrequency(const MeshConfig &cfg, const PresetParams &pp) {
    const RegionInfo &r = regionEu868();
    float bwMHz = pp.bwKHz / 1000.0f;
    float span = r.freqEndMHz - r.freqStartMHz;
    uint32_t numSlots = (uint32_t)lroundf((span + 0.0f) / bwMHz);
    if (numSlots == 0) numSlots = 1;
    const char *hashName = channelHashName(cfg.channelName, cfg.preset);
    uint32_t slot = djb2(hashName) % numSlots;
    return r.freqStartMHz + (pp.bwKHz / 2000.0f) + (slot * bwMHz);
}

static bool needsLdro(const PresetParams &pp) {
    float symMs = (float)(1u << pp.sf) / pp.bwKHz;
    return symMs >= 16.0f;
}

static int16_t beginWithTcxo(float freq, const PresetParams &pp, float tcxo, bool useLdo) {
    return lora1262->begin(
        freq, pp.bwKHz, pp.sf, pp.cr, kSyncWord, gPower, kPreambleLength, tcxo, useLdo
    );
}

uint32_t estimateAirtimeMs(size_t frameLen, const PresetParams &pp) {
    float bwHz = pp.bwKHz * 1000.0f;
    float tSym = (float)(1u << pp.sf) / bwHz;
    bool lowData = tSym > 16e-3f;
    float tPreamble = (kPreambleLength + 4.25f) * tSym;
    float payloadSym = 8.0f +
        fmaxf(
            ceilf(
                ((8.0f * (float)frameLen - 4.0f * pp.sf + 28.0f + 16.0f) /
                 (4.0f * (pp.sf - 2.0f * (lowData ? 1.0f : 0.0f)))) *
                (float)pp.cr
            ),
            0.0f
        );
    float tPacket = tPreamble + payloadSym * tSym;
    uint32_t ms = (uint32_t)(tPacket * 1000.0f);
    return ms < 1 ? 1 : ms;
}

bool meshDutyAllows(uint32_t airtimeMs) {
    if (gDutyPercent >= 99.0f) return true;
    uint32_t now = millis();
    uint32_t window = 3600000UL;
    uint32_t used = 0;
    for (size_t i = 0; i < kDutySlots; i++) {
        if (gDutyAt[i] != 0 && (now - gDutyAt[i]) < window) used += gDutyMs[i];
    }
    uint32_t budget = (uint32_t)(window * (gDutyPercent / 100.0f));
    return (used + airtimeMs) <= budget;
}

void meshDutyRecord(uint32_t airtimeMs) {
    gDutyMs[gDutyHead] = airtimeMs;
    gDutyAt[gDutyHead] = millis();
    gDutyHead = (gDutyHead + 1) % kDutySlots;
}

bool isDuplicate(uint32_t from, uint32_t id) {
    for (size_t i = 0; i < kDedupSlots; i++) {
        if (gDedupFrom[i] == from && gDedupId[i] == id) return true;
    }
    return false;
}

void rememberPacket(uint32_t from, uint32_t id) {
    gDedupFrom[gDedupHead] = from;
    gDedupId[gDedupHead] = id;
    gDedupHead = (gDedupHead + 1) % kDedupSlots;
}

float meshFrequencyMHz() { return gFreqMHz; }
int8_t meshTxPowerDbm() { return gPower; }
bool meshRadioActive() { return gActive; }

bool startMeshRadio(const MeshConfig &cfg) {
    stopMeshRadio();
    clearLoraRadio();
    loraPacketReceived = false;
    loraInterruptEnabled = true;

    const int irqPin = getLoraIrqPin();
    const int busyPin = getLoraBusyPin();
    if (getLoraCsPin() < 0 || kvxConfigPins.LoRa_bus.mosi == GPIO_NUM_NC || irqPin < 0) {
        Serial.println("Mesh: LoRa pins not configured");
        return false;
    }

    holdSdCsHigh();
    ensureLoraCapAntenna();

    gPreset = presetParams(cfg.preset);
    gFreqMHz = computeFrequency(cfg, gPreset);
    const RegionInfo &r = regionEu868();
    gDutyPercent = r.dutyCyclePercent;
    gPower = r.powerLimitDbm;
    if (gPower > kSx1262MaxPower) gPower = kSx1262MaxPower;
    if (gPower < -9) gPower = -9;

    loraSpi = selectLoraSPIBus();
    loraModule = new Module(getLoraCsPin(), irqPin, getLoraResetPin(), busyPin, *loraSpi);
    lora1262 = new SX1262(loraModule);

    // Plan: try TCXO 1.8 V then crystal. Cap LoRa-1262 often needs 3.0 V (M5 example).
    int16_t state = RADIOLIB_ERR_UNKNOWN;
    float usedTcxo = 1.8f;
    bool usedLdo = false;
    const float tryVolts[] = {1.8f, 3.0f, 0.0f};
    const bool tryLdo[] = {false, true, false};
    for (size_t i = 0; i < 3; i++) {
        usedTcxo = tryVolts[i];
        usedLdo = tryLdo[i];
        state = beginWithTcxo(gFreqMHz, gPreset, usedTcxo, usedLdo);
        if (state == RADIOLIB_ERR_NONE) break;
        Serial.printf("Mesh: SX1262 begin TCXO=%.1f LDO=%d failed (%d)\n", usedTcxo, usedLdo, state);
    }
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("Mesh: SX1262 init failed (%d)\n", state);
        clearLoraRadio();
        return false;
    }
    Serial.printf(
        "Mesh: SX1262 up @ %.3f MHz SF%u BW%.1f CR4/%u sync=0x%02X TCXO=%.1f P=%ddBm\n", gFreqMHz,
        gPreset.sf, gPreset.bwKHz, gPreset.cr, kSyncWord, usedTcxo, gPower
    );

    lora1262->setDio2AsRfSwitch(true);
    lora1262->setCurrentLimit(140.0f);
    lora1262->setCRC(2);
    if (needsLdro(gPreset)) lora1262->forceLDRO(true);
    else lora1262->forceLDRO(false);
    lora1262->setDio1Action(onLoraPacket);
    state = lora1262->startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("Mesh: startReceive failed (%d)\n", state);
        clearLoraRadio();
        return false;
    }
    gActive = true;
    return true;
}

void stopMeshRadio() {
    if (lora1262) {
        loraInterruptEnabled = false;
        lora1262->standby();
        lora1262->sleep();
    }
    holdSdCsHigh();
    clearLoraRadio();
    gActive = false;
}

bool meshSendRaw(const uint8_t *frame, size_t len, uint32_t airtimeMsEstimate) {
    if (!gActive || !lora1262 || !frame || len == 0) return false;
    if (!meshDutyAllows(airtimeMsEstimate)) {
        Serial.println("Mesh: duty cycle limit");
        return false;
    }
    holdSdCsHigh();
    loraInterruptEnabled = false;
    int16_t state = lora1262->transmit(frame, len);
    lora1262->startReceive();
    loraInterruptEnabled = true;
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("Mesh: TX failed (%d)\n", state);
        return false;
    }
    meshDutyRecord(airtimeMsEstimate);
    return true;
}

static bool sendEncoded(const MeshConfig &cfg, uint32_t portnum, const uint8_t *payload, size_t payloadLen) {
    CryptoKey key = expandPsk(cfg.pskStored, cfg.pskStoredLen);
    const char *hashName = channelHashName(cfg.channelName, cfg.preset);
    uint8_t ch = channelHash(hashName, key);

    uint8_t plain[240];
    size_t plainLen = encodeDataText(plain, sizeof(plain), portnum, payload, payloadLen);
    if (!plainLen) return false;

    PacketHeader hdr{};
    hdr.to = kBroadcast;
    hdr.from = cfg.nodeNum ? cfg.nodeNum : makeNodeNumFromMac();
    hdr.id = nextPacketId();
    hdr.flags = (uint8_t)(kHopLimit | (kHopLimit << kFlagHopStartShift));
    hdr.channel = ch;
    hdr.next_hop = 0;
    hdr.relay_node = 0;

    uint8_t frame[kMaxFrame];
    size_t frameLen = buildMeshFrame(frame, sizeof(frame), hdr, plain, plainLen, key);
    if (!frameLen) return false;

    uint32_t air = estimateAirtimeMs(frameLen, gPreset);
    bool ok = meshSendRaw(frame, frameLen, air);
    if (ok) rememberPacket(hdr.from, hdr.id);
    return ok;
}

bool meshSendText(const MeshConfig &cfg, const char *text) {
    if (!text || !text[0]) return false;
    return sendEncoded(cfg, kPortText, (const uint8_t *)text, strlen(text));
}

bool meshSendNodeInfo(const MeshConfig &cfg) {
    char id[16];
    snprintf(id, sizeof(id), "!%08x", (unsigned)cfg.nodeNum);
    uint8_t userPb[96];
    size_t userLen = encodeUser(userPb, sizeof(userPb), id, cfg.longName, cfg.shortName);
    if (!userLen) return false;
    return sendEncoded(cfg, kPortNodeInfo, userPb, userLen);
}

bool meshPollRx(const MeshConfig &cfg, RxEvent &ev) {
    ev = RxEvent{};
    if (!gActive || !lora1262) return false;
    if (!loraPacketReceived) return false;
    loraPacketReceived = false;

    size_t len = lora1262->getPacketLength();
    if (len < kHeaderLen || len > kMaxFrame) {
        lora1262->startReceive();
        return false;
    }
    uint8_t frame[kMaxFrame];
    int16_t state = lora1262->readData(frame, len);
    float snr = lora1262->getSNR();
    lora1262->startReceive();
    if (state != RADIOLIB_ERR_NONE) return false;

    CryptoKey key = expandPsk(cfg.pskStored, cfg.pskStoredLen);
    const char *hashName = channelHashName(cfg.channelName, cfg.preset);
    uint8_t ch = channelHash(hashName, key);

    PacketHeader hdr{};
    DecodedData data{};
    if (!parseMeshFrame(frame, len, hdr, data, key, ch)) return false;
    if (isDuplicate(hdr.from, hdr.id)) return false;
    rememberPacket(hdr.from, hdr.id);

    ev.hdr = hdr;
    ev.data = data;
    ev.snr = snr;
    ev.ok = true;
    return true;
}

} // namespace mesh

#endif
