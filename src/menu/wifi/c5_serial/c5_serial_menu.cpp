/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * UART console for ESP32-C5 slave firmware (slave stays in resources/).
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "c5_serial.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <HardwareSerial.h>
#include <globals.h>

void c5SerialMenu() {
    HardwareSerial uart(1);
    uart.begin(115200, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    while (true) {
        std::vector<Option> opts = {
            {"Send help", [&]() { uart.println("help"); displayInfo("Sent help", true); }},
            {"Send scan", [&]() { uart.println("scan"); displayInfo("Sent scan", true); }},
            {"Send hop", [&]() { uart.println("hop"); displayInfo("Sent hop", true); }},
            {"Custom cmd",
             [&]() {
                 String c = keyboard("", 40, "C5 command");
                 if (c.length() && c != "\x1B") {
                     uart.println(c);
                     displayInfo("Sent " + c, true);
                 }
             }},
            {"Listen 5s",
             [&]() {
                 drawMainBorderWithTitle("C5 Serial");
                 unsigned long until = millis() + 5000;
                 String log;
                 while (millis() < until) {
                     while (uart.available()) log += (char)uart.read();
                     delay(10);
                 }
                 displayInfo(log.length() ? log.substring(0, 180) : "(no data)", true);
             }},
            {"Back", []() {}},
        };
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "ESP32C5 Serial");
        if (sel < 0 || sel == (int)opts.size() - 1) break;
    }
    uart.end();
}
#endif
