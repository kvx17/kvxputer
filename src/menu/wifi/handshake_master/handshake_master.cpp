/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Handshake Master: ESP-NOW slave aggregator + PCAP inventory.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#if defined(EVIL_EXTENSIONS)
#include "handshake_master.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <globals.h>
#include <vector>

namespace {

constexpr uint16_t kEspNowMax = 250;
constexpr uint16_t kFragPayload = kEspNowMax - 5;

typedef struct {
    uint16_t frame_len;
    uint8_t fragment_number;
    bool last_fragment;
    uint8_t boardID;
    uint8_t frame[kFragPayload];
} __attribute__((packed)) WifiFrameFragment;

struct FragItem {
    uint8_t data[kEspNowMax];
    uint8_t len;
};

constexpr int kQ = 16;
FragItem gQ[kQ];
volatile int gQHead = 0;
volatile int gQTail = 0;
volatile uint32_t gRxCount = 0;

FS *storageFs() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) return nullptr;
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_CAPTURES_HANDSHAKES);
    return fs;
}

void IRAM_ATTR onNowRecv(
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_now_recv_info_t *info, const uint8_t *d, int l
#else
    const uint8_t *, const uint8_t *d, int l
#endif
) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    (void)info;
#endif
    if (l <= 0 || l > (int)kEspNowMax) return;
    int next = (gQHead + 1) % kQ;
    if (next == gQTail) return;
    memcpy(gQ[gQHead].data, d, l);
    gQ[gQHead].len = (uint8_t)l;
    gQHead = next;
}

bool writePcapHeader(File &file) {
    uint32_t magic = 0xa1b2c3d4;
    uint16_t vmaj = 2, vmin = 4;
    uint32_t z = 0, snap = 2500, net = 105;
    file.write((uint8_t *)&magic, 4);
    file.write((uint8_t *)&vmaj, 2);
    file.write((uint8_t *)&vmin, 2);
    file.write((uint8_t *)&z, 4);
    file.write((uint8_t *)&z, 4);
    file.write((uint8_t *)&z, 4);
    file.write((uint8_t *)&snap, 4);
    file.write((uint8_t *)&net, 4);
    return true;
}

void writePcapFrame(File &file, const uint8_t *buf, uint32_t len) {
    uint32_t ts = millis();
    uint32_t sec = ts / 1000, usec = (ts % 1000) * 1000;
    file.write((uint8_t *)&sec, 4);
    file.write((uint8_t *)&usec, 4);
    file.write((uint8_t *)&len, 4);
    file.write((uint8_t *)&len, 4);
    file.write(buf, len);
}

int countEapol(FS &fs, const String &path) {
    File f = fs.open(path, FILE_READ);
    if (!f) return -1;
    int hits = 0;
    uint8_t b[256];
    int prev = -1;
    while (int n = f.read(b, sizeof(b))) {
        for (int i = 0; i < n; i++) {
            if (prev == 0x88 && b[i] == 0x8e) hits++;
            prev = b[i];
        }
    }
    f.close();
    return hits;
}

void listPcaps(FS &fs, const char *dir, std::vector<String> &out) {
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }
    File f;
    while ((f = root.openNextFile())) {
        String name = f.name();
        bool dirent = f.isDirectory();
        size_t sz = f.size();
        f.close();
        if (dirent) continue;
        if (name.endsWith("/")) name.remove(name.length() - 1);
        int slash = name.lastIndexOf('/');
        String base = slash >= 0 ? name.substring(slash + 1) : name;
        if (base.endsWith(".pcap") || base.endsWith(".pcapng")) {
            out.push_back(String(dir) + "/" + base + " (" + String(sz) + " B)");
        }
        (void)sz;
    }
    root.close();
}

} // namespace

void handshakeMasterRun() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50);
    if (esp_now_init() != ESP_OK) {
        displayError("ESP-NOW init failed", true);
        return;
    }
    esp_now_register_recv_cb(onNowRecv);

    String fn;
    int n = 0;
    do {
        fn = String(kvx::paths::WIFI_CAPTURES_HANDSHAKES) + "/master_" + String(n++) + ".pcap";
    } while (fs->exists(fn));
    File pcap = fs->open(fn, FILE_WRITE);
    if (!pcap) {
        esp_now_deinit();
        displayError("PCAP open failed", true);
        return;
    }
    writePcapHeader(pcap);

    gQHead = gQTail = 0;
    gRxCount = 0;
    uint8_t reasm[2048];
    uint16_t reasmLen = 0;
    uint8_t expectFrag = 0;

    drawMainBorderWithTitle("Handshake Master");
    tft.drawString("Waiting ESP-NOW slaves", 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        while (gQTail != gQHead) {
            FragItem item = gQ[gQTail];
            gQTail = (gQTail + 1) % kQ;
            if (item.len < 5) continue;
            const WifiFrameFragment *rx = (const WifiFrameFragment *)item.data;
            if (rx->frame_len == 0 || rx->frame_len > kFragPayload) continue;
            if (rx->fragment_number == 0) {
                reasmLen = 0;
                expectFrag = 0;
            }
            if (rx->fragment_number != expectFrag) {
                reasmLen = 0;
                expectFrag = 0;
                continue;
            }
            if (reasmLen + rx->frame_len > sizeof(reasm)) {
                reasmLen = 0;
                expectFrag = 0;
                continue;
            }
            memcpy(reasm + reasmLen, rx->frame, rx->frame_len);
            reasmLen += rx->frame_len;
            expectFrag++;
            if (rx->last_fragment && reasmLen > 0) {
                writePcapFrame(pcap, reasm, reasmLen);
                gRxCount++;
                reasmLen = 0;
                expectFrag = 0;
            }
        }
        tft.fillRect(10, 58, tftWidth - 20, 16, kvxConfig.bgColor);
        tft.drawString("Frames: " + String((unsigned)gRxCount), 10, 58);
        delay(40);
    }

    pcap.close();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    wifiDisconnect();
    displayInfo("Saved\n" + fn + "\n" + String((unsigned)gRxCount) + " frames", true);
}

void checkHandshakes() {
    FS *fs = storageFs();
    if (!fs) {
        displayError("No storage", true);
        return;
    }
    std::vector<String> files;
    listPcaps(*fs, kvx::paths::WIFI_CAPTURES_HANDSHAKES, files);
    listPcaps(*fs, kvx::paths::WIFI_CAPTURES, files);
    if (files.empty()) {
        displayError("No .pcap files", true);
        return;
    }
    std::vector<Option> opts;
    for (const auto &label : files) {
        String entry = label;
        opts.push_back({entry.c_str(), [fs, entry]() {
                            int sp = entry.lastIndexOf(" (");
                            String path = sp > 0 ? entry.substring(0, sp) : entry;
                            int hits = countEapol(*fs, path);
                            displayInfo(path + "\nEAPOL markers: " + String(hits), true);
                        }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Check Handshakes");
}

#endif
