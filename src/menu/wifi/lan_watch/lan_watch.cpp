#if !defined(LITE_VERSION)
#include "lan_watch.h"

#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "menu/others/audio.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <globals.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>

namespace {

String macToStr(const uint8_t *m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

bool resolveGwMac(uint8_t out[6]) {
    memset(out, 0, 6);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK) return false;
    ip4_addr_t gw;
    gw.addr = info.gw.addr;
    struct netif *lw = nullptr;
    // Walk ARP table
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *ip = nullptr;
        struct netif *nif = nullptr;
        eth_addr *eth = nullptr;
        if (etharp_get_entry(i, &ip, &nif, &eth) && ip && eth && ip4_addr_cmp(ip, &gw)) {
            memcpy(out, eth->addr, 6);
            return true;
        }
    }
    // Fallback: AP BSSID often equals gateway on home routers
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(out, ap.bssid, 6);
        return true;
    }
    (void)lw;
    return false;
}

void logEvent(FS *fs, const String &line) {
    if (!fs) return;
    kvx::paths::ensureDir(*fs, kvx::paths::WIFI_BLUE_WATCH);
    File f = fs->open(String(kvx::paths::WIFI_BLUE_WATCH) + "/lan_watch.csv", FILE_APPEND);
    if (!f) return;
    f.printf("%lu,%s\n", (unsigned long)millis(), line.c_str());
    f.close();
}

} // namespace

void lanWatchMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;

    FS *fs = nullptr;
    getFsStorage(fs);

    uint8_t baseGw[6] = {0};
    bool haveBase = resolveGwMac(baseGw);
    IPAddress baseGwIp = WiFi.gatewayIP();
    IPAddress baseDns = WiFi.dnsIP(0);

    drawMainBorderWithTitle("LAN Watch");
    tft.setTextSize(FP);
    padprintln("GW " + baseGwIp.toString());
    if (haveBase) padprintln("MAC " + macToStr(baseGw));
    else padprintln("MAC learning...");
    padprintln("Watching ARP/GW  ESC");

    EscPress = false;
    uint32_t last = 0;
    int alerts = 0;
    while (!check(EscPress) && !returnToMenu && !forceHome) {
        if (millis() - last < 1500) {
            delay(50);
            continue;
        }
        last = millis();
        if (!WiFi.isConnected()) {
            displayError("WiFi lost", true);
            break;
        }

        uint8_t cur[6];
        bool ok = resolveGwMac(cur);
        IPAddress gw = WiFi.gatewayIP();
        IPAddress dns = WiFi.dnsIP(0);
        String status = "ok";

        if (!haveBase && ok) {
            memcpy(baseGw, cur, 6);
            haveBase = true;
            status = "learned " + macToStr(baseGw);
        } else if (haveBase && ok && memcmp(baseGw, cur, 6) != 0) {
            status = "GW MAC CHANGE " + macToStr(cur);
            alerts++;
            logEvent(fs, status);
            _tone(2000, 80);
        } else if (gw != baseGwIp) {
            status = "GW IP CHANGE " + gw.toString();
            alerts++;
            logEvent(fs, status);
            _tone(2000, 80);
            baseGwIp = gw;
        } else if ((uint32_t)dns != 0 && dns != baseDns && (uint32_t)baseDns != 0) {
            status = "DNS CHANGE " + dns.toString();
            alerts++;
            logEvent(fs, status);
            _tone(1800, 60);
            baseDns = dns;
        }

        // Heuristic second DHCP server: if SoftAP peers advertise different gateway via ARP
        // (full DHCP sniff is heavier) — note DNS/GW changes cover common rogue DHCP outcomes.

        tft.fillRect(8, 50, tftWidth - 16, 40, kvxConfig.bgColor);
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.drawString("alerts " + String(alerts), 8, 50);
        tft.drawString(status.substring(0, 28), 8, 62);
    }
}

#endif
