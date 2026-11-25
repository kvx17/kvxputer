// kvxputerzero-blescan — privileged helper for Wall of Flipper / AirTag.
// Grant: sudo setcap cap_net_raw,cap_net_admin+ep <this binary>
//
// Captures raw LE advertising payloads (NimBLE getPayload() equivalent) so
// matching can follow the Cardputer wall_of_flipper / wall_of_airtag logic.
//
// Usage: kvxputerzero-blescan [seconds]
// Stdout: ADV <mac> <rssi> <addr_type> <payload_hex>
//         OK <n>
//         or a single ERR line on hard failure.

#include "ambient_net_caps.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef HCI_DEV_NONE
#define HCI_DEV_NONE 0xffff
#endif

namespace {

struct Hit {
    std::string mac;
    int rssi{-127};
    uint8_t addr_type{1};
    std::vector<uint8_t> payload;
};

std::string mac_str(const bdaddr_t& a) {
    char mac[18];
    std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", a.b[5], a.b[4], a.b[3], a.b[2], a.b[1],
                  a.b[0]);
    return mac;
}

void ingest_report(std::map<std::string, Hit>& by_mac, const le_advertising_info* info) {
    Hit h;
    h.mac = mac_str(info->bdaddr);
    h.rssi = static_cast<int>(static_cast<int8_t>(info->data[info->length]));
    h.addr_type = info->bdaddr_type;
    h.payload.assign(info->data, info->data + info->length);
    auto it = by_mac.find(h.mac);
    if (it == by_mac.end()) {
        by_mac.emplace(h.mac, std::move(h));
        return;
    }
    it->second.rssi = h.rssi;
    if (h.payload.size() >= it->second.payload.size()) it->second.payload = std::move(h.payload);
}

void parse_le_meta(std::map<std::string, Hit>& by_mac, const uint8_t* meta_data, int meta_len) {
    if (meta_len < 2) return;
    // meta_data[0] = subevent already stripped by caller OR included?
    // Caller passes evt_le_meta_event::data which starts with num_reports for ADV_REPORT.
    const uint8_t num = meta_data[0];
    const uint8_t* ptr = meta_data + 1;
    const uint8_t* end = meta_data + meta_len;
    for (uint8_t n = 0; n < num; ++n) {
        if (ptr + 1 + 1 + 6 + 1 > end) break;
        const auto* info = reinterpret_cast<const le_advertising_info*>(ptr);
        if (info->data + info->length + 1 > end) break;
        ingest_report(by_mac, info);
        ptr = info->data + info->length + 1;
    }
}

// Monitor channel: see HCI traffic while bluetoothd discovers (needs CAP_NET_RAW).
bool scan_via_monitor(std::map<std::string, Hit>& by_mac, int seconds) {
    const int sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (sock < 0) return false;
    sockaddr_hci addr{};
    addr.hci_family = AF_BLUETOOTH;
    addr.hci_dev = HCI_DEV_NONE;
    addr.hci_channel = HCI_CHANNEL_MONITOR;
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    // Kick BlueZ LE discovery so adverts flow; ignore failures.
    (void)std::system(("bluetoothctl --timeout " + std::to_string(seconds) + " scan on >/dev/null 2>&1 &").c_str());

    const int loops = seconds * 10;
    for (int i = 0; i < loops; ++i) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{0, 100000};
        if (select(sock + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        uint8_t buf[2048];
        const int len = static_cast<int>(read(sock, buf, sizeof(buf)));
        if (len < 10) continue;
        // BlueZ monitor packet: opcode(le16) index(le16) len(le16) data…
        // Inside data, look for HCI event LE meta (0x3e) advertising reports.
        for (int off = 0; off + 8 < len; ++off) {
            if (buf[off] != EVT_LE_META_EVENT) continue;
            // Heuristic: next byte is plen, then subevent.
            if (off + 2 >= len) continue;
            const uint8_t plen = buf[off + 1];
            if (off + 2 + plen > len) continue;
            const uint8_t sub = buf[off + 2];
            if (sub != EVT_LE_ADVERTISING_REPORT) continue;
            parse_le_meta(by_mac, buf + off + 3, plen - 1);
        }
    }
    (void)std::system("bluetoothctl scan off >/dev/null 2>&1");
    close(sock);
    return !by_mac.empty();
}

// Exclusive HCI LE scan (needs CAP_NET_ADMIN / CAP_NET_RAW). Stops BlueZ discovery first.
bool scan_via_hci_active(std::map<std::string, Hit>& by_mac, int seconds) {
    (void)std::system("busctl --system call org.bluez /org/bluez/hci0 org.bluez.Adapter1 StopDiscovery "
                      "2>/dev/null");
    usleep(150000);

    const int dev_id = hci_get_route(nullptr);
    if (dev_id < 0) return false;
    const int dd = hci_open_dev(dev_id);
    if (dd < 0) return false;

    (void)hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
    // Active scan, high duty — mirrors Cardputer scannerBleStart (interval≈100, window≈99).
    if (hci_le_set_scan_parameters(dd, 0x01, htobs(0x00A0), htobs(0x0099), 0x00, 0x00, 2000) < 0) {
        hci_close_dev(dd);
        return false;
    }
    if (hci_le_set_scan_enable(dd, 0x01, 0x00 /* filter_dup = off */, 2000) < 0) {
        hci_close_dev(dd);
        return false;
    }

    hci_filter of{};
    socklen_t olen = sizeof(of);
    getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen);
    hci_filter nf{};
    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf));

    const int loops = seconds * 10;
    for (int i = 0; i < loops; ++i) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(dd, &fds);
        timeval tv{0, 100000};
        if (select(dd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
        uint8_t buf[HCI_MAX_EVENT_SIZE];
        const int len = static_cast<int>(read(dd, buf, sizeof(buf)));
        if (len < 5 || buf[0] != HCI_EVENT_PKT || buf[1] != EVT_LE_META_EVENT) continue;
        const auto* meta = reinterpret_cast<evt_le_meta_event*>(buf + 3);
        if (meta->subevent != EVT_LE_ADVERTISING_REPORT) continue;
        const int plen = buf[2];
        if (plen < 2) continue;
        parse_le_meta(by_mac, meta->data, plen - 1);
    }

    (void)hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
    setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
    hci_close_dev(dd);
    return !by_mac.empty();
}

void print_hits(const std::map<std::string, Hit>& by_mac) {
    for (const auto& kv : by_mac) {
        const Hit& h = kv.second;
        std::printf("ADV %s %d %u ", h.mac.c_str(), h.rssi, static_cast<unsigned>(h.addr_type));
        for (uint8_t b : h.payload) std::printf("%02x", b);
        std::printf("\n");
    }
    std::printf("OK %zu\n", by_mac.size());
}

} // namespace

int main(int argc, char** argv) {
    int seconds = 6;
    if (argc >= 2) {
        seconds = std::atoi(argv[1]);
        if (seconds < 1) seconds = 1;
        if (seconds > 30) seconds = 30;
    }

    pass_net_caps_to_children();
    std::map<std::string, Hit> by_mac;
    // Prefer exclusive HCI LE scan (same raw AD stream Cardputer NimBLE sees).
    if (!scan_via_hci_active(by_mac, seconds)) {
        by_mac.clear();
        if (!scan_via_monitor(by_mac, seconds)) {
            std::fprintf(stderr,
                         "ERR BLE helper lacks privileges (open the tool again and enter the "
                         "administrator password)\n");
            return 1;
        }
    }
    print_hits(by_mac);
    return 0;
}
