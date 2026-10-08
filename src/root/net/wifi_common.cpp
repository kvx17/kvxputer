#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/app/powerSave.h"
#include "root/hal/radio_mem.h"
#include "root/app/ram_profile.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include "root/net/wifi_mac.h"
#include "esp_wifi.h"
#include "menu/ble/ble_common.h"
#include <esp_event.h>
#include <esp_netif.h>
#include <globals.h>

static TaskHandle_t timezoneTaskHandle = NULL;
static bool wifiTransitioning = false;

#if defined(ARDUINO_M5STACK_TAB5)
// ESP32-P4 talks to the on-board C6 over SDIO. Pins are set by M5.begin /
// BOARD_HAS_SDIO_ESP_HOSTED defaults, but the slave needs a settle window after
// WLAN_PWR_EN, and WiFi.mode must be checked — a failed hostedInit leaves the
// stack half-dead and the next scan/UI blit can freeze the DSI panel cyan.
static bool ensureTab5HostedWifi() {
    static bool pinsAsserted = false;
    if (!pinsAsserted) {
        pinsAsserted = true;
#if defined(CONFIG_ESP_WIFI_REMOTE_ENABLED)
        WiFi.setPins(
            BOARD_SDIO_ESP_HOSTED_CLK,
            BOARD_SDIO_ESP_HOSTED_CMD,
            BOARD_SDIO_ESP_HOSTED_D0,
            BOARD_SDIO_ESP_HOSTED_D1,
            BOARD_SDIO_ESP_HOSTED_D2,
            BOARD_SDIO_ESP_HOSTED_D3,
            BOARD_SDIO_ESP_HOSTED_RESET
        );
#endif
        // C6 boot after PI4IO WLAN_PWR_EN (M5.Power.begin).
        if (millis() < 800) delay(800 - millis());
        else delay(120);
    }
    return true;
}

void wifiPrepareTab5Hosted() { (void)ensureTab5HostedWifi(); }

static bool tab5WifiModeSta() {
    ensureTab5HostedWifi();
    for (int attempt = 0; attempt < 3; attempt++) {
        if (WiFi.mode(WIFI_MODE_STA)) return true;
        Serial.printf("[Tab5] WiFi.mode(STA) failed attempt %d\n", attempt + 1);
        delay(250 + attempt * 250);
        ensureTab5HostedWifi();
    }
    return false;
}
#endif

bool tab5RadioLater(const char *feature) {
#if defined(ARDUINO_M5STACK_TAB5)
    // esp_wifi_80211_tx / set_promiscuous on this SDK call weak
    // esp_wifi_remote_* stubs that return ESP_ERR_NOT_SUPPORTED (0x106).
    // There is no strong esp_hosted_wifi_80211_tx in pioarduino 55.03.39.
    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err == ESP_OK) {
        esp_wifi_set_promiscuous(false);
        return true;
    }
    if (err != ESP_ERR_NOT_SUPPORTED) return true;
    const char *what = (feature && feature[0]) ? feature : "WiFi raw/promisc";
    displayError(String(what) + "\nhosted C6 WiFi-remote:\npromiscuous/raw TX unsupported", true);
    return false;
#else
    (void)feature;
    return true;
#endif
}

esp_err_t wifiRawTx(wifi_interface_t ifx, const void *frame, int len, uint8_t retries) {
    // Tab5's linked esp_wifi_80211_tx is the remote trampoline. Today that
    // trampoline hits a weak stub and returns ESP_ERR_NOT_SUPPORTED; calling it
    // is what a future strong hosted inject would use.
    esp_err_t err = esp_wifi_80211_tx(ifx, frame, len, false);
    for (uint8_t i = 0; err == ESP_ERR_NO_MEM && i < retries; i++) {
        vTaskDelay(1); // let the driver drain TX buffers and retry
        err = esp_wifi_80211_tx(ifx, frame, len, false);
    }
    return err;
}

void ensureWifiPlatform() {
    static bool netifInitialized = false;
    static bool eventLoopCreated = false;
    static portMUX_TYPE platformMux = portMUX_INITIALIZER_UNLOCKED;

    portENTER_CRITICAL(&platformMux);
    bool needNetif = !netifInitialized;
    bool needLoop = !eventLoopCreated;
    portEXIT_CRITICAL(&platformMux);

    if (needNetif) {
        ESP_ERROR_CHECK(esp_netif_init());
        portENTER_CRITICAL(&platformMux);
        netifInitialized = true;
        portEXIT_CRITICAL(&platformMux);
    }

    if (needLoop) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_ERR_INVALID_STATE) { ESP_ERROR_CHECK(err); }
        portENTER_CRITICAL(&platformMux);
        eventLoopCreated = true;
        portEXIT_CRITICAL(&platformMux);
    }
}

bool _wifiConnect(const String &ssid, int encryption) {
    String password = kvxConfig.getWifiPassword(ssid);
    if (password == "" && encryption > 0) { password = keyboard(password, 63, "Network Password:", true); }
    if (password == "\x1B") return false;
    bool connected = _connectToWifiNetwork(ssid, password);
    bool retry = false;

    while (!connected) {
        wakeUpScreen();

        options = {
            {"Retry",  [&]() { retry = true; } },
            {"Cancel", [&]() { retry = false; }},
        };
        loopOptions(options);

        if (!retry) {
            wifiDisconnect();
            return false;
        }

        password = keyboard(password, 63, "Network Password:", true);
        if (password == "\x1B") {
            wifiDisconnect();
            return false;
        }
        connected = _connectToWifiNetwork(ssid, password);
    }

    if (connected) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();
        kvxConfig.addWifiCredential(ssid, password);

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }

    delay(200);
    return connected;
}

bool _connectToWifiNetwork(const String &ssid, const String &pwd) {
    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        if (BLEConnected) {
            displayWarning("Board with no PSRAM, closing BLE Stack");
            vTaskDelay(700 / portTICK_PERIOD_MS);
        }
        stopBLEStack();
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    RAM_LOG("wifi pre-mode"); // Wi-Fi is already up from the menu scan by this point
    drawMainBorderWithTitle("WiFi Connect");
    padprintln("");
    tft.setTextSize(FP);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    padprintln("Connecting to:");
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    padprint(ssid);
    tft.print(".");
#if defined(ARDUINO_M5STACK_TAB5)
    if (!tab5WifiModeSta()) {
        displayError("WiFi radio failed\n(C6 hosted)", true);
        return false;
    }
#else
    WiFi.mode(WIFI_MODE_STA);
#endif
    RAM_LOG("wifi post-mode");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    WiFi.begin(ssid, pwd);

    int i = 1;
    while (!WiFi.isConnected()) {
        if (tft.getCursorX() >= tftWidth - 12) {
            padprintln("");
            padprint("");
        }
#ifdef HAS_SCREEN
        tft.print(".");
#else
        Serial.print(".");
#endif

        if (i > 20) {
            displayError("Wifi Offline");
            vTaskDelay(500 / portTICK_RATE_MS);
            break;
        }

        vTaskDelay(500 / portTICK_RATE_MS);
        i++;
    }

    return WiFi.isConnected();
}

bool _setupAP() {
    IPAddress AP_GATEWAY(172, 0, 0, 1);
    WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
    WiFi.softAP(kvxConfig.wifiAp.ssid, kvxConfig.wifiAp.pwd, 6, 0, 4, false);
    wifiIP = WiFi.softAPIP().toString(); // update global var
    Serial.println("IP: " + wifiIP);
    wifiConnected = true;
    return true;
}

static bool isStockApSsid(String s) {
    s.trim();
    String u = s;
    u.toLowerCase();
    return s.length() == 0 || u == "bruce" || u == "brucenet" || s == "KvxputerNet";
}

static bool isStockApPwd(String s) {
    String u = s;
    u.toLowerCase();
    return s.length() == 0 || u == "bruce" || u == "kvxputernet";
}

bool wifiStartApInteractive() {
    if (WiFi.AP.started()) {
        displayInfo("AP already running", true);
        return true;
    }
    if (WiFi.isConnected()) {
        displayError("Disconnect WiFi first", true);
        return false;
    }

    String ssid = kvxConfig.wifiAp.ssid;
    String pwd = kvxConfig.wifiAp.pwd;
    if (isStockApSsid(ssid)) ssid = "kvxputer";
    if (isStockApPwd(pwd)) pwd = "kvxputer";

    ssid = keyboard(ssid, 32, "AP SSID:");
    if (ssid == "\x1B") return false;
    ssid.trim();
    if (ssid.length() == 0) {
        displayError("SSID cannot be empty", true);
        return false;
    }

    pwd = keyboard(pwd, 63, "AP Password:", true);
    if (pwd == "\x1B") return false;
    if (pwd.length() < 8) {
        displayError("Password min 8 chars", true);
        return false;
    }

    kvxConfig.setWifiApCreds(ssid, pwd);
    if (!wifiConnectMenu(WIFI_AP)) return false;
    displayInfo("pwd: " + kvxConfig.wifiAp.pwd, true);
    return true;
}

void wifiDisconnect() {
    wifiTransitioning = true;

    wifi_mode_t mode = WiFi.getMode();
    if (mode & WIFI_MODE_AP) {
        WiFi.softAPdisconnect();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (mode & WIFI_MODE_STA) {
        WiFi.disconnect(false, true);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (mode != WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    wifiConnected = false;
    wifiTransitioning = false;
    uiRamLeaveHeavy();
}

bool wifiConnectMenu(wifi_mode_t mode) {
    if (WiFi.isConnected()) return false; // safeguard

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }

    switch (mode) {
        case WIFI_AP: // access point
#if defined(ARDUINO_M5STACK_TAB5)
            ensureTab5HostedWifi();
            if (!WiFi.mode(WIFI_AP)) {
                displayError("WiFi radio failed\n(C6 hosted)", true);
                return false;
            }
#else
            WiFi.mode(WIFI_AP);
#endif
            return _setupAP();
            break;

        case WIFI_STA: { // station mode
            int nets;
            if (!radioHasMemForWifi()) {
                displayError("Low RAM: free BLE/SD first", true);
                return false;
            }
            uiRamEnterHeavy();
#if defined(ARDUINO_M5STACK_TAB5)
            if (!tab5WifiModeSta()) {
                displayError("WiFi radio failed\n(C6 hosted)", true);
                uiRamLeaveHeavy();
                return false;
            }
#else
            WiFi.mode(WIFI_MODE_STA);
#endif

            // wifiMACMenu();
            applyConfiguredMAC();

            bool refresh_scan = false;
            do {
                displayTextLine("Scanning..");
                nets = WiFi.scanNetworks();
                if (nets < 0) {
                    displayError("WiFi scan failed", true);
                    wifiDisconnect();
                    return false;
                }

                String selSsid = "";
                int selEnc = 0;
                bool selHidden = false;

                options = {};
                for (int i = 0; i < nets; i++) {
                    if (options.size() < 250) {
                        String ssid = WiFi.SSID(i);
                        int encryptionType = WiFi.encryptionType(i);
                        int32_t rssi = WiFi.RSSI(i);
                        int32_t ch = WiFi.channel(i);
                        // Check if the network is secured
                        String encryptionPrefix = (encryptionType == WIFI_AUTH_OPEN) ? "" : "#";
                        String encryptionTypeStr;
                        switch (encryptionType) {
                            case WIFI_AUTH_OPEN: encryptionTypeStr = "Open"; break;
                            case WIFI_AUTH_WEP: encryptionTypeStr = "WEP"; break;
                            case WIFI_AUTH_WPA_PSK: encryptionTypeStr = "WPA/PSK"; break;
                            case WIFI_AUTH_WPA2_PSK: encryptionTypeStr = "WPA2/PSK"; break;
                            case WIFI_AUTH_WPA_WPA2_PSK: encryptionTypeStr = "WPA/WPA2/PSK"; break;
                            case WIFI_AUTH_WPA2_ENTERPRISE: encryptionTypeStr = "WPA2/Enterprise"; break;
                            case WIFI_AUTH_WPA3_PSK: encryptionTypeStr = "WPA3/PSK"; break;
                            case WIFI_AUTH_WPA2_WPA3_PSK: encryptionTypeStr = "WPA2/WPA3/PSK"; break;
                            default: encryptionTypeStr = "Unknown"; break;
                        }

                        String optionText = encryptionPrefix + ssid + "(" + String(rssi) + "|" +
                                            encryptionTypeStr + "|ch." + String(ch) + ")";

                        options.push_back({optionText.c_str(), [&selSsid, &selEnc, ssid, encryptionType]() {
                                               selSsid = ssid;
                                               selEnc = encryptionType;
                                           }});
                    }
                }
                WiFi.scanDelete();
                options.push_back({"Hidden SSID", [&selHidden]() { selHidden = true; }});
                addOptionToMainMenu();

                loopOptions(options);
                options.clear();

                if (returnToMenu) {
                    refresh_scan = false;
                } else if (selHidden) {
                    String __ssid = keyboard("", 32, "Your SSID");
                    if (__ssid != "\x1B") _wifiConnect(__ssid.c_str(), 8);
                    refresh_scan = false;
                } else if (selSsid != "") {
                    _wifiConnect(selSsid, selEnc);
                    refresh_scan = false;
                } else if (check(EscPress)) {
                    refresh_scan = true;
                } else {
                    refresh_scan = false;
                }
            } while (refresh_scan);
        } break;

        case WIFI_AP_STA: // repeater mode
                          // _setupRepeater();
            break;

        default: // error handling
            Serial.println("Unknown wifi mode: " + String(mode));
            break;
    }

    if (returnToMenu) {
        wifiDisconnect(); // Forced turning off the wifi module if exiting back to the menu
        return false;
    }
    return wifiConnected;
}

void wifiConnectTask(void *pvParameters) {
    if (WiFi.isConnected()) return;

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        vTaskDelete(NULL);
        return;
    }

    // No-PSRAM guard: don't bring Wi-Fi up if the contiguous DMA block is too
    // small (e.g. BLE already active) — the scan would half-init the driver and
    // crash on teardown. Silent bail: this is a background auto-connect task.
    if (!radioHasMemForWifi()) {
        vTaskDelete(NULL);
        return;
    }
    uiRamEnterHeavy();

#if defined(ARDUINO_M5STACK_TAB5)
    if (!tab5WifiModeSta()) {
        uiRamLeaveHeavy();
        vTaskDelete(NULL);
        return;
    }
#else
    WiFi.mode(WIFI_MODE_STA);
#endif
    int nets = WiFi.scanNetworks();
    if (nets < 0) {
        uiRamLeaveHeavy();
        vTaskDelete(NULL);
        return;
    }
    String ssid;
    String pwd;

    for (int i = 0; i < nets; i++) {
        ssid = WiFi.SSID(i);
        pwd = kvxConfig.getWifiPassword(ssid);
        if (pwd == "") continue;

        WiFi.begin(ssid, pwd);
        for (int i = 0; i < 50; i++) {
            if (WiFi.isConnected()) {
                wifiConnected = true;
                wifiIP = WiFi.localIP().toString();

                // Start timezone update in background if not already running
                if (timezoneTaskHandle == NULL) {
                    xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
                }
                // Do not draw UI from this task — it races the main menu / apps
                // and previously stamped the theme border onto the kvx grid.
                break;
            }
            vTaskDelay(100 / portTICK_RATE_MS);
        }
    }
    WiFi.scanDelete();

    vTaskDelete(NULL);
    return;
}

String checkMAC() { return String(WiFi.macAddress()); }

bool wifiConnecttoKnownNet(void) {
    if (WiFi.isConnected()) return true; // safeguard

    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Check if WiFi is in transition
    if (wifiTransitioning) {
        displayTextLine("WiFi busy, please wait...");
        vTaskDelay(500 / portTICK_PERIOD_MS);
        return false;
    }

    // No-PSRAM guard: refuse before the scan brings Wi-Fi up in low memory.
    if (!radioHasMemForWifi()) {
        displayError("Low RAM: free BLE/SD first", true);
        return false;
    }
    uiRamEnterHeavy();

    bool result = false;
    int nets;
#if defined(ARDUINO_M5STACK_TAB5)
    if (!tab5WifiModeSta()) {
        displayError("WiFi radio failed\n(C6 hosted)", true);
        uiRamLeaveHeavy();
        return false;
    }
#endif
    displayTextLine("Scanning Networks..");
    WiFi.disconnect(true, true);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    nets = WiFi.scanNetworks();
    if (nets < 0) {
        displayError("WiFi scan failed", true);
        wifiDisconnect();
        return false;
    }
    for (int i = 0; i < nets; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        String ssid = WiFi.SSID(i);
        String password = kvxConfig.getWifiPassword(ssid);
        if (password != "") {
            Serial.println("Connecting to: " + ssid);
            result = _connectToWifiNetwork(ssid, password);
        }
        // Maybe it finds a known network and can't connect, then try the next
        // until it gets connected (or not)
        if (result) {
            Serial.println("Connected to: " + ssid);
            break;
        }
    }
    if (WiFi.isConnected()) {
        wifiConnected = true;
        wifiIP = WiFi.localIP().toString();

        // Start timezone update in background if not already running
        if (timezoneTaskHandle == NULL) {
            xTaskCreate(updateTimezoneTask, "updateTimezone", 4096, NULL, 1, &timezoneTaskHandle);
        }
    }
    return result;
}

void updateTimezoneTask(void *pvParameters) {
    // Wait a bit for connection to stabilize before updating timezone
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Only update timezone if WiFi is still connected
    if (WiFi.isConnected() && wifiConnected) { updateClockTimezone(); }

    // Clear the task handle before deleting
    timezoneTaskHandle = NULL;
    vTaskDelete(NULL);
}
