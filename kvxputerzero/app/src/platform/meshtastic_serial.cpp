#include "meshtastic_serial.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <random>
#include <thread>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr uint8_t kStart1 = 0x94;
constexpr uint8_t kStart2 = 0xC3;
constexpr size_t kMaxFramePb = 512;
constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr int kPortnumText = 1;

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string config_root() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg && *xdg) return std::string(xdg) + "/kvxputerzero";
    }
    if (const char* home = std::getenv("HOME")) {
        if (home && *home) return std::string(home) + "/.config/kvxputerzero";
    }
    return "/tmp/kvxputerzero";
}

std::string read_lora_cfg(const char* name) {
    std::ifstream in(config_root() + "/lora/" + name);
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    return trim(line);
}

bool cancelled(std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}

void sleep_cancel(int ms, std::atomic<bool>* cancel) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cancelled(cancel)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool is_usb_serial_candidate(const std::string& path) {
    return path.find("/dev/ttyACM") == 0 || path.find("/dev/ttyUSB") == 0;
}

std::vector<std::string> list_usb_serials() {
    std::vector<std::string> out;
    DIR* d = ::opendir("/dev");
    if (!d) return out;
    while (auto* ent = ::readdir(d)) {
        const std::string name = ent->d_name;
        if (name.rfind("ttyACM", 0) == 0 || name.rfind("ttyUSB", 0) == 0) {
            out.push_back("/dev/" + name);
        }
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

// --- minimal protobuf helpers -------------------------------------------------------------

void pb_put_varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

void pb_put_tag(std::vector<uint8_t>& out, uint32_t field, uint8_t wire) {
    pb_put_varint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

void pb_put_bytes(std::vector<uint8_t>& out, uint32_t field, const uint8_t* data, size_t len) {
    pb_put_tag(out, field, 2);
    pb_put_varint(out, len);
    out.insert(out.end(), data, data + len);
}

void pb_put_string(std::vector<uint8_t>& out, uint32_t field, const std::string& s) {
    pb_put_bytes(out, field, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void pb_put_uint32(std::vector<uint8_t>& out, uint32_t field, uint32_t v) {
    pb_put_tag(out, field, 0);
    pb_put_varint(out, v);
}

void pb_put_fixed32(std::vector<uint8_t>& out, uint32_t field, uint32_t v) {
    pb_put_tag(out, field, 5);
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

bool pb_read_varint(const uint8_t* data, size_t len, size_t& i, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (i < len && shift < 64) {
        const uint8_t b = data[i++];
        out |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

bool pb_skip(const uint8_t* data, size_t len, size_t& i, uint8_t wire) {
    if (wire == 0) {
        uint64_t v = 0;
        return pb_read_varint(data, len, i, v);
    }
    if (wire == 1) {
        if (i + 8 > len) return false;
        i += 8;
        return true;
    }
    if (wire == 2) {
        uint64_t n = 0;
        if (!pb_read_varint(data, len, i, n)) return false;
        if (i + n > len) return false;
        i += static_cast<size_t>(n);
        return true;
    }
    if (wire == 5) {
        if (i + 4 > len) return false;
        i += 4;
        return true;
    }
    return false;
}

float pb_read_float(const uint8_t* p) {
    float f = 0;
    std::memcpy(&f, p, 4);
    return f;
}

uint32_t pb_read_fixed32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

struct ParsedUser {
    std::string short_name;
    std::string long_name;
};

ParsedUser parse_user(const uint8_t* data, size_t len) {
    ParsedUser u;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(data, len, i, tag)) break;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        const uint8_t wire = static_cast<uint8_t>(tag & 7);
        if (wire == 2 && (field == 2 || field == 3)) {
            uint64_t n = 0;
            if (!pb_read_varint(data, len, i, n) || i + n > len) break;
            std::string s(reinterpret_cast<const char*>(data + i), static_cast<size_t>(n));
            i += static_cast<size_t>(n);
            if (field == 2) u.long_name = s;
            else u.short_name = s;
        } else if (!pb_skip(data, len, i, wire)) {
            break;
        }
    }
    return u;
}

MeshNode parse_node_info(const uint8_t* data, size_t len) {
    MeshNode n;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(data, len, i, tag)) break;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        const uint8_t wire = static_cast<uint8_t>(tag & 7);
        if (field == 1 && wire == 0) {
            uint64_t v = 0;
            if (!pb_read_varint(data, len, i, v)) break;
            n.num = static_cast<uint32_t>(v);
        } else if (field == 2 && wire == 2) {
            uint64_t nlen = 0;
            if (!pb_read_varint(data, len, i, nlen) || i + nlen > len) break;
            const auto u = parse_user(data + i, static_cast<size_t>(nlen));
            i += static_cast<size_t>(nlen);
            n.short_name = u.short_name;
            n.long_name = u.long_name;
        } else if (field == 4 && wire == 5) {
            if (i + 4 > len) break;
            n.snr = pb_read_float(data + i);
            n.has_snr = true;
            i += 4;
        } else if (field == 5 && wire == 5) {
            if (i + 4 > len) break;
            n.last_heard = pb_read_fixed32(data + i);
            i += 4;
        } else if (!pb_skip(data, len, i, wire)) {
            break;
        }
    }
    return n;
}

struct ParsedData {
    int portnum{0};
    std::string payload;
};

ParsedData parse_data(const uint8_t* data, size_t len) {
    ParsedData d;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(data, len, i, tag)) break;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        const uint8_t wire = static_cast<uint8_t>(tag & 7);
        if (field == 1 && wire == 0) {
            uint64_t v = 0;
            if (!pb_read_varint(data, len, i, v)) break;
            d.portnum = static_cast<int>(v);
        } else if (field == 2 && wire == 2) {
            uint64_t n = 0;
            if (!pb_read_varint(data, len, i, n) || i + n > len) break;
            d.payload.assign(reinterpret_cast<const char*>(data + i), static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else if (!pb_skip(data, len, i, wire)) {
            break;
        }
    }
    return d;
}

struct ParsedPacket {
    uint32_t from{0};
    ParsedData data;
    float snr{0};
    bool has_snr{false};
    int rssi{0};
    bool has_rssi{false};
    bool has_decoded{false};
};

ParsedPacket parse_mesh_packet(const uint8_t* data, size_t len) {
    ParsedPacket p;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(data, len, i, tag)) break;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        const uint8_t wire = static_cast<uint8_t>(tag & 7);
        if (field == 1 && wire == 5) {
            if (i + 4 > len) break;
            p.from = pb_read_fixed32(data + i);
            i += 4;
        } else if (field == 4 && wire == 2) {
            uint64_t n = 0;
            if (!pb_read_varint(data, len, i, n) || i + n > len) break;
            p.data = parse_data(data + i, static_cast<size_t>(n));
            p.has_decoded = true;
            i += static_cast<size_t>(n);
        } else if (field == 8 && wire == 5) {
            if (i + 4 > len) break;
            p.snr = pb_read_float(data + i);
            p.has_snr = true;
            i += 4;
        } else if (field == 12 && wire == 0) {
            uint64_t v = 0;
            if (!pb_read_varint(data, len, i, v)) break;
            p.rssi = static_cast<int>(v);
            // Meshtastic often stores signed rssi in uint32 bit pattern; clamp display.
            if (p.rssi > 127) p.rssi = static_cast<int>(static_cast<int32_t>(v));
            p.has_rssi = true;
        } else if (!pb_skip(data, len, i, wire)) {
            break;
        }
    }
    return p;
}

enum class FromKind { Other, Packet, NodeInfo, ConfigComplete };

struct FromRadio {
    FromKind kind{FromKind::Other};
    ParsedPacket packet;
    MeshNode node;
    uint32_t config_complete_id{0};
};

FromRadio parse_from_radio(const uint8_t* data, size_t len) {
    FromRadio fr;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        if (!pb_read_varint(data, len, i, tag)) break;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        const uint8_t wire = static_cast<uint8_t>(tag & 7);
        if (field == 2 && wire == 2) {
            uint64_t n = 0;
            if (!pb_read_varint(data, len, i, n) || i + n > len) break;
            fr.kind = FromKind::Packet;
            fr.packet = parse_mesh_packet(data + i, static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else if (field == 4 && wire == 2) {
            uint64_t n = 0;
            if (!pb_read_varint(data, len, i, n) || i + n > len) break;
            fr.kind = FromKind::NodeInfo;
            fr.node = parse_node_info(data + i, static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else if (field == 7 && wire == 0) {
            uint64_t v = 0;
            if (!pb_read_varint(data, len, i, v)) break;
            fr.kind = FromKind::ConfigComplete;
            fr.config_complete_id = static_cast<uint32_t>(v);
        } else if (!pb_skip(data, len, i, wire)) {
            break;
        }
    }
    return fr;
}

std::vector<uint8_t> make_want_config(uint32_t nonce) {
    std::vector<uint8_t> pb;
    pb_put_uint32(pb, 3, nonce); // ToRadio.want_config_id = 3
    return pb;
}

std::vector<uint8_t> make_text_packet(const std::string& text) {
    std::vector<uint8_t> data;
    pb_put_uint32(data, 1, kPortnumText);
    pb_put_string(data, 2, text);

    std::vector<uint8_t> pkt;
    pb_put_fixed32(pkt, 2, kBroadcast); // to
    pb_put_bytes(pkt, 4, data.data(), data.size()); // decoded
    // random-ish non-zero id
    static std::mt19937 rng{std::random_device{}()};
    uint32_t id = rng();
    if (id == 0) id = 1;
    pb_put_fixed32(pkt, 6, id);
    pb_put_uint32(pkt, 9, 3); // hop_limit

    std::vector<uint8_t> to;
    pb_put_bytes(to, 1, pkt.data(), pkt.size()); // ToRadio.packet = 1
    return to;
}

std::string node_id_label(uint32_t num) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "!%08x", static_cast<unsigned>(num));
    return buf;
}

bool probe_phone_on_port(const std::string& path, int baud, std::atomic<bool>* cancel) {
    UartSession uart;
    std::string err;
    if (!uart.open(path, baud, err)) return false;
    // Wake framer with START1 bytes, then want_config_id=1
    const uint8_t wake[] = {kStart1, kStart1, kStart1, kStart1};
    uart.write_bytes(wake, sizeof(wake), err);
    const uint8_t frame[] = {kStart1, kStart2, 0x00, 0x02, 0x18, 0x01};
    if (!uart.write_bytes(frame, sizeof(frame), err)) {
        uart.close();
        return false;
    }
    std::string buf;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    while (std::chrono::steady_clock::now() < deadline && !cancelled(cancel)) {
        std::string chunk;
        if (!uart.read_available(chunk, err)) break;
        if (!chunk.empty()) {
            buf += chunk;
            if (buf.find(std::string("\x94\xc3", 2)) != std::string::npos) {
                uart.close();
                return true;
            }
        }
        sleep_cancel(20, cancel);
    }
    uart.close();
    return false;
}

} // namespace

MeshSerialInfo probe_meshtastic_serial(std::atomic<bool>* cancel) {
    MeshSerialInfo info;
    if (kDesktop) {
        info.error = "Cap / module not present";
        return info;
    }

    const std::string mode_cfg = read_lora_cfg("mesh_mode.txt");
    const std::string port_cfg = read_lora_cfg("mesh_port.txt");
    const std::string baud_cfg = read_lora_cfg("mesh_baud.txt");

    if (mode_cfg == "text") {
        if (port_cfg.empty() || !is_usb_serial_candidate(port_cfg)) {
            info.error = "No mesh serial API (set lora/mesh_port.txt for TEXTMSG)";
            return info;
        }
        if (::access(port_cfg.c_str(), F_OK) != 0) {
            info.error = "No mesh serial API (" + port_cfg + " missing)";
            return info;
        }
        int baud = 38400;
        if (!baud_cfg.empty()) {
            const int b = std::atoi(baud_cfg.c_str());
            if (b > 0) baud = b;
        }
        UartSession uart;
        std::string err;
        if (!uart.open(port_cfg, baud, err)) {
            info.error = err.empty() ? "No mesh serial API" : err;
            return info;
        }
        uart.close();
        info.present = true;
        info.mode = MeshSerialMode::Text;
        info.path = port_cfg;
        info.baud = baud;
        return info;
    }

    std::vector<std::string> ports;
    if (!port_cfg.empty() && is_usb_serial_candidate(port_cfg)) ports.push_back(port_cfg);
    for (const auto& p : list_usb_serials()) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end()) ports.push_back(p);
    }
    if (ports.empty()) {
        info.error = "No mesh serial API";
        return info;
    }

    int baud = 115200;
    if (!baud_cfg.empty()) {
        const int b = std::atoi(baud_cfg.c_str());
        if (b > 0) baud = b;
    }

    constexpr size_t kMaxProbe = 4;
    const size_t n = std::min(ports.size(), kMaxProbe);
    for (size_t i = 0; i < n; ++i) {
        if (cancelled(cancel)) {
            info.error = "cancelled";
            return info;
        }
        if (probe_phone_on_port(ports[i], baud, cancel)) {
            info.present = true;
            info.mode = MeshSerialMode::Phone;
            info.path = ports[i];
            info.baud = baud;
            return info;
        }
    }
    info.error = "No mesh serial API";
    return info;
}

MeshtasticSerial::~MeshtasticSerial() {
    close();
}

bool MeshtasticSerial::open(const MeshSerialInfo& info, std::string& error) {
    close();
    if (kDesktop) {
        error = "Cap / module not present";
        return false;
    }
    if (!info.present || info.mode == MeshSerialMode::None || info.path.empty()) {
        error = info.error.empty() ? "No mesh serial API" : info.error;
        return false;
    }
    if (!uart_.open(info.path, info.baud > 0 ? info.baud : 115200, error)) return false;
    mode_ = info.mode;
    path_ = info.path;
    rx_buf_.clear();
    if (mode_ == MeshSerialMode::Phone) {
        const uint8_t wake[] = {kStart1, kStart1, kStart1, kStart1};
        uart_.write_bytes(wake, sizeof(wake), error);
    }
    return true;
}

void MeshtasticSerial::close() {
    uart_.close();
    mode_ = MeshSerialMode::None;
    path_.clear();
    rx_buf_.clear();
}

bool MeshtasticSerial::is_open() const {
    return uart_.is_open();
}

bool MeshtasticSerial::write_frame(const std::vector<uint8_t>& pb, std::string& error) {
    if (pb.size() > kMaxFramePb) {
        error = "frame too large";
        return false;
    }
    std::vector<uint8_t> frame;
    frame.reserve(4 + pb.size());
    frame.push_back(kStart1);
    frame.push_back(kStart2);
    frame.push_back(static_cast<uint8_t>((pb.size() >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(pb.size() & 0xff));
    frame.insert(frame.end(), pb.begin(), pb.end());
    return uart_.write_bytes(frame.data(), frame.size(), error);
}

bool MeshtasticSerial::read_frame(std::vector<uint8_t>& pb, std::string& error, std::atomic<bool>* cancel,
                                  int timeout_ms) {
    pb.clear();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && !cancelled(cancel)) {
        std::string chunk;
        if (!uart_.read_available(chunk, error)) return false;
        if (!chunk.empty()) rx_buf_ += chunk;

        // Resync to START1 START2
        while (rx_buf_.size() >= 4) {
            const auto start = rx_buf_.find(std::string("\x94\xc3", 2));
            if (start == std::string::npos) {
                // Keep last byte in case it is START1
                if (!rx_buf_.empty() && static_cast<uint8_t>(rx_buf_.back()) == kStart1) {
                    rx_buf_.assign(1, static_cast<char>(kStart1));
                } else {
                    rx_buf_.clear();
                }
                break;
            }
            if (start > 0) rx_buf_.erase(0, start);
            if (rx_buf_.size() < 4) break;
            const size_t len = (static_cast<size_t>(static_cast<uint8_t>(rx_buf_[2])) << 8) |
                               static_cast<size_t>(static_cast<uint8_t>(rx_buf_[3]));
            if (len == 0 || len > kMaxFramePb) {
                rx_buf_.erase(0, 1);
                continue;
            }
            if (rx_buf_.size() < 4 + len) break;
            pb.assign(reinterpret_cast<const uint8_t*>(rx_buf_.data() + 4),
                      reinterpret_cast<const uint8_t*>(rx_buf_.data() + 4 + len));
            rx_buf_.erase(0, 4 + len);
            return true;
        }
        sleep_cancel(20, cancel);
    }
    if (cancelled(cancel)) {
        error = "cancelled";
        return false;
    }
    error = "timeout";
    return false;
}

bool MeshtasticSerial::send_want_config(uint32_t nonce, std::string& error) {
    return write_frame(make_want_config(nonce), error);
}

bool MeshtasticSerial::drain_phone_handshake(std::vector<MeshNode>* nodes, std::string& error,
                                             std::atomic<bool>* cancel, int timeout_ms) {
    const uint32_t nonce = 1;
    if (!send_want_config(nonce, error)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline && !cancelled(cancel)) {
        const int left = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count());
        if (left <= 0) break;
        std::vector<uint8_t> pb;
        std::string err;
        if (!read_frame(pb, err, cancel, std::min(left, 400))) {
            if (err == "cancelled") {
                error = err;
                return false;
            }
            continue;
        }
        const FromRadio fr = parse_from_radio(pb.data(), pb.size());
        if (fr.kind == FromKind::NodeInfo && nodes) {
            nodes->push_back(fr.node);
        } else if (fr.kind == FromKind::ConfigComplete) {
            return true;
        }
    }
    if (cancelled(cancel)) {
        error = "cancelled";
        return false;
    }
    // Partial node list is still useful.
    return true;
}

bool MeshtasticSerial::list_nodes(std::vector<MeshNode>& out, std::string& error, std::atomic<bool>* cancel,
                                  int timeout_ms) {
    out.clear();
    if (!is_open()) {
        error = "not open";
        return false;
    }
    if (mode_ == MeshSerialMode::Text) {
        error.clear();
        return true; // caller shows honest "no node table" line
    }
    if (mode_ != MeshSerialMode::Phone) {
        error = "No mesh serial API";
        return false;
    }
    return drain_phone_handshake(&out, error, cancel, timeout_ms);
}

bool MeshtasticSerial::send_text(const std::string& text, std::string& error) {
    if (!is_open()) {
        error = "not open";
        return false;
    }
    if (text.empty()) {
        error = "empty message";
        return false;
    }
    if (mode_ == MeshSerialMode::Text) {
        std::string line = text;
        if (line.back() != '\n') line.push_back('\n');
        return uart_.write_bytes(reinterpret_cast<const uint8_t*>(line.data()), line.size(), error);
    }
    if (mode_ != MeshSerialMode::Phone) {
        error = "No mesh serial API";
        return false;
    }
    return write_frame(make_text_packet(text), error);
}

bool MeshtasticSerial::listen_chat(const std::function<void(const MeshChatLine&)>& on_line, std::string& error,
                                   std::atomic<bool>& cancel) {
    if (!is_open()) {
        error = "not open";
        return false;
    }
    if (mode_ == MeshSerialMode::Text) {
        std::string line_acc;
        while (!cancel.load()) {
            std::string chunk;
            if (!uart_.read_available(chunk, error)) return false;
            if (!chunk.empty()) {
                for (char c : chunk) {
                    if (c == '\r') continue;
                    if (c == '\n') {
                        const std::string line = trim(line_acc);
                        line_acc.clear();
                        if (line.empty()) continue;
                        MeshChatLine m;
                        const auto colon = line.find(':');
                        if (colon != std::string::npos) {
                            m.from_label = trim(line.substr(0, colon));
                            m.text = trim(line.substr(colon + 1));
                        } else {
                            m.from_label = "?";
                            m.text = line;
                        }
                        if (on_line) on_line(m);
                    } else {
                        line_acc.push_back(c);
                        if (line_acc.size() > 512) line_acc.clear();
                    }
                }
            }
            sleep_cancel(40, &cancel);
        }
        return true;
    }
    if (mode_ != MeshSerialMode::Phone) {
        error = "No mesh serial API";
        return false;
    }
    // Keep session warm with an initial want_config so node names may arrive, then listen.
    drain_phone_handshake(nullptr, error, &cancel, 2500);
    error.clear();
    while (!cancel.load()) {
        std::vector<uint8_t> pb;
        std::string err;
        if (!read_frame(pb, err, &cancel, 400)) {
            if (err == "cancelled") return true;
            continue;
        }
        const FromRadio fr = parse_from_radio(pb.data(), pb.size());
        if (fr.kind != FromKind::Packet || !fr.packet.has_decoded) continue;
        if (fr.packet.data.portnum != kPortnumText) continue;
        MeshChatLine m;
        m.from = fr.packet.from;
        m.from_label = node_id_label(fr.packet.from);
        m.text = fr.packet.data.payload;
        m.snr = fr.packet.snr;
        m.has_snr = fr.packet.has_snr;
        m.rssi = fr.packet.rssi;
        m.has_rssi = fr.packet.has_rssi;
        if (on_line) on_line(m);
    }
    return true;
}

} // namespace platform
