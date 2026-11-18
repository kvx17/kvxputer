#pragma once

#include "uart_session.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace platform {

enum class MeshSerialMode {
    None,
    Phone, // PhoneAPI framing 0x94 0xC3
    Text,  // TEXTMSG line protocol (explicit mesh_mode=text)
};

struct MeshSerialInfo {
    bool present{false};
    MeshSerialMode mode{MeshSerialMode::None};
    std::string path;
    int baud{0};
    std::string error; // set when !present
};

struct MeshNode {
    uint32_t num{0};
    std::string short_name;
    std::string long_name;
    uint32_t last_heard{0}; // unix seconds when known
    float snr{0};
    bool has_snr{false};
    int rssi{0};
    bool has_rssi{false};
};

struct MeshChatLine {
    uint32_t from{0};
    std::string from_label; // short name or !id
    std::string text;
    float snr{0};
    bool has_snr{false};
    int rssi{0};
    bool has_rssi{false};
};

// Desktop: immediate Cap / module not present (no /dev scan).
// Device: optional ~/.config/kvxputerzero/lora/mesh_port.txt + ttyACM*/ttyUSB* PhoneAPI probe;
// TEXTMSG only when lora/mesh_mode.txt is "text" and the configured port opens.
MeshSerialInfo probe_meshtastic_serial(std::atomic<bool>* cancel = nullptr);

class MeshtasticSerial {
public:
    MeshtasticSerial() = default;
    ~MeshtasticSerial();

    MeshtasticSerial(const MeshtasticSerial&) = delete;
    MeshtasticSerial& operator=(const MeshtasticSerial&) = delete;

    bool open(const MeshSerialInfo& info, std::string& error);
    void close();
    bool is_open() const;
    MeshSerialMode mode() const { return mode_; }
    const std::string& path() const { return path_; }

    // PhoneAPI: want_config then collect NodeInfo until config_complete_id or timeout/cancel.
    // Text mode: logs that TEXTMSG has no node table and returns true.
    bool list_nodes(std::vector<MeshNode>& out, std::string& error, std::atomic<bool>* cancel = nullptr,
                    int timeout_ms = 8000);

    bool send_text(const std::string& text, std::string& error);

    // PhoneAPI: stream FromRadio text packets. Text: read "name: text" lines.
    // Calls on_line for each message until cancel.
    bool listen_chat(const std::function<void(const MeshChatLine&)>& on_line, std::string& error,
                     std::atomic<bool>& cancel);

private:
    bool write_frame(const std::vector<uint8_t>& pb, std::string& error);
    bool read_frame(std::vector<uint8_t>& pb, std::string& error, std::atomic<bool>* cancel, int timeout_ms);
    bool send_want_config(uint32_t nonce, std::string& error);
    bool drain_phone_handshake(std::vector<MeshNode>* nodes, std::string& error, std::atomic<bool>* cancel,
                               int timeout_ms);

    UartSession uart_;
    MeshSerialMode mode_{MeshSerialMode::None};
    std::string path_;
    std::string rx_buf_;
};

} // namespace platform
