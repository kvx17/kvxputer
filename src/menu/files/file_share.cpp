#include "file_share.h"
#include "file_share_page.h"
#include <qrcode.h>
#include "root/config/config.h"
#include "root/input/mykeyboard.h"
#include "menu/ble/ble_common.h"
#include "root/net/webInterface.h"
#include "root/net/wifi_common.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/kvx_ui.h"
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp32-hal-psram.h>
#include <globals.h>
#include <vector>

namespace {

static constexpr uint8_t kMaxClients = 4;
static constexpr size_t kNameMax = 48;

static AsyncWebServer *shareServer = nullptr;
static DNSServer shareDns;
static bool shareDnsRunning = false;
static bool shareMdns = false;
static bool shareModeAp = true;      // true = softAP, false = STA on existing WiFi
static bool shareOwnedWifi = false;  // we brought WiFi up for this session
static bool sharePwdVisible = false;
static bool shareUploadBusy = false;
static uint8_t shareScreen = 0; // 0 info, 1 url QR, 2 wifi QR (AP only)
static bool shareQrInvert = true;

static void saveShareConfig() {
    kvxConfig.setFileShareSettings(
        kvxConfig.fileShare.ssid, kvxConfig.fileShare.pwd, kvxConfig.fileShare.user,
        kvxConfig.fileShare.portalPwd, kvxConfig.fileShare.readOnly, kvxConfig.fileShare.channel,
        kvxConfig.fileShare.root
    );
}

static String shareDisplayIp() {
    if (shareModeAp) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

static String shareHttpUrl() { return String("http://") + shareDisplayIp(); }
static std::vector<String> shareSessions;
static FS *shareUploadFs = nullptr;
static String shareUploadPath;
static bool shareUploadFailed = false;
static int shareUploadFailCode = 500; // 409 busy, 507 no space, else 500

struct TransferEvent {
    uint32_t seq = 0;
    char direction = 0; // 'U' or 'D'
    char name[kNameMax] = {};
    bool ok = false;
};
static TransferEvent shareEvent;

static void noteTransfer(char direction, const String &name, bool ok) {
    shareEvent.direction = direction;
    shareEvent.ok = ok;
    size_t n = name.length();
    if (n >= kNameMax) n = kNameMax - 1;
    memcpy(shareEvent.name, name.c_str(), n);
    shareEvent.name[n] = 0;
    shareEvent.seq++;
}

static String makeToken(int length = 24) {
    String token;
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length; i++) token += charset[random(0, (int)sizeof(charset) - 1)];
    return token;
}

static bool isValidSession(const String &token) {
    for (const String &t : shareSessions) {
        if (t == token) return true;
    }
    return false;
}

static String cookieToken(AsyncWebServerRequest *request) {
    if (!request->hasHeader("Cookie")) return "";
    String c = request->getHeader("Cookie")->value();
    int idx = c.indexOf("KVXSHARE=");
    if (idx < 0) return "";
    int start = idx + 9;
    int end = c.indexOf(';', start);
    if (end < 0) end = c.length();
    return c.substring(start, end);
}

static bool checkShareAuth(AsyncWebServerRequest *request) {
    return isValidSession(cookieToken(request));
}

static void sendUnauthorized(AsyncWebServerRequest *request) {
    request->send(401, "text/plain", "Unauthorized");
}

static String humanSize(uint64_t bytes) {
    if (bytes < 1024) return String((uint32_t)bytes) + " B";
    if (bytes < 1024ULL * 1024) return String(bytes / 1024.0, 1) + " kB";
    if (bytes < 1024ULL * 1024 * 1024) return String(bytes / 1024.0 / 1024.0, 1) + " MB";
    return String(bytes / 1024.0 / 1024.0 / 1024.0, 1) + " GB";
}

static bool pathHasBadSegment(const String &path) {
    if (!path.startsWith("/")) return true;
    if (path == "/") return false;
    int start = 1;
    while (start <= (int)path.length()) {
        int end = path.indexOf('/', start);
        if (end < 0) end = path.length();
        String seg = path.substring(start, end);
        if (seg.length() == 0) return true; // "//"
        if (seg == "." || seg == "..") return true;
        if (end >= (int)path.length()) break;
        start = end + 1;
    }
    return false;
}

// Resolve vol+path into absolute FS path. Applies fileShare.root when set.
static bool resolveSharePath(
    const String &vol, const String &reqPath, FS *&fsOut, String &absOut, bool *sdMissing
) {
    if (sdMissing) *sdMissing = false;
    if (vol != "sd" && vol != "lfs") return false;
    if (pathHasBadSegment(reqPath)) return false;

    String root = kvxConfig.fileShare.root;
    root.trim();
    if (root.length() && !root.startsWith("/")) root = "/" + root;
    while (root.endsWith("/") && root.length() > 1) root.remove(root.length() - 1);
    if (root.length() && pathHasBadSegment(root)) return false;

    String rel = reqPath;
    if (rel != "/" && rel.endsWith("/")) rel.remove(rel.length() - 1);

    if (root.length()) {
        if (rel == "/") absOut = root;
        else absOut = root + rel;
    } else {
        absOut = rel;
    }
    if (absOut.length() == 0) absOut = "/";

    if (vol == "sd") {
        if (!setupSdCard()) {
            if (sdMissing) *sdMissing = true;
            return false;
        }
        fsOut = &SD;
    } else {
        fsOut = &LittleFS;
    }
    return true;
}

static String uniqueUploadName(FS &fs, const String &dir, const String &filename) {
    String base = filename;
    String ext = "";
    int dot = filename.lastIndexOf('.');
    int slash = filename.lastIndexOf('/');
    if (dot > slash) {
        base = filename.substring(0, dot);
        ext = filename.substring(dot);
    }
    String tryName = filename;
    for (int i = 0; i < 100; i++) {
        String full = (dir == "/") ? ("/" + tryName) : (dir + "/" + tryName);
        if (!fs.exists(full)) return tryName;
        tryName = base + " (" + String(i + 2) + ")" + ext;
    }
    return base + " (99)" + ext;
}

static uint64_t freeBytes(FS &fs, bool isSd) {
    if (isSd) return SD.totalBytes() - SD.usedBytes();
    return LittleFS.totalBytes() - LittleFS.usedBytes();
}

static void stopShareServer() {
    if (shareDnsRunning) {
        shareDns.stop();
        shareDnsRunning = false;
    }
    if (shareServer) {
        shareServer->end();
        shareServer->~AsyncWebServer();
        free(shareServer);
        shareServer = nullptr;
    }
    if (shareMdns) {
        MDNS.end();
        shareMdns = false;
    }
    shareSessions.clear();
    shareUploadBusy = false;
    shareUploadFs = nullptr;
    shareUploadPath = "";
    shareUploadFailed = false;
    shareUploadFailCode = 500;
}

static bool ensureApDhcpRunning() {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        Serial.println("[FileShare] WIFI_AP_DEF missing");
        return false;
    }
    esp_err_t e = esp_netif_dhcps_start(ap_netif);
    if (e == ESP_OK || e == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return true;
    Serial.printf("[FileShare] dhcps_start: %s — restarting\n", esp_err_to_name(e));
    esp_netif_dhcps_stop(ap_netif);
    e = esp_netif_dhcps_start(ap_netif);
    Serial.printf("[FileShare] dhcps restart: %s\n", esp_err_to_name(e));
    return e == ESP_OK || e == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
}

static bool startShareAp() {
    shareModeAp = true;
    shareOwnedWifi = true;
    cleanlyStopWebUiForWiFiFeature();
    if (server) {
        displayError("WebUI still active", true);
        return false;
    }

    // Same preflight as wifiConnectMenu(WIFI_AP): free BLE if this board requires it.
    if (FORCE_RADIO_TEARDOWN_ON_SWITCH) {
        stopBLEStack();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    wifiDisconnect();
    vTaskDelay(pdMS_TO_TICKS(400));
    WiFi.persistent(false);

#if defined(ARDUINO_M5STACK_TAB5)
    wifiPrepareTab5Hosted();
    if (!WiFi.mode(WIFI_AP)) {
        displayError("WiFi radio failed\n(C6 hosted)", true);
        return false;
    }
#else
    WiFi.mode(WIFI_AP);
#endif
    vTaskDelay(pdMS_TO_TICKS(100));

    // Exact same softAP bring-up as working WebUI `_setupAP()` (gateway
    // 172.0.0.1, 3-arg config, ch6). Custom 5-arg configs and
    // enableDhcpCaptivePortal() were leaving DHCPS stopped so phones could
    // not associate/lease (169.254.* or fail to join).
    String ssid = kvxConfig.fileShare.ssid;
    String pwd = kvxConfig.fileShare.pwd;
    ssid.trim();
    if (ssid.length() == 0) ssid = "kvxputer";
    if (pwd.length() < 8) pwd = "kvxputer";

    IPAddress apGw(172, 0, 0, 1);
    if (!WiFi.softAPConfig(apGw, apGw, IPAddress(255, 255, 255, 0))) {
        displayError("AP config failed", true);
        wifiDisconnect();
        return false;
    }
    if (!WiFi.softAP(ssid.c_str(), pwd.c_str(), 6, 0, 4, false)) {
        displayError("softAP failed", true);
        wifiDisconnect();
        return false;
    }

    esp_wifi_set_ps(WIFI_PS_NONE);
    (void)ensureApDhcpRunning();

    wifiConnected = true;
    wifiIP = WiFi.softAPIP().toString();
    Serial.printf("[FileShare] AP '%s' IP %s (WebUI-compatible)\n", ssid.c_str(), wifiIP.c_str());

    vTaskDelay(pdMS_TO_TICKS(500));

    // DNS after AP is up (captive probes). Failure must not tear down the AP.
    IPAddress dnsIp = WiFi.softAPIP();
    if (dnsIp != IPAddress((uint32_t)0)) {
        shareDns.setErrorReplyCode(DNSReplyCode::NoError);
        shareDnsRunning = shareDns.start(53, "*", dnsIp);
        if (!shareDnsRunning) Serial.println("[FileShare] DNS optional start failed");
    }
    return true;
}

static void redirectToShare(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader("Location", shareHttpUrl() + "/");
    request->send(response);
}

static void sendSharePage(AsyncWebServerRequest *request) {
    // Flash string + explicit length (ESP32 maps PROGMEM; avoid ambiguous overloads).
    size_t len = strlen(FILE_SHARE_PAGE);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", reinterpret_cast<const uint8_t *>(FILE_SHARE_PAGE), len);
    if (!response) {
        request->send(500, "text/plain", "page alloc failed");
        return;
    }
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

static void addCaptiveProbeRoutes() {
    // Phones probe these and drop the AP if they never get a response ("no internet").
    const char *paths[] = {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/ncsi.txt",
        "/connecttest.txt",
        "/redirect",
        "/success.txt",
        "/canonical.html",
        "/fwlink",
    };
    for (const char *p : paths) {
        shareServer->on(p, HTTP_GET, [](AsyncWebServerRequest *request) { redirectToShare(request); });
    }
}

static void configureShareRoutes() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    if (shareModeAp) addCaptiveProbeRoutes();

    shareServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) { sendSharePage(request); });

    // Plain probe so a browser/curl can confirm the HTTP stack is up.
    shareServer->on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "kvxputer-file-share");
    });

    shareServer->on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        String user = request->hasParam("username", true) ? request->getParam("username", true)->value() : "";
        String pwd = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
        if (user == kvxConfig.fileShare.user && pwd == kvxConfig.fileShare.portalPwd) {
            String token = makeToken();
            shareSessions.push_back(token);
            if (shareSessions.size() > 8) shareSessions.erase(shareSessions.begin());
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "ok");
            response->addHeader("Set-Cookie", "KVXSHARE=" + token + "; Path=/; HttpOnly");
            request->send(response);
            return;
        }
        request->send(401, "text/plain", "bad credentials");
    });

    shareServer->on("/logout", HTTP_POST, [](AsyncWebServerRequest *request) {
        String token = cookieToken(request);
        for (auto it = shareSessions.begin(); it != shareSessions.end(); ++it) {
            if (*it == token) {
                shareSessions.erase(it);
                break;
            }
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "ok");
        response->addHeader("Set-Cookie", "KVXSHARE=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        request->send(response);
    });

    shareServer->on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        bool sdOk = setupSdCard();
        String json = "{";
        json += "\"readOnly\":";
        json += kvxConfig.fileShare.readOnly ? "true" : "false";
        json += ",\"clients\":";
        json += String(WiFi.softAPgetStationNum());
        json += ",\"sd\":";
        if (sdOk) {
            json += "{\"free\":\"" + humanSize(SD.totalBytes() - SD.usedBytes()) + "\",\"used\":\"" +
                    humanSize(SD.usedBytes()) + "\",\"total\":\"" + humanSize(SD.totalBytes()) + "\"}";
        } else {
            json += "null";
        }
        json += ",\"lfs\":{\"free\":\"" + humanSize(LittleFS.totalBytes() - LittleFS.usedBytes()) +
                "\",\"used\":\"" + humanSize(LittleFS.usedBytes()) + "\",\"total\":\"" +
                humanSize(LittleFS.totalBytes()) + "\"}";
        json += "}";
        request->send(200, "application/json", json);
    });

    shareServer->on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        String vol = request->hasParam("vol") ? request->getParam("vol")->value() : "sd";
        String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
        FS *fs = nullptr;
        String abs;
        bool sdMissing = false;
        if (!resolveSharePath(vol, path, fs, abs, &sdMissing)) {
            request->send(400, "text/plain", sdMissing ? "No SD" : "bad path");
            return;
        }
        if (!fs->exists(abs)) {
            request->send(404, "text/plain", "not found");
            return;
        }
        File root = fs->open(abs);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            request->send(400, "text/plain", "not a folder");
            return;
        }

        String json = "{\"path\":\"";
        // escape path for JSON
        String showPath = path;
        if (showPath.length() == 0) showPath = "/";
        for (size_t i = 0; i < showPath.length(); i++) {
            char c = showPath[i];
            if (c == '"' || c == '\\') json += '\\';
            json += c;
        }
        json += "\",\"items\":[";
        bool first = true;
        while (true) {
            if (esp_get_free_heap_size() < 8192) break;
            bool isDir = false;
            String fullPath = root.getNextFileName(&isDir);
            if (fullPath.length() == 0) break;
            String name = fullPath.substring(fullPath.lastIndexOf('/') + 1);
            if (name.length() == 0) continue;
            uint64_t sz = 0;
            time_t mtime = 0;
            if (!isDir) {
                File f = fs->open(fullPath);
                if (f) {
                    sz = f.size();
                    mtime = f.getLastWrite();
                    f.close();
                }
            }
            if (!first) json += ',';
            first = false;
            json += "{\"name\":\"";
            for (size_t i = 0; i < name.length(); i++) {
                char c = name[i];
                if (c == '"' || c == '\\') json += '\\';
                json += c;
            }
            json += "\",\"dir\":";
            json += isDir ? "true" : "false";
            json += ",\"size\":";
            json += String((unsigned long)sz);
            json += ",\"mtime\":";
            json += String((unsigned long)mtime);
            json += '}';
            delay(1);
        }
        root.close();
        json += "]}";
        request->send(200, "application/json", json);
    });

    shareServer->on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        String vol = request->hasParam("vol") ? request->getParam("vol")->value() : "";
        String path = request->hasParam("path") ? request->getParam("path")->value() : "";
        FS *fs = nullptr;
        String abs;
        bool sdMissing = false;
        if (!resolveSharePath(vol, path, fs, abs, &sdMissing)) {
            request->send(400, "text/plain", sdMissing ? "No SD" : "bad path");
            return;
        }
        if (!fs->exists(abs)) {
            request->send(404, "text/plain", "not found");
            return;
        }
        File f = fs->open(abs);
        if (!f || f.isDirectory()) {
            if (f) f.close();
            request->send(400, "text/plain", "not a file");
            return;
        }
        f.close();
        String base = abs.substring(abs.lastIndexOf('/') + 1);
        noteTransfer('D', base, true);
        request->send(*fs, abs, "application/octet-stream", true);
    });

    auto denyIfReadOnly = [](AsyncWebServerRequest *request) -> bool {
        if (kvxConfig.fileShare.readOnly) {
            request->send(403, "text/plain", "read-only");
            return true;
        }
        return false;
    };

    shareServer->on("/api/mkdir", HTTP_POST, [denyIfReadOnly](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        if (denyIfReadOnly(request)) return;
        String vol = request->hasParam("vol", true) ? request->getParam("vol", true)->value() : "";
        String path = request->hasParam("path", true) ? request->getParam("path", true)->value() : "/";
        String name = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";
        name.trim();
        if (name.length() == 0 || name.indexOf('/') >= 0 || name == "." || name == "..") {
            request->send(400, "text/plain", "bad name");
            return;
        }
        FS *fs = nullptr;
        String abs;
        if (!resolveSharePath(vol, path, fs, abs, nullptr)) {
            request->send(400, "text/plain", "bad path");
            return;
        }
        String full = (abs == "/") ? ("/" + name) : (abs + "/" + name);
        if (pathHasBadSegment(full) || !fs->mkdir(full)) {
            request->send(500, "text/plain", "mkdir failed");
            return;
        }
        request->send(200, "text/plain", "ok");
    });

    shareServer->on("/api/rename", HTTP_POST, [denyIfReadOnly](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        if (denyIfReadOnly(request)) return;
        String vol = request->hasParam("vol", true) ? request->getParam("vol", true)->value() : "";
        String path = request->hasParam("path", true) ? request->getParam("path", true)->value() : "/";
        String oldN = request->hasParam("old", true) ? request->getParam("old", true)->value() : "";
        String name = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";
        name.trim();
        oldN.trim();
        if (name.length() == 0 || oldN.length() == 0 || name.indexOf('/') >= 0 || oldN.indexOf('/') >= 0 ||
            name == "." || name == ".." || oldN == "." || oldN == "..") {
            request->send(400, "text/plain", "bad name");
            return;
        }
        FS *fs = nullptr;
        String abs;
        if (!resolveSharePath(vol, path, fs, abs, nullptr)) {
            request->send(400, "text/plain", "bad path");
            return;
        }
        String from = (abs == "/") ? ("/" + oldN) : (abs + "/" + oldN);
        String to = (abs == "/") ? ("/" + name) : (abs + "/" + name);
        if (!fs->exists(from) || !fs->rename(from, to)) {
            request->send(500, "text/plain", "rename failed");
            return;
        }
        request->send(200, "text/plain", "ok");
    });

    shareServer->on("/api/delete", HTTP_POST, [denyIfReadOnly](AsyncWebServerRequest *request) {
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        if (denyIfReadOnly(request)) return;
        String vol = request->hasParam("vol", true) ? request->getParam("vol", true)->value() : "";
        String path = request->hasParam("path", true) ? request->getParam("path", true)->value() : "/";
        String name = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";
        name.trim();
        if (name.length() == 0 || name.indexOf('/') >= 0 || name == "." || name == "..") {
            request->send(400, "text/plain", "bad name");
            return;
        }
        FS *fs = nullptr;
        String abs;
        if (!resolveSharePath(vol, path, fs, abs, nullptr)) {
            request->send(400, "text/plain", "bad path");
            return;
        }
        String full = (abs == "/") ? ("/" + name) : (abs + "/" + name);
        if (!fs->exists(full) || !deleteFromSd(*fs, full)) {
            request->send(500, "text/plain", "delete failed");
            return;
        }
        request->send(200, "text/plain", "ok");
    });

    shareServer->on(
        "/api/upload",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (shareUploadFailed) {
                int code = shareUploadFailCode;
                String msg = "upload failed";
                if (code == 409) msg = "upload busy";
                else if (code == 507) msg = "no space";
                else if (code == 403) msg = "read-only";
                else if (code == 401) msg = "Unauthorized";
                noteTransfer('U', shareUploadPath, false);
                shareUploadBusy = false;
                shareUploadFailed = false;
                shareUploadFailCode = 500;
                shareUploadFs = nullptr;
                shareUploadPath = "";
                request->send(code, "text/plain", msg);
                return;
            }
            String base = shareUploadPath;
            int slash = base.lastIndexOf('/');
            if (slash >= 0) base = base.substring(slash + 1);
            noteTransfer('U', base, true);
            shareUploadBusy = false;
            shareUploadFs = nullptr;
            shareUploadPath = "";
            request->send(200, "text/plain", "ok");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len,
           bool final) {
            auto failUpload = [](int code) {
                shareUploadFailed = true;
                shareUploadFailCode = code;
            };
            if (!checkShareAuth(request)) {
                failUpload(401);
                return;
            }
            if (kvxConfig.fileShare.readOnly) {
                failUpload(403);
                return;
            }
            if (!index) {
                if (shareUploadBusy) {
                    failUpload(409);
                    return;
                }
                String vol = request->hasParam("vol") ? request->getParam("vol")->value() : "";
                String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
                FS *fs = nullptr;
                String abs;
                if (!resolveSharePath(vol, path, fs, abs, nullptr)) {
                    failUpload(400);
                    return;
                }
                if (filename.indexOf('/') >= 0 || filename == "." || filename == ".." ||
                    filename.length() == 0) {
                    failUpload(400);
                    return;
                }
                if (request->contentLength() > 0) {
                    bool isSd = (fs == &SD);
                    if (request->contentLength() > freeBytes(*fs, isSd)) {
                        failUpload(507);
                        return;
                    }
                }
                String unique = uniqueUploadName(*fs, abs, filename);
                String full = (abs == "/") ? ("/" + unique) : (abs + "/" + unique);
                shareUploadBusy = true;
                shareUploadFailed = false;
                shareUploadFailCode = 500;
                shareUploadFs = fs;
                shareUploadPath = full;
                request->_tempFile = fs->open(full, "w");
                if (!request->_tempFile) {
                    failUpload(500);
                    shareUploadBusy = false;
                    return;
                }
            }
            if (shareUploadFailed) return;
            if (len && request->_tempFile) request->_tempFile.write(data, len);
            if (final && request->_tempFile) request->_tempFile.close();
        }
    );

    shareServer->onNotFound([](AsyncWebServerRequest *request) {
        // Captive portal: any unknown Host/path from phone probes → share page.
        if (shareModeAp && request->method() == HTTP_GET && !request->url().startsWith("/api")) {
            redirectToShare(request);
            return;
        }
        request->send(404, "text/plain", "not found");
    });

    shareServer->begin();
    Serial.printf("[FileShare] HTTP on %s mode=%s\n", shareHttpUrl().c_str(), shareModeAp ? "AP" : "STA");
}

static String maskPwd(const String &pwd, bool show) {
    if (show) return pwd;
    String m;
    for (size_t i = 0; i < pwd.length(); i++) m += '*';
    return m;
}

static String lastTransferLine() {
    if (shareEvent.seq == 0 || shareEvent.direction == 0) return "Last: -";
    String d = (shareEvent.direction == 'U') ? "UP " : "DN ";
    return "Last: " + d + String(shareEvent.name) + (shareEvent.ok ? " ok" : " fail");
}

static int shareLineY(int line, int sz = FP) {
    return KVX_TOPBAR_H + 2 + line * (sz * LH + 2);
}

static void drawInfoScreen() {
    TftFrame frame;
    tft.fillScreen(kvxConfig.bgColor);
    char topStatus[12] = {};
    if (shareModeAp) {
        snprintf(
            topStatus, sizeof(topStatus), "%u/%u", (unsigned)WiFi.softAPgetStationNum(),
            (unsigned)kMaxClients
        );
        drawKvxTopBar("File Share", topStatus);
    } else {
        drawKvxTopBar("File Share");
    }

    const uint16_t labelCol = kvxConfig.priColor;
    const uint16_t green = TFT_GREEN;
    const uint16_t bg = kvxConfig.bgColor;
    const int padX = 6;
    const int body = FP;
    int line = 0;

    auto labelVal = [&](const char *label, const String &val, bool valGreen) {
        int y = shareLineY(line++);
        tft.setTextSize(body);
        tft.setTextColor(labelCol, bg);
        tft.setCursor(padX, y);
        tft.print(label);
        tft.setTextColor(valGreen ? green : labelCol, bg);
        String clipped = val;
        int maxChars = max(4, (tftWidth - padX) / (body * LW) - (int)strlen(label));
        if ((int)clipped.length() > maxChars) clipped = clipped.substring(0, maxChars);
        tft.print(clipped);
    };

    if (shareModeAp) {
        labelVal("SSID: ", kvxConfig.fileShare.ssid, true);
        labelVal("AP: ", maskPwd(kvxConfig.fileShare.pwd, sharePwdVisible), sharePwdVisible);
    } else {
        String net = WiFi.SSID();
        if (net.length() == 0) net = "(wifi)";
        labelVal("Net: ", net, true);
    }

    labelVal("User: ", kvxConfig.fileShare.user, true);
    labelVal("Pwd: ", maskPwd(kvxConfig.fileShare.portalPwd, sharePwdVisible), sharePwdVisible);

    int y = shareLineY(line++);
    tft.setTextSize(FM);
    tft.setTextColor(green, bg);
    tft.setCursor(padX, y);
    tft.print(shareDisplayIp());

    y = shareLineY(line, 1);
    tft.setTextSize(1);
    tft.setTextColor(labelCol, bg);
    tft.setCursor(padX, y);
    String last = lastTransferLine();
    if ((int)last.length() > 38) last = last.substring(0, 38);
    tft.print(last);

    tft.setTextColor(TFT_RED, bg);
    const char *foot = shareModeAp ? "Spc pwd  Next QR  ESC" : "Spc pwd  Next URL  ESC";
    tft.drawCentreString(foot, tftWidth / 2, uiFooterY(1), 1);
}

static void drawQrScreen(const String &payload) {
#ifdef HAS_SCREEN
    QRcode qrcode(&tft);
    qrcode.init();
    qrcode.create(payload, shareQrInvert);
#else
    (void)payload;
#endif
}

static void runShareLoop() {
    sharePwdVisible = false;
    shareScreen = 0;
    shareQrInvert = true;
    uint32_t lastSeq = shareEvent.seq;
    uint8_t lastClients = 255;
    bool needDraw = true;

    EscPress = false;
    NextPress = false;
    SelPress = false;
    KeyStroke.Clear();

    while (!returnToMenu && !forceHome) {
        if (shareDnsRunning) shareDns.processNextRequest();

        if (check(EscPress)) {
            if (shareUploadBusy) {
                {
                    TftFrame frame;
                    tft.fillScreen(kvxConfig.bgColor);
                    drawKvxTopBar("File Share");
                    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                    tft.setTextSize(FP);
                    tft.setCursor(8, shareLineY(1));
                    tft.print("Upload in progress");
                    tft.setCursor(8, shareLineY(2));
                    tft.print("Sel=stop  ESC=cancel");
                }
                EscPress = false;
                SelPress = false;
                bool decided = false;
                bool doStop = false;
                while (!decided && !returnToMenu && !forceHome) {
                    if (shareDnsRunning) shareDns.processNextRequest();
                    if (check(SelPress)) {
                        doStop = true;
                        decided = true;
                    }
                    if (check(EscPress)) {
                        doStop = false;
                        decided = true;
                    }
                    vTaskDelay(pdMS_TO_TICKS(40));
                }
                if (!doStop) {
                    needDraw = true;
                    continue;
                }
            }
            break;
        }

        uint8_t clients = shareModeAp ? WiFi.softAPgetStationNum() : 0;
        if (shareEvent.seq != lastSeq || clients != lastClients) {
            lastSeq = shareEvent.seq;
            lastClients = clients;
            if (shareScreen == 0) needDraw = true;
        }

        if (check(NextPress)) {
            uint8_t screens = shareModeAp ? 3 : 2; // STA: info + URL QR only
            shareScreen = (shareScreen + 1) % screens;
            shareQrInvert = true;
            needDraw = true;
        }

        if (KeyStroke.pressed) {
            for (char c : KeyStroke.word) {
                if (c == ' ') {
                    if (shareScreen == 0) {
                        sharePwdVisible = !sharePwdVisible;
                        needDraw = true;
                    } else {
                        shareQrInvert = !shareQrInvert;
                        needDraw = true;
                    }
                    break;
                }
            }
            KeyStroke.Clear();
        }

        if (needDraw) {
            needDraw = false;
            if (shareScreen == 0) {
                drawInfoScreen();
            } else if (shareScreen == 1) {
                drawQrScreen(shareHttpUrl());
            } else if (shareModeAp) {
                String wifiQr = "WIFI:T:WPA;S:" + kvxConfig.fileShare.ssid + ";P:" +
                                kvxConfig.fileShare.pwd + ";;";
                drawQrScreen(wifiQr);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(40));
    }

    stopShareServer();
    if (shareModeAp || shareOwnedWifi) wifiDisconnect();
}

static void setSsidInteractive() {
    String ssid = keyboard(kvxConfig.fileShare.ssid, 32, "AP SSID:");
    if (ssid == "\x1B") return;
    ssid.trim();
    if (ssid.length() == 0) {
        displayError("SSID cannot be empty", true);
        return;
    }
    kvxConfig.fileShare.ssid = ssid;
    saveShareConfig();
}

static void setApPwdInteractive() {
    String pwd = keyboard(kvxConfig.fileShare.pwd, 63, "AP Password:", true);
    if (pwd == "\x1B") return;
    if (pwd.length() < 8) {
        displayError("Password min 8 chars", true);
        return;
    }
    kvxConfig.fileShare.pwd = pwd;
    saveShareConfig();
}

static void setPortalUserInteractive() {
    String user = keyboard(kvxConfig.fileShare.user, 32, "Portal user:");
    if (user == "\x1B") return;
    user.trim();
    if (user.length() == 0) {
        displayError("User cannot be empty", true);
        return;
    }
    kvxConfig.fileShare.user = user;
    saveShareConfig();
}

static void setPortalPwdInteractive() {
    String pwd = keyboard(kvxConfig.fileShare.portalPwd, 63, "Portal Password:", true);
    if (pwd == "\x1B") return;
    if (pwd.length() < 4) {
        displayError("Portal pwd min 4 chars", true);
        return;
    }
    kvxConfig.fileShare.portalPwd = pwd;
    saveShareConfig();
}

static void toggleReadOnly() {
    kvxConfig.fileShare.readOnly = kvxConfig.fileShare.readOnly ? 0 : 1;
    saveShareConfig();
}

static bool beginShareServer() {
    // Prefer internal RAM — AsyncTCP is unreliable when the server object lives in PSRAM.
    shareServer = (AsyncWebServer *)heap_caps_malloc(
        sizeof(AsyncWebServer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (!shareServer) shareServer = (AsyncWebServer *)malloc(sizeof(AsyncWebServer));
    if (!shareServer) {
        displayError("No mem for server", true);
        return false;
    }
    new (shareServer) AsyncWebServer(80);
    configureShareRoutes();

    if (MDNS.begin("kvxputer")) {
        MDNS.addService("http", "tcp", 80);
        shareMdns = true;
    }
    Serial.printf(
        "[FileShare] listening %s dns=%d\n", shareHttpUrl().c_str(), (int)shareDnsRunning
    );
    return true;
}

static bool startShareSta() {
    cleanlyStopWebUiForWiFiFeature();
    if (server) {
        displayError("WebUI still active", true);
        return false;
    }

    shareModeAp = false;
    shareOwnedWifi = false;

    if (WiFi.AP.started()) {
        wifiDisconnect();
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (WiFi.isConnected()) {
        shareOwnedWifi = false;
    } else {
        if (!wifiConnectMenu(WIFI_STA)) return false;
        if (!WiFi.isConnected()) {
            displayError("WiFi not connected", true);
            return false;
        }
        shareOwnedWifi = true;
    }

    wifiConnected = true;
    wifiIP = WiFi.localIP().toString();
    esp_wifi_set_ps(WIFI_PS_NONE);
    return true;
}

static void startFileShareAp() {
    shareModeAp = true;
    shareOwnedWifi = true;
    if (!startShareAp()) return;
    if (!beginShareServer()) {
        wifiDisconnect();
        return;
    }
    runShareLoop();
}

static void startFileShareSta() {
    if (!startShareSta()) return;
    if (!beginShareServer()) {
        if (shareOwnedWifi) wifiDisconnect();
        return;
    }
    runShareLoop();
}

static void startFileShare() {
    options = {
        {"Create AP", startFileShareAp},
        {"Use WiFi", startFileShareSta},
        {"Back", []() {}},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "File Share mode");
}

} // namespace

void fileShareMenu() {
    options = {
        {"Start", startFileShare},
        {"Set AP SSID", setSsidInteractive},
        {"Set AP password", setApPwdInteractive},
        {"Set portal user", setPortalUserInteractive},
        {"Set portal pwd", setPortalPwdInteractive},
        {kvxConfig.fileShare.readOnly ? "Read-only: ON" : "Read-only: OFF",
         []() {
             toggleReadOnly();
             fileShareMenu();
         }},
        {"Back", []() {}},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "File Share");
}
