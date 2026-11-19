#include "nfc_session.h"

#include "pn532_bus.h"
#include "st25r3916.h"

namespace platform {

bool NfcSession::open(std::string& error) {
    close();
    const auto pref = tools::nfc_module_pref();
    auto try_st25 = [&]() -> bool {
        auto* s = new St25Session();
        std::string e;
        if (s->open({}, e)) {
            st_ = s;
            chip_ = NfcChip::St25;
            return true;
        }
        delete s;
        error = e;
        return false;
    };
    auto try_pn = [&]() -> bool {
        auto* s = new Pn532Session();
        std::string e;
        if (s->open(-1, e)) {
            pn_ = s;
            chip_ = NfcChip::Pn532;
            return true;
        }
        delete s;
        error = e;
        return false;
    };

    if (pref == tools::NfcModulePref::St25) return try_st25();
    if (pref == tools::NfcModulePref::Pn532) return try_pn();
    if (try_st25()) return true;
    if (try_pn()) return true;
    if (error.empty()) error = "Cap not present";
    return false;
}

void NfcSession::close() {
    if (st_) {
        static_cast<St25Session*>(st_)->close();
        delete static_cast<St25Session*>(st_);
        st_ = nullptr;
    }
    if (pn_) {
        static_cast<Pn532Session*>(pn_)->close();
        delete static_cast<Pn532Session*>(pn_);
        pn_ = nullptr;
    }
    chip_ = NfcChip::None;
}

bool NfcSession::ok() const { return chip_ != NfcChip::None; }

const char* NfcSession::chip_name() const {
    switch (chip_) {
        case NfcChip::St25: return "ST25R3916";
        case NfcChip::Pn532: return "PN532";
        default: return "none";
    }
}

bool NfcSession::read_dump(tools::NfcDump& out, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->read_dump(out, error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->read_dump(out, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::read_uid(std::string& uid_hex, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->read_uid(uid_hex, error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->read_uid(uid_hex, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->field_poll(present, uid_hex, error, cancel);
    if (chip_ == NfcChip::Pn532)
        return static_cast<Pn532Session*>(pn_)->field_poll(present, uid_hex, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::erase(std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->erase_ultralight(error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->erase_ultralight(error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->write_ndef_tlv(tlv, error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->write_ndef_tlv(tlv, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::lock_ntag(std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->lock_ntag(error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->lock_ntag(error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                                   bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->set_ntag_password(pwd, pack, error, cancel);
    if (chip_ == NfcChip::Pn532)
        return static_cast<Pn532Session*>(pn_)->set_ntag_password(pwd, pack, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::clear_ntag_password(std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25) return static_cast<St25Session*>(st_)->clear_ntag_password(error, cancel);
    if (chip_ == NfcChip::Pn532) return static_cast<Pn532Session*>(pn_)->clear_ntag_password(error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::transceive_apdu(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx,
                                 std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->transceive_apdu(tx, rx, error, cancel);
    if (chip_ == NfcChip::Pn532)
        return static_cast<Pn532Session*>(pn_)->transceive_apdu(tx, rx, error, cancel);
    error = "no NFC module";
    return false;
}

bool NfcSession::emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel,
                                  int timeout_ms) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->emulate_ndef_tlv(tlv, error, cancel, timeout_ms);
    if (chip_ == NfcChip::Pn532)
        return static_cast<Pn532Session*>(pn_)->emulate_ndef_tlv(tlv, error, cancel, timeout_ms);
    error = "no NFC module";
    return false;
}

bool NfcSession::read_felica(tools::NfcDump& out, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::Pn532)
        return static_cast<Pn532Session*>(pn_)->read_felica(out, error, cancel);
    if (chip_ == NfcChip::St25) {
        // Cap ST25 FeliCa needs RFAL tech F; fall back to normal dump note.
        if (!read_dump(out, error, cancel)) return false;
        error = "FeliCa dedicated poll needs RFAL on Cap ST25; showed ISO14443 dump if any.";
        return true;
    }
    error = "no NFC module";
    return false;
}

bool NfcSession::iso15693_inventory(std::string& uid_hex, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->iso15693_inventory(uid_hex, error, cancel);
    error = "PN532 has no ISO15693 / NFC-V.";
    return false;
}

bool NfcSession::magic_probe(std::string& result, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->magic_probe(result, error, cancel);
    error = "Magic write not on this module. Use Cap ST25.";
    return false;
}

bool NfcSession::magic_clone_uid(const std::string& uid_hex, std::string& error, bool* cancel) {
    if (chip_ == NfcChip::St25)
        return static_cast<St25Session*>(st_)->magic_clone_uid(uid_hex, error, cancel);
    error = "Magic write not on this module. Use Cap ST25.";
    return false;
}

} // namespace platform
