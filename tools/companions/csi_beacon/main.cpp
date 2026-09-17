// ═══════════════════════════════════════════════════════════════
// CSI-Beacon — Espressif-style CSI transmitter
//
// Exact replication of Espressif's esp-csi sender config:
//   - Pure STA mode (no AP)
//   - Fixed MAC: 1A:00:00:00:00:ID
//   - ESP-NOW broadcast at 100Hz
//   - HT40 bandwidth, MCS0 rate, channel 11
//   - Power save disabled
//
// Also creates AP "CSI-Beacon-N" for single-beacon mode fallback.
//
// Serial commands (115200):
//   beacon id N        — set beacon ID (1-8), changes MAC + SSID
//   beacon status      — show config
//   beacon help        — commands list
//
// Flash on ANY ESP32: C3, C6, S2, S3, classic ESP32
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include <esp_now.h>

static uint8_t  beacon_id          = 1;
static uint32_t beacon_pkt_count   = 0;
static char     beacon_ssid[32]    = "CSI-Beacon-1";
// Fixed MAC per Espressif pattern: 1A:00:00:00:00:ID
static uint8_t  fixed_mac[6]       = {0x1A, 0x00, 0x00, 0x00, 0x00, 0x01};

#if defined(LED_BUILTIN)
  #define BEACON_LED LED_BUILTIN
#elif defined(ARDUINO_M5STACK_NANOC6)
  #define BEACON_LED 7
#else
  #define BEACON_LED -1
#endif

static WiFiUDP udp;
static String serialBuf;
static const uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void handleCommand(const String& cmd);

void startBeacon() {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(100);

    // Espressif config: APSTA for dual-mode support
    // STA side: fixed MAC + ESP-NOW (for multi mode)
    // AP side: CSI-Beacon-N (for single mode)
    WiFi.mode(WIFI_AP_STA);

    // Set fixed MAC on STA interface (Espressif style)
    fixed_mac[5] = beacon_id;
    esp_err_t mac_err = esp_wifi_set_mac(WIFI_IF_STA, fixed_mac);
    if (mac_err != ESP_OK) Serial.printf("[BEACON] WARNING: set_mac failed (%d)\n", mac_err);
    // Verify
    uint8_t check_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, check_mac);
    Serial.printf("[BEACON] STA MAC set: %02X:%02X:%02X:%02X:%02X:%02X\n",
        check_mac[0], check_mac[1], check_mac[2], check_mac[3], check_mac[4], check_mac[5]);

    // AP for single-beacon fallback
    snprintf(beacon_ssid, sizeof(beacon_ssid), "CSI-Beacon-%d", beacon_id);
    WiFi.softAP(beacon_ssid, NULL, 11);

    // Espressif config (ignore errors for WiFi 6 chips)
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);  // may fail on C6
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(11, WIFI_SECOND_CHAN_BELOW);

    esp_wifi_start();
    delay(100);

    // ESP-NOW init
    esp_now_init();
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast_addr, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // Set ESP-NOW rate (may not be available on all chips)
    esp_now_rate_config_t rate_cfg = {};
    rate_cfg.phymode = WIFI_PHY_MODE_HT40;
    rate_cfg.rate = WIFI_PHY_RATE_MCS0_LGI;
    esp_err_t rc = esp_now_set_peer_rate_config(broadcast_addr, &rate_cfg);
    if (rc != ESP_OK) Serial.printf("[BEACON] Rate config not supported (%d), using default\n", rc);

    // UDP for single-beacon ping replies
    udp.begin(55555);

    beacon_pkt_count = 0;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println(F("\n========================================"));
    Serial.println(F("  CSI-Beacon — Espressif-style TX"));
    Serial.println(F("  Fixed MAC + ESP-NOW 100Hz + HT40"));
    Serial.println(F("========================================"));

    if (BEACON_LED >= 0) {
        pinMode(BEACON_LED, OUTPUT);
        digitalWrite(BEACON_LED, LOW);
    }

    startBeacon();

    Serial.printf("[BEACON] ID=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
        beacon_id, fixed_mac[0], fixed_mac[1], fixed_mac[2],
        fixed_mac[3], fixed_mac[4], fixed_mac[5]);
    Serial.printf("[BEACON] SSID=%s CH=11 HT40 MCS0\n", beacon_ssid);
    Serial.println(F("[BEACON] Type 'beacon help' for commands"));
}

void loop() {
    static unsigned long last_tx = 0;

    unsigned long now = millis();

    // 100Hz ESP-NOW broadcast (Espressif: usleep(1000*1000/100))
    if (now - last_tx >= 10) {
        last_tx = now;

        uint32_t count = beacon_pkt_count;
        esp_now_send(broadcast_addr, (const uint8_t*)&count, sizeof(count));

        // Also handle UDP pings for single-beacon mode
        int pktSize = udp.parsePacket();
        if (pktSize > 0) {
            uint8_t buf[32];
            udp.read(buf, sizeof(buf));
            uint8_t reply[8] = {0xC5, 0x1B, beacon_id, (uint8_t)(count & 0xFF)};
            udp.beginPacket(udp.remoteIP(), udp.remotePort());
            udp.write(reply, 4);
            udp.endPacket();
        }

        beacon_pkt_count++;

        if (BEACON_LED >= 0 && (beacon_pkt_count % 50) == 0)
            digitalWrite(BEACON_LED, !digitalRead(BEACON_LED));

        if ((beacon_pkt_count % 1000) == 0)
            Serial.printf("[BEACON] TX:%lu\n", beacon_pkt_count);
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialBuf.trim();
            if (serialBuf.length() > 0) { handleCommand(serialBuf); serialBuf = ""; }
        } else if (serialBuf.length() < 128) serialBuf += c;
    }

    delay(1);
}

void handleCommand(const String& cmd) {
    if (!cmd.startsWith("beacon ")) { Serial.println(F("Type 'beacon help'")); return; }
    String sub = cmd.substring(7); sub.trim();

    if (sub == "help") {
        Serial.println(F("── CSI-Beacon Commands ──"));
        Serial.println(F("beacon id N     — set ID 1-8 (MAC=1A:00:00:00:00:N)"));
        Serial.println(F("beacon status   — show config"));
        Serial.println(F("beacon restart  — restart"));
        Serial.println(F("─────────────────────────"));
    } else if (sub.startsWith("id ")) {
        int id = sub.substring(3).toInt();
        if (id >= 1 && id <= 8) {
            beacon_id = (uint8_t)id;
            startBeacon();
            Serial.printf("[BEACON] ID=%d MAC=1A:00:00:00:00:%02X SSID=%s\n",
                beacon_id, beacon_id, beacon_ssid);
        } else Serial.println(F("ERR: id 1-8"));
    } else if (sub == "status") {
        Serial.println(F("── CSI-Beacon Status ──"));
        Serial.printf("ID:    %d\n", beacon_id);
        Serial.printf("MAC:   %02X:%02X:%02X:%02X:%02X:%02X\n",
            fixed_mac[0], fixed_mac[1], fixed_mac[2],
            fixed_mac[3], fixed_mac[4], fixed_mac[5]);
        Serial.printf("SSID:  %s\n", beacon_ssid);
        Serial.printf("CH:    11 (HT40)\n");
        Serial.printf("TX:    %lu (100Hz)\n", beacon_pkt_count);
        Serial.printf("AP:    %d client(s)\n", WiFi.softAPgetStationNum());
    } else if (sub == "restart") {
        ESP.restart();
    } else Serial.println(F("Unknown. Type 'beacon help'"));
}
