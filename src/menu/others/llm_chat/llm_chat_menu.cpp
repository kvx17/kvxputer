/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * LLM: HTTP stream (Ollama-style) or UART module.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "llm_chat.h"
#if defined(EVIL_EXTENSIONS)
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>
#include <vector>
#include "mbedtls/base64.h"

namespace {

String b64(const String &in) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, (const uint8_t *)in.c_str(), in.length());
    std::vector<uint8_t> out(olen + 1);
    if (mbedtls_base64_encode(out.data(), out.size(), &olen, (const uint8_t *)in.c_str(), in.length()) != 0)
        return "";
    out[olen] = 0;
    return String((char *)out.data());
}

void httpStream() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String host = keyboard("127.0.0.1", 48, "LLM host");
    if (!host.length() || host == "\x1B") return;
    String path = keyboard("/api/generate", 48, "API path");
    if (path == "\x1B") return;
    String model = keyboard("llama3", 32, "Model");
    if (model == "\x1B") return;
    String user = keyboard("", 32, "Basic user (opt)");
    String pass = user.length() ? keyboard("", 32, "Password") : "";
    while (true) {
        String prompt = keyboard("", 80, "Prompt");
        if (!prompt.length() || prompt == "\x1B") break;
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(12000);
        if (!client.connect(host.c_str(), 443)) {
            WiFiClient plain;
            if (!plain.connect(host.c_str(), 11434)) {
                displayError("Connect failed", true);
                continue;
            }
            String body = "{\"model\":\"" + model + "\",\"prompt\":\"" + prompt + "\",\"stream\":true}";
            String req = "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Type: application/json\r\n";
            if (user.length()) req += "Authorization: Basic " + b64(user + ":" + pass) + "\r\n";
            req += "Content-Length: " + String(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
            plain.print(req);
            String acc;
            unsigned long t0 = millis();
            while (plain.connected() && millis() - t0 < 20000) {
                while (plain.available()) acc += (char)plain.read();
                if (check(EscPress)) break;
                delay(10);
            }
            plain.stop();
            displayInfo(acc.substring(0, 220), true);
            continue;
        }
        String body = "{\"model\":\"" + model + "\",\"prompt\":\"" + prompt + "\",\"stream\":true}";
        String req = "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Type: application/json\r\n";
        if (user.length()) req += "Authorization: Basic " + b64(user + ":" + pass) + "\r\n";
        req += "Content-Length: " + String(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
        client.print(req);
        String acc;
        unsigned long t0 = millis();
        while (client.connected() && millis() - t0 < 20000) {
            while (client.available()) acc += (char)client.read();
            if (check(EscPress)) break;
            delay(10);
        }
        client.stop();
        displayInfo(acc.substring(0, 220), true);
    }
}

#if defined(HAS_LLM_MODULE)
void uartLlm() {
    HardwareSerial llm(2);
    llm.begin(115200, SERIAL_8N1, kvxConfigPins.gps_bus.rx, kvxConfigPins.gps_bus.tx);
    while (true) {
        String prompt = keyboard("", 80, "UART prompt");
        if (!prompt.length() || prompt == "\x1B") break;
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
}
#endif

} // namespace

void llmChatMenu() {
    std::vector<Option> opts = {
        {"HTTP stream (WiFi)", httpStream},
#if defined(HAS_LLM_MODULE)
        {"UART LLM module", uartLlm},
#else
        {"UART LLM module",
         []() { displayInfo("Build with HAS_LLM_MODULE\nfor Grove UART LLM", true); }},
#endif
        {"Back", []() {}},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "LLM Chat");
}
#endif
