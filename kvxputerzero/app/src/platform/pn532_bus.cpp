#include "pn532_bus.h"

#include "cap_detect.h"
#include "nfc_ndef.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sstream>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr uint8_t kAddr = 0x24;

const uint8_t kDefaultKeys[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

std::string hex_bytes(const uint8_t* p, size_t n, char sep = 0) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02X", p[i]);
        if (sep && i) out.push_back(sep);
        out += b;
    }
    return out;
}

} // namespace

struct Pn532Session::Impl {
    int fd{-1};
    std::string path;
    uint8_t uid[10]{};
    uint8_t uid_len{0};
    uint8_t atqa[2]{};
    uint8_t sak{0};
    uint8_t tg{1};

    bool write_frame(const uint8_t* data, size_t len, std::string& error) {
        std::vector<uint8_t> frame;
        frame.push_back(0x00);
        frame.push_back(0x00);
        frame.push_back(0xFF);
        frame.push_back(static_cast<uint8_t>(len + 1));
        frame.push_back(static_cast<uint8_t>(~frame[3] + 1));
        frame.push_back(0xD4);
        frame.insert(frame.end(), data, data + len);
        uint8_t sum = 0xD4;
        for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
        frame.push_back(static_cast<uint8_t>(~sum + 1));
        frame.push_back(0x00);
        if (::write(fd, frame.data(), frame.size()) < 0) {
            error = "PN532 write failed";
            return false;
        }
        return true;
    }

    bool wait_ready(int timeout_ms, std::string& error, bool* cancel) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            uint8_t b = 0;
            if (::read(fd, &b, 1) == 1 && (b & 1)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        error = "PN532 timeout";
        return false;
    }

    bool read_ack(std::string& error, bool* cancel, int timeout_ms = 200) {
        if (!wait_ready(timeout_ms, error, cancel)) return false;
        uint8_t ack[8] = {};
        (void)::read(fd, ack, sizeof(ack));
        return true;
    }

    bool read_response(std::vector<uint8_t>& payload, std::string& error, bool* cancel,
                       int timeout_ms = 1000) {
        payload.clear();
        if (!wait_ready(timeout_ms, error, cancel)) return false;
        uint8_t buf[320] = {0};
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n < 6) {
            error = "PN532 short reply";
            return false;
        }
        // Prefer framed parse: … 00 00 FF LEN LCS D5 CMD …
        for (ssize_t i = 0; i + 5 < n; ++i) {
            if (buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0xFF) {
                const uint8_t len = buf[i + 3];
                if (len < 2) continue;
                const ssize_t d5 = i + 5;
                if (d5 >= n || buf[d5] != 0xD5) continue;
                const size_t body = static_cast<size_t>(len); // includes D5? No: LEN = TFI+data = 1+payload
                // LEN counts bytes from TFI (0xD5) through last data byte (excl. DCS).
                if (d5 + static_cast<ssize_t>(len) > n) continue;
                payload.assign(buf + d5 + 1, buf + d5 + static_cast<ssize_t>(len));
                return true;
            }
        }
        for (ssize_t i = 0; i + 2 < n; ++i) {
            if (buf[i] == 0xD5) {
                payload.assign(buf + i + 1, buf + n);
                return true;
            }
        }
        error = "PN532 bad reply";
        return false;
    }

    bool cmd(const uint8_t* data, size_t len, std::vector<uint8_t>& resp, std::string& error,
             bool* cancel, int timeout_ms = 1000) {
        if (!write_frame(data, len, error)) return false;
        if (!read_ack(error, cancel, 200)) {
            // Some firmwares ACK slowly; still try the response.
            error.clear();
        }
        return read_response(resp, error, cancel, timeout_ms);
    }

    bool in_release(std::string& error, bool* cancel) {
        const uint8_t c[] = {0x52, 0x00};
        std::vector<uint8_t> resp;
        return cmd(c, sizeof(c), resp, error, cancel, 500);
    }

    bool set_parameters_picc(std::string& error, bool* cancel) {
        // fISO14443-4_PICC (bit 5): chip auto-answers RATS with ATS.
        const uint8_t c[] = {0x12, 0x20};
        std::vector<uint8_t> resp;
        return cmd(c, sizeof(c), resp, error, cancel, 500);
    }

    bool tg_init_as_target(std::string& error, bool* cancel, int timeout_ms) {
        // MODE 0x04 = PICCOnly (T4T). Do NOT use 0x05 (PICC+Passive) — hangs I2C.
        // Framing matches ADV tgInitAsTargetIrq (incl. historical bytes).
        const uint8_t cmd[] = {
            0x8C, 0x04,
            0x04, 0x00,             // SENS_RES (ATQA)
            0x12, 0x34, 0x56,       // NFCID1t (trailing 3 of 4-byte UID)
            0x20,                   // SEL_RES ISO14443-4
            0x01, 0xFE, 0x0F, 0xBB, 0xBA, 0xA6, 0xC0, 0x00, // NFCID2t
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // PAD
            0xFF, 0xFF,             // system code
            0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // NFCID3t
            0x00,                   // GeneralBytes length = 0 (no NFC-DEP)
            0x0E,                   // HistoricalBytes length
            0x42, 0x52, 0x55, 0x43, 0x45, 0x20, 0x46, 0x49, 0x52, 0x4D, 0x57, 0x41, 0x52, 0x45,
        };
        if (!write_frame(cmd, sizeof(cmd), error)) return false;
        if (!read_ack(error, cancel, 500)) {
            // Chip may still be waiting; release before retry.
            std::string e2;
            (void)in_release(e2, nullptr);
            return false;
        }
        std::vector<uint8_t> resp;
        if (!read_response(resp, error, cancel, timeout_ms)) {
            // Timed out waiting for a reader — abort the pending target session.
            std::string e2;
            (void)in_release(e2, nullptr);
            if (error != "cancelled") error = "waiting for reader";
            return false;
        }
        // resp[0] = 0x8D, resp[1] = mode/status
        if (resp.size() < 2 || resp[0] != 0x8D) {
            std::string e2;
            (void)in_release(e2, nullptr);
            error = "TgInitAsTarget bad reply";
            return false;
        }
        const uint8_t st = resp[1];
        // 0x04 is known-bad (self-releases); 0x08 = 106kbps PICC activation.
        if (st == 0x04) {
            std::string e2;
            (void)in_release(e2, nullptr);
            error = "TgInitAsTarget status 0x04";
            return false;
        }
        return st == 0x00 || st == 0x08 || st == 0x15;
    }

    bool tg_get_data(uint8_t* out, uint8_t max_len, uint8_t& out_len, uint8_t& status,
                     std::string& error, bool* cancel) {
        out_len = 0;
        status = 0xFF;
        const uint8_t c = 0x86;
        if (!write_frame(&c, 1, error)) return false;
        if (!read_ack(error, cancel, 200)) return false;
        std::vector<uint8_t> resp;
        if (!read_response(resp, error, cancel, 1000)) return false;
        if (resp.size() < 2 || resp[0] != 0x87) return false;
        status = resp[1];
        if (resp.size() > 2) {
            out_len = static_cast<uint8_t>(std::min<size_t>(resp.size() - 2, max_len));
            std::memcpy(out, resp.data() + 2, out_len);
        }
        return true;
    }

    bool tg_set_data(const uint8_t* data, uint8_t len, std::string& error, bool* cancel) {
        if (len == 0 || len > 254) {
            error = "TgSetData bad length";
            return false;
        }
        std::vector<uint8_t> c;
        c.push_back(0x8E);
        c.insert(c.end(), data, data + len);
        std::vector<uint8_t> resp;
        if (!cmd(c.data(), c.size(), resp, error, cancel, 1000)) return false;
        return resp.size() >= 1 && resp[0] == 0x8F;
    }

    bool list_passive_a(std::string& error, bool* cancel, int timeout_ms) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            const uint8_t c[] = {0x4A, 0x01, 0x00};
            std::vector<uint8_t> resp;
            if (!cmd(c, sizeof(c), resp, error, cancel, 400)) {
                if (error == "cancelled") return false;
                error.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }
            // resp[0]=0x4B NbTg ...
            if (resp.size() < 9 || resp[0] != 0x4B || resp[1] < 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                continue;
            }
            tg = resp[2];
            atqa[0] = resp[3];
            atqa[1] = resp[4];
            sak = resp[5];
            uid_len = resp[6];
            if (uid_len == 0 || uid_len > 10 || resp.size() < 7u + uid_len) continue;
            std::memcpy(uid, &resp[7], uid_len);
            return true;
        }
        error = "no tag";
        return false;
    }

    bool data_exchange(const uint8_t* tx, size_t tx_len, std::vector<uint8_t>& rx, std::string& error,
                       bool* cancel) {
        std::vector<uint8_t> c;
        c.push_back(0x40); // InDataExchange
        c.push_back(tg);
        c.insert(c.end(), tx, tx + tx_len);
        std::vector<uint8_t> resp;
        if (!cmd(c.data(), c.size(), resp, error, cancel, 1500)) return false;
        if (resp.size() < 2 || resp[0] != 0x41) {
            error = "InDataExchange failed";
            return false;
        }
        if (resp[1] != 0x00) {
            error = "tag status " + std::to_string(resp[1]);
            return false;
        }
        rx.assign(resp.begin() + 2, resp.end());
        return true;
    }

    bool mifare_auth(uint8_t block, bool key_b, const uint8_t key[6], std::string& error, bool* cancel) {
        std::vector<uint8_t> c;
        c.push_back(0x40);
        c.push_back(tg);
        c.push_back(key_b ? 0x61 : 0x60);
        c.push_back(block);
        c.insert(c.end(), key, key + 6);
        c.insert(c.end(), uid, uid + uid_len);
        std::vector<uint8_t> resp;
        if (!cmd(c.data(), c.size(), resp, error, cancel, 1500)) return false;
        return resp.size() >= 2 && resp[0] == 0x41 && resp[1] == 0x00;
    }

    bool mifare_read16(uint8_t block, uint8_t out[16], std::string& error, bool* cancel) {
        const uint8_t tx[] = {0x30, block};
        std::vector<uint8_t> rx;
        if (!data_exchange(tx, 2, rx, error, cancel)) return false;
        if (rx.size() < 16) {
            error = "short block";
            return false;
        }
        std::memcpy(out, rx.data(), 16);
        return true;
    }

    bool ul_read4(uint8_t page, uint8_t out[16], std::string& error, bool* cancel) {
        const uint8_t tx[] = {0x30, page}; // READ returns 16 bytes from page
        std::vector<uint8_t> rx;
        if (!data_exchange(tx, 2, rx, error, cancel)) return false;
        if (rx.size() < 16) {
            error = "short page";
            return false;
        }
        std::memcpy(out, rx.data(), 16);
        return true;
    }

    bool ul_write4(uint8_t page, const uint8_t data[4], std::string& error, bool* cancel) {
        uint8_t tx[6] = {0xA2, page, data[0], data[1], data[2], data[3]};
        std::vector<uint8_t> rx;
        return data_exchange(tx, 6, rx, error, cancel);
    }
};

bool Pn532Session::open(int i2c_bus, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    const auto info = detect_pn532(i2c_bus);
    if (!info.present) {
        error = info.error.empty() ? "Cap not present" : info.error;
        return false;
    }
    const int fd = ::open(info.path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "PN532 open failed";
        return false;
    }
    if (ioctl(fd, I2C_SLAVE, kAddr) < 0) {
        ::close(fd);
        error = "PN532 I2C_SLAVE failed";
        return false;
    }
    // SAMConfiguration (normal mode)
    impl_ = new Impl();
    impl_->fd = fd;
    impl_->path = info.path;
    const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01};
    std::vector<uint8_t> resp;
    std::string e;
    (void)impl_->cmd(sam, sizeof(sam), resp, e, nullptr, 500);
    return true;
}

void Pn532Session::close() {
    if (impl_) {
        if (impl_->fd >= 0) ::close(impl_->fd);
        delete impl_;
        impl_ = nullptr;
    }
}

bool Pn532Session::ok() const { return impl_ && impl_->fd >= 0; }

bool Pn532Session::get_firmware(std::string& out, std::string& error) {
    out.clear();
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    const uint8_t cmd[] = {0x02};
    std::vector<uint8_t> resp;
    if (!impl_->cmd(cmd, 1, resp, error, nullptr, 500)) return false;
    if (resp.size() >= 5 && resp[0] == 0x03) {
        char line[64];
        std::snprintf(line, sizeof(line), "IC=0x%02X ver=%u.%u", resp[1], resp[2], resp[3]);
        out = line;
        return true;
    }
    out = "firmware reply ok";
    return true;
}

bool Pn532Session::read_uid(std::string& uid_hex, std::string& error, bool* cancel, int timeout_ms) {
    uid_hex.clear();
    tools::NfcDump d;
    if (!read_dump(d, error, cancel, timeout_ms)) return false;
    uid_hex = d.uid_hex;
    return true;
}

bool Pn532Session::save_dump(const std::string& path, const std::string& uid_hex, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "write failed";
        return false;
    }
    out << "Filetype: kvxputerzero NFC dump\nUID: " << uid_hex << "\n";
    return true;
}

bool Pn532Session::read_dump(tools::NfcDump& out, std::string& error, bool* cancel, int timeout_ms) {
    out = {};
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    if (!impl_->list_passive_a(error, cancel, timeout_ms)) return false;
    out.uid_hex = hex_bytes(impl_->uid, impl_->uid_len);
    out.atqa_hex = hex_bytes(impl_->atqa, 2, ' ');
    out.sak_hex = hex_bytes(&impl_->sak, 1);
    out.device_type = "ISO14443-3A";
    std::ostringstream pages;

    if (impl_->sak == 0x00) {
        // Ultralight / NTAG
        uint8_t buf[16];
        int total = 64;
        if (impl_->ul_read4(3, buf, error, cancel)) {
            if (buf[2] == 0x12) total = 45;
            else if (buf[2] == 0x3E) total = 135;
            else if (buf[2] == 0x6D) total = 231;
        }
        error.clear();
        out.device_type = (total == 45) ? "NTAG213" : (total == 135) ? "NTAG215" : (total == 231) ? "NTAG216"
                                                                                                  : "MIFARE Ultralight";
        out.pages_total = total;
        for (int page = 0; page < total; page += 4) {
            if (cancel && *cancel) {
                error = "cancelled";
                return false;
            }
            if (!impl_->ul_read4(static_cast<uint8_t>(page), buf, error, cancel)) {
                out.page_read_ok = false;
                break;
            }
            for (int o = 0; o < 4 && page + o < total; ++o) {
                char line[48];
                std::snprintf(line, sizeof(line), "Page %d: %02X %02X %02X %02X\n", page + o,
                              buf[o * 4], buf[o * 4 + 1], buf[o * 4 + 2], buf[o * 4 + 3]);
                pages << line;
                ++out.pages_read;
            }
        }
    } else if (impl_->sak == 0x08 || impl_->sak == 0x09 || impl_->sak == 0x18) {
        out.device_type = (impl_->sak == 0x18) ? "Mifare Classic 4K"
                        : (impl_->sak == 0x09) ? "Mifare Classic Mini"
                                               : "Mifare Classic 1K";
        const int sectors = (impl_->sak == 0x18) ? 40 : (impl_->sak == 0x09) ? 5 : 16;
        out.pages_total = (impl_->sak == 0x18) ? 256 : (impl_->sak == 0x09) ? 20 : 64;
        for (int s = 0; s < sectors; ++s) {
            const int first = (s < 32) ? s * 4 : 128 + (s - 32) * 16;
            const int nblocks = (s < 32) ? 4 : 16;
            bool authed = false;
            for (const auto& key : kDefaultKeys) {
                if (impl_->mifare_auth(static_cast<uint8_t>(first), false, key, error, cancel)) {
                    authed = true;
                    break;
                }
                error.clear();
                if (!impl_->list_passive_a(error, cancel, 2000)) return false;
            }
            if (!authed) {
                out.page_read_ok = false;
                break;
            }
            for (int b = 0; b < nblocks; ++b) {
                uint8_t data[16];
                if (!impl_->mifare_read16(static_cast<uint8_t>(first + b), data, error, cancel)) {
                    out.page_read_ok = false;
                    break;
                }
                char line[80];
                std::snprintf(line, sizeof(line),
                              "Page %d: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                              "%02X %02X %02X %02X\n",
                              out.pages_read, data[0], data[1], data[2], data[3], data[4], data[5],
                              data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13],
                              data[14], data[15]);
                pages << line;
                ++out.pages_read;
            }
            if (!out.page_read_ok) break;
        }
    } else if (impl_->sak & 0x20) {
        out.device_type = "ISO14443-4A";
        // SELECT NDEF App
        static const uint8_t sel[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};
        std::vector<uint8_t> rx;
        if (impl_->data_exchange(sel, sizeof(sel), rx, error, cancel)) {
            out.ndef_text = "ISO-DEP present; APDU OK (SELECT NDEF App SW in reply)";
            out.pages_text = "NDEF T4T\n";
            out.pages_read = 1;
            out.pages_total = 1;
        } else {
            error.clear();
            out.ndef_text = "ISO-DEP tag (no NDEF app)";
        }
    }
    out.pages_text = pages.str();
    error.clear();
    return true;
}

bool Pn532Session::field_poll(bool& present, std::string& uid_hex, std::string& error, bool* cancel) {
    present = false;
    uid_hex.clear();
    tools::NfcDump d;
    if (read_dump(d, error, cancel, 1500)) {
        present = true;
        uid_hex = d.uid_hex;
        return true;
    }
    if (error == "cancelled") return false;
    error.clear();
    present = false;
    return true;
}

bool Pn532Session::erase_ultralight(std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel, 8000)) return false;
    if (impl_->sak != 0x00) {
        error = "Erase here targets NTAG/Ultralight";
        return false;
    }
    const uint8_t zero[4] = {0, 0, 0, 0};
    for (int page = 4; page < d.pages_total - 5; ++page) {
        if (!impl_->ul_write4(static_cast<uint8_t>(page), zero, error, cancel)) return false;
    }
    return true;
}

bool Pn532Session::write_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel, 8000)) return false;
    if (impl_->sak != 0x00) {
        error = "Tag is not MIFARE Ultralight.";
        return false;
    }
    size_t off = 0;
    for (int page = 4; off < tlv.size(); ++page) {
        uint8_t block[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4 && off < tlv.size(); ++i, ++off) block[i] = tlv[off];
        if (!impl_->ul_write4(static_cast<uint8_t>(page), block, error, cancel)) return false;
    }
    return true;
}

bool Pn532Session::lock_ntag(std::string& error, bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel, 8000)) return false;
    if (impl_->sak != 0x00) {
        error = "Not an NTAG/Ultralight tag.";
        return false;
    }
    // Static lock bytes page 2 — irreversible; match ADV intent
    uint8_t page2[16];
    if (!impl_->ul_read4(0, page2, error, cancel)) return false;
    uint8_t lock[4] = {page2[8], page2[9], 0xFF, 0xFF};
    return impl_->ul_write4(2, lock, error, cancel);
}

bool Pn532Session::set_ntag_password(const uint8_t pwd[4], const uint8_t pack[2], std::string& error,
                                     bool* cancel) {
    tools::NfcDump d;
    if (!read_dump(d, error, cancel, 8000)) return false;
    if (impl_->sak != 0x00) {
        error = "Not supported here.";
        return false;
    }
    const int pwd_page = (d.pages_total >= 45) ? d.pages_total - 2 : 0x29;
    const int pack_page = pwd_page + 1;
    if (!impl_->ul_write4(static_cast<uint8_t>(pwd_page), pwd, error, cancel)) return false;
    uint8_t pk[4] = {pack[0], pack[1], 0, 0};
    return impl_->ul_write4(static_cast<uint8_t>(pack_page), pk, error, cancel);
}

bool Pn532Session::clear_ntag_password(std::string& error, bool* cancel) {
    const uint8_t pwd[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t pack[2] = {0, 0};
    return set_ntag_password(pwd, pack, error, cancel);
}

bool Pn532Session::transceive_apdu(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx,
                                   std::string& error, bool* cancel) {
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    if (!impl_->list_passive_a(error, cancel, 8000)) return false;
    if (!(impl_->sak & 0x20)) {
        error = "Need ISO-DEP tag";
        return false;
    }
    return impl_->data_exchange(tx.data(), tx.size(), rx, error, cancel);
}

bool Pn532Session::emulate_ndef_tlv(const std::vector<uint8_t>& tlv, std::string& error, bool* cancel,
                                    int timeout_ms) {
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }

    static constexpr uint8_t kInsSelect = 0xA4;
    static constexpr uint8_t kInsRead = 0xB0;
    static constexpr uint8_t kMaxRsp = 252;
    static const uint8_t kCcFile[] = {0x00, 0x0F, 0x20, 0x00, 0x3B, 0x00, 0x34, 0x04, 0x06,
                                      0xE1, 0x04, 0x00, 0x80, 0x00, 0xFF};
    static const uint8_t kAid[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

    auto ndef_file = tools::ndef_t4t_file_from_tlv(tlv);
    if (ndef_file.empty()) {
        // Fallback URI so emulate still arms even with empty/malformed TLV.
        ndef_file = tools::ndef_t4t_file_from_tlv(tools::ndef_tlv_uri("https://github.com/kvx17/kvxputer"));
    }
    if (ndef_file.empty()) {
        error = "NDEF file build failed";
        return false;
    }

    // Refresh SAM + enable ISO14443-4 PICC auto-ATS before arming.
    {
        const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01};
        std::vector<uint8_t> resp;
        std::string e;
        (void)impl_->cmd(sam, sizeof(sam), resp, e, cancel, 500);
        e.clear();
        if (!impl_->set_parameters_picc(e, cancel)) {
            // Non-fatal on some firmwares; continue.
            e.clear();
        }
    }

    enum class TagFile { None, Cc, Ndef };
    TagFile current = TagFile::None;
    bool armed = false;
    bool had_hit = false;
    auto next_arm = std::chrono::steady_clock::now();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            (void)impl_->in_release(error, cancel);
            return had_hit;
        }

        if (!armed && std::chrono::steady_clock::now() >= next_arm) {
            current = TagFile::None;
            std::string e;
            // 5s arm window — TgInitAsTarget only returns once a reader activates.
            armed = impl_->tg_init_as_target(e, cancel, 5000);
            next_arm = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
            if (!armed) {
                if (e == "cancelled") {
                    error = e;
                    return had_hit;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
        }
        if (!armed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        uint8_t req[255] = {};
        uint8_t req_len = 0;
        uint8_t tg_status = 0xFF;
        std::string e;
        bool got = impl_->tg_get_data(req, sizeof(req), req_len, tg_status, e, cancel);
        for (int retry = 0; !got && retry < 3; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            e.clear();
            got = impl_->tg_get_data(req, sizeof(req), req_len, tg_status, e, cancel);
        }
        if (!got) {
            if (e == "cancelled") {
                error = e;
                return had_hit;
            }
            (void)impl_->in_release(e, cancel);
            armed = false;
            continue;
        }
        if (tg_status == 0x29) {
            (void)impl_->in_release(e, cancel);
            armed = false;
            continue;
        }
        // 0x25 = idle / not yet selected — keep waiting, do not release.
        if (tg_status != 0x00 || req_len < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const uint8_t ins = req[1];
        const uint8_t p1 = req[2];
        const uint8_t p2 = req[3];
        const uint8_t lc = req[4];
        const uint16_t p1p2 = static_cast<uint16_t>((p1 << 8) | p2);
        std::vector<uint8_t> response;

        if (ins == kInsSelect) {
            if (p1 == 0x00) { // select by file id
                if (p2 != 0x0C) {
                    response = {0x90, 0x00};
                } else if (lc == 2 && req_len >= 7 && req[5] == 0xE1 &&
                           (req[6] == 0x03 || req[6] == 0x04)) {
                    current = (req[6] == 0x03) ? TagFile::Cc : TagFile::Ndef;
                    response = {0x90, 0x00};
                } else {
                    response = {0x6A, 0x82};
                }
            } else if (p1 == 0x04) { // select by name (AID)
                if (req_len >= 12 && lc >= 7 && std::memcmp(req + 5, kAid, 7) == 0) {
                    response = {0x90, 0x00};
                } else {
                    response = {0x6A, 0x81};
                }
            } else {
                response = {0x6A, 0x81};
            }
        } else if (ins == kInsRead) {
            if (current == TagFile::None) {
                response = {0x6A, 0x82};
            } else if (p1p2 > ndef_file.size()) {
                response = {0x62, 0x82};
            } else {
                const size_t want = (lc == 0) ? kMaxRsp : std::min<size_t>(lc, kMaxRsp);
                if (current == TagFile::Cc) {
                    for (size_t i = 0; i < want; ++i) {
                        const size_t idx = static_cast<size_t>(p1p2) + i;
                        response.push_back(idx < sizeof(kCcFile) ? kCcFile[idx] : 0x00);
                    }
                } else {
                    for (size_t i = 0; i < want; ++i) {
                        const size_t idx = static_cast<size_t>(p1p2) + i;
                        response.push_back(idx < ndef_file.size() ? ndef_file[idx] : 0x00);
                    }
                }
                response.push_back(0x90);
                response.push_back(0x00);
            }
        } else {
            response = {0x6A, 0x81};
        }

        had_hit = true;
        if (response.empty() || response.size() > 254) {
            (void)impl_->in_release(e, cancel);
            armed = false;
            continue;
        }
        e.clear();
        if (!impl_->tg_set_data(response.data(), static_cast<uint8_t>(response.size()), e, cancel)) {
            (void)impl_->in_release(e, cancel);
            armed = false;
        }
    }

    {
        std::string e;
        (void)impl_->in_release(e, nullptr);
    }
    if (had_hit) {
        error.clear();
        return true;
    }
    error = "listen timed out (no reader)";
    return false;
}

bool Pn532Session::read_felica(tools::NfcDump& out, std::string& error, bool* cancel, int timeout_ms) {
    out = {};
    if (!ok()) {
        error = "PN532 not open";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel && *cancel) {
            error = "cancelled";
            return false;
        }
        // InListPassiveTarget 212kbps FeliCa
        const uint8_t c[] = {0x4A, 0x01, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x00};
        std::vector<uint8_t> resp;
        if (!impl_->cmd(c, sizeof(c), resp, error, cancel, 500)) {
            if (error == "cancelled") return false;
            error.clear();
            continue;
        }
        if (resp.size() < 20 || resp[0] != 0x4B || resp[1] < 1) continue;
        out.device_type = "FeliCa";
        out.uid_hex = hex_bytes(&resp[4], 8); // IDm
        out.sak_hex = hex_bytes(&resp[12], 8); // PMm as manufacture
        out.pages_text = "IDm: " + tools::nfc_uid_spaced(out.uid_hex) + "\n";
        out.pages_read = 1;
        out.pages_total = 1;
        return true;
    }
    error = "no FeliCa tag";
    return false;
}

} // namespace platform
