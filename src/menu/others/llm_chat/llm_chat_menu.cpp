/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "llm_chat.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <HardwareSerial.h>
#include <globals.h>

void llmChatMenu() {
#if !defined(HAS_LLM_MODULE)
    displayInfo("LLM Chat Stream\nRequires M5 LLM module\n(build with HAS_LLM_MODULE)", true);
    return;
#else
    HardwareSerial llm(2);
    llm.begin(115200, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    while (true) {
        String prompt = keyboard("", 80, "LLM prompt");
        if (prompt.length() == 0 || prompt == "\x1B") break;
        llm.println(prompt);
        String resp;
        unsigned long until = millis() + 8000;
        while (millis() < until) {
            while (llm.available()) resp += (char)llm.read();
            delay(20);
        }
        displayInfo(resp.length() ? resp.substring(0, 200) : "(no reply)", true);
    }
    llm.end();
#endif
}
#endif
