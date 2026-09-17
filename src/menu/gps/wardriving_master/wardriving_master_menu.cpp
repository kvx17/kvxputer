/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Wardriving Master: ESP-NOW slave aggregator into Wigle CSV.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "wardriving_master.h"
#if defined(EVIL_EXTENSIONS)
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "esp_now.h"
#include <WiFi.h>
#include <globals.h>

namespace {
struct LineItem {
    uint8_t data[200];
    uint8_t len;
};
constexpr int kQ = 16;
LineItem gQ[kQ];
volatile int gQHead = 0, gQTail = 0;

void IRAM_ATTR onSlaveRecv(
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_now_recv_info_t *, const uint8_t *d, int l
#else
    const uint8_t *, const uint8_t *d, int l
#endif
) {
    if (l <= 0) return;
    int next = (gQHead + 1) % kQ;
    if (next == gQTail) return;
    int n = l > 200 ? 200 : l;
    memcpy(gQ[gQHead].data, d, n);
    gQ[gQHead].len = (uint8_t)n;
    gQHead = next;
}
} // namespace

void wardrivingMasterMenu() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        displayError("No storage", true);
        return;
    }
    kvx::paths::ensureDir(*fs, kvx::paths::GPS_WARDRIVING);
    String path = String(kvx::paths::GPS_WARDRIVING) + "/master_" + String(millis()) + ".csv";
    File out = fs->open(path, FILE_WRITE);
    if (!out) {
        displayError("CSV open failed", true);
        return;
    }
    out.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,"
                "AccuracyMeters,Type");
    gQHead = gQTail = 0;
    uint32_t lines = 0;
    String last;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    if (esp_now_init() != ESP_OK) {
        out.close();
        displayError("ESP-NOW init failed", true);
        return;
    }
    esp_now_register_recv_cb(onSlaveRecv);

    drawMainBorderWithTitle("Wardriving Master");
    tft.drawString("Listening for slaves", 10, 40);
    tft.drawString("ESC to stop", 10, tftHeight - 20);
    EscPress = false;
    while (!check(EscPress) && !returnToMenu) {
        while (gQTail != gQHead) {
            LineItem item = gQ[gQTail];
            gQTail = (gQTail + 1) % kQ;
            out.write(item.data, item.len);
            if (item.len == 0 || item.data[item.len - 1] != '\n') out.write('\n');
            lines++;
            last = String((char *)item.data).substring(0, item.len);
        }
        tft.fillRect(10, 56, tftWidth - 20, 32, kvxConfig.bgColor);
        tft.drawString("Rows: " + String(lines), 10, 56);
        tft.drawString(last.substring(0, 28), 10, 72);
        delay(40);
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    out.close();
    wifiDisconnect();
    displayInfo("Saved\n" + path, true);
}
#endif
