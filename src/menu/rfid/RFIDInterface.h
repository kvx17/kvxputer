/**
 * @file RFIDInterface.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Interface for RFID modules
 * @version 0.1
 * @date 2024-08-19
 */

#ifndef __RFID_INTERFACE_H__
#define __RFID_INTERFACE_H__

#include <globals.h>
#include <vector>

class RFIDInterface {
public:
    typedef struct {
        byte size;
        byte uidByte[10];
        byte sak;
        byte atqaByte[2];
    } Uid;

    typedef struct {
        String uid;
        String bcc;
        String sak;
        String atqa;
        String picc_type;
    } PrintableUID;

    typedef struct {
        byte begin = 0x03;
        byte messageSize;
        byte header = 0xD1;
        byte tnf = 0x01;
        byte payloadSize;
        byte payloadType;
        byte payload[140];
        byte end = 0xFE;
    } NdefMessage;

    enum ReturnCode {
        SUCCESS = 0,
        FAILURE = 1,
        TAG_NOT_PRESENT = 2,
        TAG_NOT_MATCH = 3,
        TAG_AUTH_ERROR = 4,
        NOT_IMPLEMENTED = 5,
    };

    enum NDEF_Payload_Type { NDEF_TEXT = 0x54, NDEF_URI = 0x55 };

    /** Forced / preferred HF family for hybrid tags (read_as). */
    enum ReadAsType {
        READ_AS_AUTO = 0,
        READ_AS_CLASSIC = 1,
        READ_AS_UL_NTAG = 2,
        READ_AS_ISO15693 = 3,
        READ_AS_FELICA = 4,
        READ_AS_ISODEP = 5,
    };

    enum MagicType {
        MAGIC_NONE = 0,
        MAGIC_GEN1 = 1,
        MAGIC_GEN2 = 2,
        MAGIC_GEN4 = 3,
        MAGIC_UNKNOWN = 4,
    };

    ReadAsType preferredReadAs = READ_AS_AUTO;

    uint8_t keys[15][6] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
        {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},
        {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD},
        {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        {0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97},
        {0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F},
        {0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91},
        {0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6},
        {0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9},
        {0xA6, 0x45, 0x98, 0xA7, 0x74, 0x78},
        {0x26, 0x94, 0x0B, 0x21, 0xFF, 0x5D},
        {0xFC, 0x00, 0x01, 0x87, 0x78, 0xF7},
        {0x00, 0x00, 0x0F, 0xFE, 0x24, 0x88}
    };

    Uid uid;
    PrintableUID printableUID;
    NdefMessage ndefMessage;
    std::vector<uint8_t> rawNdefRecord;
    String strAllPages = "";
    // Optional forced emulation mode set by the CLI (e.g. "t4t", "felica").
    // Empty = auto-detect from the loaded tag. Honored by drivers that support it.
    String emuMode = "";
    int totalPages = 0;
    int dataPages = 0;
    bool pageReadSuccess = false;
    int pageReadStatus = FAILURE;

    virtual ~RFIDInterface() {} // Virtual destructor

    /////////////////////////////////////////////////////////////////////////////////////
    // Life Cycle
    /////////////////////////////////////////////////////////////////////////////////////
    virtual bool begin() = 0;

    /////////////////////////////////////////////////////////////////////////////////////
    // Operations
    /////////////////////////////////////////////////////////////////////////////////////
    virtual int read(int cardBaudRate = 0) = 0; // cardBaudRate = 0 means MIFARE, 1 means FeliCa
    virtual int clone() = 0;
    virtual int erase() = 0;
    virtual int write(int cardBaudRate = 0) = 0;
    virtual int write_ndef() = 0;
    virtual int emulate() { return NOT_IMPLEMENTED; }
    // Optional NFC Tools ops — default NOT_IMPLEMENTED; refuse on screen in UI.
    virtual int lock_ntag() { return NOT_IMPLEMENTED; }
    virtual int set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2]) {
        (void)pwd;
        (void)pack;
        return NOT_IMPLEMENTED;
    }
    virtual int clear_ntag_password() { return NOT_IMPLEMENTED; }
    virtual int format_memory() { return erase(); }
    virtual int transceive_apdu(const uint8_t *tx, uint16_t txLen, uint8_t *rx, uint16_t rxCap,
                                uint16_t *rxLen) {
        (void)tx;
        (void)txLen;
        (void)rx;
        (void)rxCap;
        (void)rxLen;
        return NOT_IMPLEMENTED;
    }
    virtual int load() = 0;
    // Load + parse a dump file by path (shared by the GUI and the serial CLI so
    // their behavior never diverges). The base class provides a generic Bruce
    // .rfid parser; drivers may override it for richer handling (e.g. MIFARE
    // Classic blocks/keys, NTAG version/signature/counters in ST25R3916).
    virtual int loadFromFile(const String &filepath);
    virtual int save(const String &filename) = 0;
    virtual int saveFlipper(const String &filename) { return NOT_IMPLEMENTED; }

    // --- NFC toolkit expansion (defaults NOT_IMPLEMENTED; refuse in UI) ---
    virtual int read_as(ReadAsType type) {
        preferredReadAs = type;
        return read(type == READ_AS_FELICA ? 1 : 0);
    }
    /** Emulate loaded Classic dump and harvest Crypto1 nonces into filepath. */
    virtual int harvest_nonces(const String &filepath, uint32_t timeoutMs = 30000) {
        (void)filepath;
        (void)timeoutMs;
        return NOT_IMPLEMENTED;
    }
    virtual MagicType magic_probe() { return MAGIC_NONE; }
    virtual int magic_write_uid() { return NOT_IMPLEMENTED; }
    virtual int magic_write_dump() { return NOT_IMPLEMENTED; }
    virtual int iso15693_inventory(String &uidOut) {
        (void)uidOut;
        return NOT_IMPLEMENTED;
    }
    virtual int iso15693_read_block(uint8_t block, uint8_t *data, uint8_t dataLen) {
        (void)block;
        (void)data;
        (void)dataLen;
        return NOT_IMPLEMENTED;
    }
    virtual int iso15693_write_block(uint8_t block, const uint8_t *data, uint8_t dataLen) {
        (void)block;
        (void)data;
        (void)dataLen;
        return NOT_IMPLEMENTED;
    }
    /** Append human-readable DESFire app/file listing into strAllPages. */
    virtual int desfire_list() { return NOT_IMPLEMENTED; }
    virtual int ulc_auth(const uint8_t key[16]) {
        (void)key;
        return NOT_IMPLEMENTED;
    }
    virtual int felica_services() { return NOT_IMPLEMENTED; }
    /** One presence poll; SUCCESS if a tag is in field. */
    virtual int field_poll() { return read(0) == SUCCESS ? SUCCESS : TAG_NOT_PRESENT; }
    /** Dict-attack style Classic read with progress callback (sector, ok). */
    virtual int dict_read_classic(bool knownKeysOnly) {
        (void)knownKeysOnly;
        return read(0);
    }

    // One-line warning shown before emulation starts if the driver can't
    // fully reproduce the loaded tag. Empty = nothing to warn about.
    virtual String emulationCaveat() const { return ""; }

    // Build `ndefMessage` from a type ("url"/"text") and value. Shared by the
    // serial `rfid ndef` and `rfid emulate t4t` paths so the encoding is
    // identical regardless of entry point.
    void buildNdefMessage(const String &type, const String &value);

    // Build `rawNdefRecord` as an NFC Forum Wi-Fi Simple Config MIME record
    // ("application/vnd.wfa.wsc") that phones recognize for the "connect to
    // this network" prompt
    void buildWifiNdef(const String &ssid, const String &password);

    String statusMessage(int status) const {
        switch (status) {
            case SUCCESS: return String(F("Success"));
            case FAILURE: return String(F("Failed reading data blocks"));
            case TAG_NOT_PRESENT: return String(F("Failed reading. Tag not found"));
            case TAG_NOT_MATCH: return String(F("Error! Tags don't match"));
            case TAG_AUTH_ERROR: return String(F("Failed authenticating"));
            case NOT_IMPLEMENTED: return String(F("Not implemented"));
            default: return String();
        }
    }
};

#endif
