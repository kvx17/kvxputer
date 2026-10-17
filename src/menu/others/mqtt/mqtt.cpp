#if !defined(LITE_VERSION)
#include "mqtt.h"

#include "root/app/utils.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/paths.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"

#include <Arduino.h>
#include <WiFi.h>
#include <globals.h>
#include <mqtt_client.h>
#include <vector>

namespace {

esp_mqtt_client_handle_t g_client = nullptr;
std::vector<String> g_log;
String g_broker;
String g_topic = "kvxputer/#";

static void logLine(const String &s) {
    g_log.push_back(s);
    if (g_log.size() > 40) g_log.erase(g_log.begin());
    Serial.println("[MQTT] " + s);
}

static void mqttEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: logLine("connected"); break;
        case MQTT_EVENT_DISCONNECTED: logLine("disconnected"); break;
        case MQTT_EVENT_DATA: {
            String t = String(event->topic, event->topic_len);
            String d = String(event->data, event->data_len);
            if (d.length() > 48) d = d.substring(0, 48) + "...";
            logLine(t + " = " + d);
            break;
        }
        case MQTT_EVENT_ERROR: logLine("error"); break;
        default: break;
    }
}

bool ensureConnected() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return false;
    return true;
}

void disconnectMqtt() {
    if (g_client) {
        esp_mqtt_client_stop(g_client);
        esp_mqtt_client_destroy(g_client);
        g_client = nullptr;
    }
}

bool connectMqtt(const String &uri, const String &user, const String &pass) {
    disconnectMqtt();
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri.c_str();
    if (user.length()) {
        cfg.credentials.username = user.c_str();
        cfg.credentials.authentication.password = pass.c_str();
    }
    g_client = esp_mqtt_client_init(&cfg);
    if (!g_client) return false;
    esp_mqtt_client_register_event(g_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqttEvent, nullptr);
    if (esp_mqtt_client_start(g_client) != ESP_OK) {
        disconnectMqtt();
        return false;
    }
    g_broker = uri;
    return true;
}

void showLog() {
    ScrollableTextArea area("MQTT log");
    area.addLine(g_broker.length() ? g_broker : "(not connected)");
    for (auto &l : g_log) area.addLine(l);
    if (g_log.empty()) area.addLine("(empty)");
    area.show(true);
}

} // namespace

void mqttHelp() {
    showAppHelp(
        {
            "Client for IoT brokers",
            "(not a broker itself).",
            "Needs WiFi STA.",
            "",
            "Connect:",
            "  URI e.g. mqtt://192.168.1.10",
            "  Optional user/password.",
            "",
            "Subscribe / Publish:",
            "  Topic + message via keyboard.",
            "",
            "Load topics.txt:",
            "  support_files/mqtt/topics.txt",
            "  one topic per line.",
            "",
            "Message log: recent events.",
            "Disconnect cleans the client.",
            "",
            "TLS limited by stack; prefer",
            "lab/LAN brokers for testing.",
        }
    );
}

void mqttMenu() {
    options = {
        {"Help", mqttHelp},
        {"Connect",
         []() {
             if (!ensureConnected()) return;
             String host = keyboard("mqtt://192.168.1.1", 64, "Broker URI");
             if (host.isEmpty() || host == "\x1B") return;
             if (!host.startsWith("mqtt")) host = "mqtt://" + host;
             String user = keyboard("", 32, "User (opt)");
             String pass;
             if (user != "\x1B" && user.length()) pass = keyboard("", 32, "Pass", true);
             if (connectMqtt(host, user == "\x1B" ? "" : user, pass == "\x1B" ? "" : pass))
                 displayInfo("Connecting...", true);
             else displayError("MQTT init fail", true);
         }},
        {"Subscribe",
         []() {
             if (!g_client) {
                 displayError("Not connected", true);
                 return;
             }
             String t = keyboard(g_topic, 64, "Topic");
             if (t.isEmpty() || t == "\x1B") return;
             g_topic = t;
             esp_mqtt_client_subscribe(g_client, g_topic.c_str(), 0);
             logLine("sub " + g_topic);
             displayInfo("Subscribed", true);
         }},
        {"Publish",
         []() {
             if (!g_client) {
                 displayError("Not connected", true);
                 return;
             }
             String t = keyboard("kvxputer/test", 64, "Topic");
             if (t.isEmpty() || t == "\x1B") return;
             String m = keyboard("hello", 120, "Message");
             if (m == "\x1B") return;
             esp_mqtt_client_publish(g_client, t.c_str(), m.c_str(), m.length(), 0, 0);
             logLine("pub " + t);
         }},
        {"Load topics.txt",
         []() {
             if (!g_client) {
                 displayError("Not connected", true);
                 return;
             }
             FS *fs = nullptr;
             if (!getFsStorage(fs) || !fs || !fs->exists(kvx::paths::MQTT_TOPICS)) {
                 displayError("No topics.txt", true);
                 return;
             }
             File f = fs->open(kvx::paths::MQTT_TOPICS, FILE_READ);
             while (f && f.available()) {
                 String line = f.readStringUntil('\n');
                 line.trim();
                 if (line.length() && !line.startsWith("#")) {
                     esp_mqtt_client_subscribe(g_client, line.c_str(), 0);
                     logLine("sub " + line);
                 }
             }
             if (f) f.close();
             displayInfo("Topics loaded", true);
         }},
        {"Message log", showLog},
        {"Disconnect",
         []() {
             disconnectMqtt();
             displayInfo("Disconnected", true);
         }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "MQTT");
    options.clear();
}

#endif
