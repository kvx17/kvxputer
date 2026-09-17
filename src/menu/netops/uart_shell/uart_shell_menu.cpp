/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * UART shell on Grove GPS UART pins.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "uart_shell.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <HardwareSerial.h>
#include <globals.h>

void uartShellMenu() {
    String baudStr = num_keyboard("115200", 7, "UART baud");
    if (baudStr == "\x1B") return;
    int baud = baudStr.toInt();
    if (baud <= 0) baud = 115200;

    HardwareSerial uart(1);
    int rx = kvxConfigPins.gps_bus.rx;
    int tx = kvxConfigPins.gps_bus.tx;
    uart.begin(baud, SERIAL_8N1, rx, tx);

    String log;
    drawMainBorderWithTitle("UART Shell");
    tft.drawString("RX" + String(rx) + " TX" + String(tx) + " @" + String(baud), 10, 40);
    tft.drawString("Type to send, ESC quit", 10, tftHeight - 20);
    EscPress = false;

    while (!check(EscPress) && !returnToMenu) {
        while (uart.available()) {
            char c = (char)uart.read();
            if (c == '\r') continue;
            log += c;
            if (log.length() > 400) log.remove(0, log.length() - 400);
        }
        keyStroke ks = _getKeyPress();
        if (ks.pressed && ks.word.size()) {
            for (char c : ks.word) {
                uart.write((uint8_t)c);
                log += c;
            }
            if (ks.enter) {
                uart.write('\r');
                uart.write('\n');
                log += '\n';
            }
        }
        tft.fillRect(10, 56, tftWidth - 20, tftHeight - 80, kvxConfig.bgColor);
        tft.setCursor(10, 56);
        int maxY = tftHeight - 28;
        int y = 56;
        int start = 0;
        for (int i = 0; i < (int)log.length(); i++) {
            if (log[i] == '\n') start = i + 1;
        }
        // show last ~4 lines
        int lines = 0;
        for (int i = (int)log.length() - 1; i >= 0; i--) {
            if (log[i] == '\n') {
                lines++;
                if (lines >= 5) {
                    start = i + 1;
                    break;
                }
            }
        }
        tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
        tft.setCursor(10, 56);
        tft.print(log.substring(start));
        delay(20);
        (void)y;
        (void)maxY;
    }
    uart.end();
}
#endif
