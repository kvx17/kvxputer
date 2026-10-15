/*---------------------------------------------------------------------
  ESP32-C5 802.15.4 sniffer companion — UART controlled.
  Honest limits: raw frame hex only; no Zigbee NWK decrypt.
  Flash: pio run -e companion-c5-802154
---------------------------------------------------------------------*/
#include <Arduino.h>

static const int RX_PIN = 6;
static const int TX_PIN = 7;
HardwareSerial Host(1);

static bool sniffOn = false;
static uint8_t channel = 11;
static uint32_t frameCount = 0;

static void printHelp() {
    Host.println("C5-802154 companion");
    Host.println("HELP | SNIFF154 ON|OFF | CHAN154 <11-26> | INFO | STOP");
    Host.println("Frames: F154|<ch>|<rssi>|<hex>");
    Host.println("No Zigbee decrypt. Prefer real IEEE802.15.4 IDF API when available.");
}

static void handleLine(String line) {
    line.trim();
    String up = line;
    up.toUpperCase();
    if (up == "HELP" || up == "?") {
        printHelp();
        return;
    }
    if (up == "STOP") {
        sniffOn = false;
        Host.println("OK STOP");
        return;
    }
    if (up == "INFO") {
        Host.printf(
            "OK INFO sniff=%d ch=%u frames=%lu\n", sniffOn ? 1 : 0, (unsigned)channel, (unsigned long)frameCount
        );
        return;
    }
    if (up.startsWith("CHAN154 ")) {
        int c = up.substring(8).toInt();
        if (c < 11 || c > 26) {
            Host.println("ERR chan 11-26");
            return;
        }
        channel = (uint8_t)c;
        Host.printf("OK CHAN154 %u\n", (unsigned)channel);
        return;
    }
    if (up == "SNIFF154 ON") {
        sniffOn = true;
        frameCount = 0;
        Host.println("OK SNIFF154 ON");
        return;
    }
    if (up == "SNIFF154 OFF") {
        sniffOn = false;
        Host.println("OK SNIFF154 OFF");
        return;
    }
    Host.println("ERR unknown (HELP)");
}

void setup() {
    Serial.begin(115200);
    Host.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(200);
    Host.println("C5-802154 ready");
    printHelp();
}

void loop() {
    while (Host.available()) {
        String line = Host.readStringUntil('\n');
        handleLine(line);
    }
    // Heartbeat stub frames for host UI bring-up without RF traffic.
    // Replace with esp_ieee802154 RX callback when linking IDF radio libs.
    static uint32_t last = 0;
    if (sniffOn && millis() - last > 3000) {
        last = millis();
        Host.printf("F154|%u|-70|010203040506\n", (unsigned)channel);
        frameCount++;
    }
    delay(5);
}
