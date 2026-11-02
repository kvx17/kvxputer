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
#include "root/ui/scrollableTextArea.h"
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp32-hal-psram.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdarg>
#include <globals.h>
#include <vector>

#ifndef FILESHARE_DIAG
#define FILESHARE_DIAG 1
#endif

namespace {

static constexpr uint8_t kMaxClients = 4;
static constexpr size_t kNameMax = 48;
static constexpr size_t kLogCap = 48;
static constexpr size_t kLogLine = 52;
static constexpr size_t kUploadUiStride = 16 * 1024;
static constexpr size_t kListPageDefault = 200;
static constexpr size_t kListPageMax = 500;
static constexpr uint32_t kFsCacheIntervalMs = 5000;
static constexpr uint32_t kUploadStaleMs = 60000;
static constexpr uint8_t kShareSdMaxFiles = 6;

static AsyncWebServer *shareServer = nullptr;
static DNSServer shareDns;
static bool shareDnsRunning = false;
static bool shareMdns = false;
static bool shareModeAp = true;      // true = softAP, false = STA on existing WiFi
static bool shareOwnedWifi = false;  // we brought WiFi up for this session
static bool sharePwdVisible = false;
static bool shareUploadBusy = false;
static bool shareUploadActive = false;
static uint8_t shareScreen = 0; // 0 info, 1 url QR, 2 wifi QR (AP only)
static bool shareQrInvert = true;
static bool shareCorsAdded = false;

static volatile size_t shareUploadReceived = 0;
static volatile size_t shareUploadTotal = 0; // 0 = unknown
static volatile uint32_t shareUiSeq = 0;
static char shareUploadName[kNameMax] = {};
static size_t shareUploadUiMark = 0;
static uint32_t shareUploadGen = 0;
static uint32_t shareUploadLastProgressMs = 0;

static char shareLogLines[kLogCap][kLogLine];
static uint8_t shareLogCount = 0;
static uint8_t shareLogNext = 0;
static portMUX_TYPE shareMux = portMUX_INITIALIZER_UNLOCKED;

struct FsSizeCache {
    bool sdOk = false;
    uint64_t sdFree = 0;
    uint64_t sdUsed = 0;
    uint64_t sdTotal = 0;
    uint64_t lfsFree = 0;
    uint64_t lfsUsed = 0;
    uint64_t lfsTotal = 0;
    uint32_t lastMs = 0;
};
static FsSizeCache shareFsCache;

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
static File shareUploadFile;
static bool shareUploadFailed = false;
static int shareUploadFailCode = 500; // 409 busy, 507 no space, else 500

#if FILESHARE_DIAG
static void shareDiag(const char *where) {
    UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf(
        "[FileShare][diag] %s stackHWM=%u heap=%u largest=%u\n",
        where,
        (unsigned)stack,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    );
}
#define SHARE_DIAG(w) shareDiag(w)
#else
#define SHARE_DIAG(w) ((void)0)
#endif

static void closeShareUploadFile() {
    if (shareUploadFile) {
        shareUploadFile.close();
        shareUploadFile = File();
    }
}

struct TransferEvent {
    uint32_t seq = 0;
    char direction = 0; // 'U' or 'D'
    char name[kNameMax] = {};
    bool ok = false;
};
static TransferEvent shareEvent;

static void appendShareLog(const char *msg) {
    if (!msg || !msg[0]) return;
    portENTER_CRITICAL(&shareMux);
    snprintf(shareLogLines[shareLogNext], kLogLine, "%s", msg);
    shareLogNext = (shareLogNext + 1) % kLogCap;
    if (shareLogCount < kLogCap) shareLogCount++;
    shareUiSeq = shareUiSeq + 1;
    portEXIT_CRITICAL(&shareMux);
}

static void appendShareLogf(const char *fmt, ...) {
    char buf[kLogLine];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    appendShareLog(buf);
}

static void noteTransfer(char direction, const String &name, bool ok) {
    portENTER_CRITICAL(&shareMux);
    shareEvent.direction = direction;
    shareEvent.ok = ok;
    size_t n = name.length();
    if (n >= kNameMax) n = kNameMax - 1;
    memcpy(shareEvent.name, name.c_str(), n);
    shareEvent.name[n] = 0;
    shareEvent.seq = shareEvent.seq + 1;
    shareUiSeq = shareUiSeq + 1;
    portEXIT_CRITICAL(&shareMux);
}

static void resetUploadProgress() {
    shareUploadReceived = 0;
    shareUploadTotal = 0;
    shareUploadUiMark = 0;
    portENTER_CRITICAL(&shareMux);
    shareUploadName[0] = 0;
    portEXIT_CRITICAL(&shareMux);
}

static void bumpUploadProgress(size_t received, size_t total, bool force = false) {
    shareUploadReceived = received;
    if (total) shareUploadTotal = total;
    shareUploadLastProgressMs = millis();
    if (force || received >= shareUploadUiMark + kUploadUiStride ||
        (shareUploadTotal && received >= shareUploadTotal)) {
        shareUploadUiMark = received;
        shareUiSeq = shareUiSeq + 1;
    }
}

static void setShareUploadName(const String &name) {
    size_t n = name.length();
    if (n >= kNameMax) n = kNameMax - 1;
    portENTER_CRITICAL(&shareMux);
    memcpy(shareUploadName, name.c_str(), n);
    shareUploadName[n] = 0;
    portEXIT_CRITICAL(&shareMux);
}

static String getShareUploadNameCopy() {
    char tmp[kNameMax];
    portENTER_CRITICAL(&shareMux);
    memcpy(tmp, shareUploadName, kNameMax);
    portEXIT_CRITICAL(&shareMux);
    return String(tmp);
}

static String humanSize(uint64_t bytes);

static void abortShareUpload(const char *reason, bool removePartial) {
    if (!shareUploadActive && !shareUploadBusy) return;
    size_t got = shareUploadReceived;
    String name = getShareUploadNameCopy();
    if (name.length() == 0 && shareUploadPath.length()) {
        int slash = shareUploadPath.lastIndexOf('/');
        name = slash >= 0 ? shareUploadPath.substring(slash + 1) : shareUploadPath;
    }
    closeShareUploadFile();
    if (removePartial && shareUploadFs && shareUploadPath.length()) {
        shareUploadFs->remove(shareUploadPath);
    }
    appendShareLogf("UP abort %s @%s", reason ? reason : "?", humanSize(got).c_str());
    noteTransfer('U', name.length() ? name : String("(upload)"), false);
    shareUploadBusy = false;
    shareUploadActive = false;
    shareUploadFailed = false;
    shareUploadFailCode = 500;
    shareUploadFs = nullptr;
    shareUploadPath = "";
    resetUploadProgress();
    shareUploadGen++;
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
        if (!setupSdCard(kShareSdMaxFiles)) {
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

static void escapeJsonTo(Print &out, const String &s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') out.print('\\');
        out.print(c);
    }
}

static String entryBaseName(const String &fullPath) {
    int slash = fullPath.lastIndexOf('/');
    if (slash < 0) return fullPath;
    return fullPath.substring(slash + 1);
}

// Iterative delete — avoids recursive stack growth on the AsyncTCP task.
static bool deleteSharePathIter(FS &fs, const String &path) {
    File probe = fs.open(path);
    if (!probe) return false;
    if (!probe.isDirectory()) {
        probe.close();
        return fs.remove(path);
    }
    probe.close();

    std::vector<String> pending;
    std::vector<String> dirsToRemove;
    pending.reserve(16);
    dirsToRemove.reserve(16);
    pending.push_back(path);

    while (!pending.empty()) {
        String cur = pending.back();
        pending.pop_back();
        dirsToRemove.push_back(cur);

        File dir = fs.open(cur);
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            continue;
        }
        File e = dir.openNextFile();
        while (e) {
            const char *cp = e.path();
            String child = cp ? String(cp) : String();
            bool isDir = e.isDirectory();
            e.close();
            if (child.length() == 0) {
                e = dir.openNextFile();
                continue;
            }
            if (isDir) {
                pending.push_back(child);
            } else if (!fs.remove(child)) {
                dir.close();
                return false;
            }
            e = dir.openNextFile();
            delay(0);
        }
        dir.close();
    }

    for (int i = (int)dirsToRemove.size() - 1; i >= 0; --i) {
        if (!fs.rmdir(dirsToRemove[(size_t)i]) && fs.exists(dirsToRemove[(size_t)i])) {
            return false;
        }
    }
    return !fs.exists(path);
}

static void refreshFsSizeCache(bool force = false) {
    uint32_t now = millis();
    if (!force && shareFsCache.lastMs != 0 && (now - shareFsCache.lastMs) < kFsCacheIntervalMs) {
        return;
    }
    SHARE_DIAG("fsCache.begin");
    shareFsCache.lfsTotal = LittleFS.totalBytes();
    shareFsCache.lfsUsed = LittleFS.usedBytes();
    shareFsCache.lfsFree =
        shareFsCache.lfsTotal > shareFsCache.lfsUsed ? shareFsCache.lfsTotal - shareFsCache.lfsUsed : 0;

    // Remount with enough handles if needed (setupSdCard early-outs when already mounted).
    if (sdcardMounted) {
        // already mounted — assume handle count is OK once remounted at share start
        shareFsCache.sdOk = true;
    } else {
        shareFsCache.sdOk = setupSdCard(kShareSdMaxFiles);
    }
    if (shareFsCache.sdOk) {
        shareFsCache.sdTotal = SD.totalBytes();
        shareFsCache.sdUsed = SD.usedBytes();
        shareFsCache.sdFree =
            shareFsCache.sdTotal > shareFsCache.sdUsed ? shareFsCache.sdTotal - shareFsCache.sdUsed : 0;
    } else {
        shareFsCache.sdFree = shareFsCache.sdUsed = shareFsCache.sdTotal = 0;
    }
    shareFsCache.lastMs = now;
    SHARE_DIAG("fsCache.end");
}

static bool ensureShareSdHandles() {
    // Remount so maxFiles=kShareSdMaxFiles even if already mounted with the default of 3.
    if (sdcardMounted) closeSdCard();
    bool ok = setupSdCard(kShareSdMaxFiles);
    Serial.printf("[FileShare] SD remount maxFiles=%u ok=%d\n", (unsigned)kShareSdMaxFiles, (int)ok);
    return ok;
}

static void stopShareServer() {
    if (shareDnsRunning) {
        shareDns.stop();
        shareDnsRunning = false;
    }
    // Close upload first so async handlers don't write into a half-torn-down server.
    closeShareUploadFile();
    shareUploadBusy = false;
    shareUploadActive = false;
    shareUploadFs = nullptr;
    shareUploadPath = "";
    shareUploadFailed = false;
    shareUploadFailCode = 500;
    resetUploadProgress();
    shareUploadGen++;

    if (shareServer) {
        shareServer->end();
        // Brief pause so in-flight AsyncTCP work can drain before destructor.
        vTaskDelay(pdMS_TO_TICKS(100));
        shareServer->~AsyncWebServer();
        free(shareServer);
        shareServer = nullptr;
    }
    if (shareMdns) {
        MDNS.end();
        shareMdns = false;
    }
    shareSessions.clear();
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

    String ssid = kvxConfig.fileShare.ssid;
    String pwd = kvxConfig.fileShare.pwd;
    ssid.trim();
    if (ssid.length() == 0) ssid = "kvxputer";
    if (pwd.length() < 8) pwd = "kvxputer";

    // 172.0.0.1 avoids colliding with home routers at 192.168.1.1. Shared helper
    // puts the AP IP in the DHCP DNS field and recovers DHCPS if captive-portal
    // option setup fails (that failure mode left phones on 169.254.*).
    IPAddress apGw(172, 0, 0, 1);
    uint8_t ch = kvxConfig.fileShare.channel;
    if (ch < 1 || ch > 13) ch = 6;
    if (!wifiStartPortalAp(ssid, pwd, ch, apGw, WIFI_AP)) {
        displayError("AP start failed", true);
        wifiDisconnect();
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(400));

    IPAddress dnsIp = WiFi.softAPIP();
    if (dnsIp == IPAddress((uint32_t)0)) dnsIp = apGw;
    shareDns.setErrorReplyCode(DNSReplyCode::NoError);
    shareDnsRunning = shareDns.start(53, "*", dnsIp);
    if (!shareDnsRunning) {
        Serial.println("[FileShare] DNS start failed — retry");
        vTaskDelay(pdMS_TO_TICKS(100));
        shareDnsRunning = shareDns.start(53, "*", dnsIp);
    }
    if (!shareDnsRunning) Serial.println("[FileShare] DNS optional start failed");
    Serial.printf("[FileShare] AP '%s' URL http://%s/\n", ssid.c_str(), dnsIp.toString().c_str());
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
    if (!shareCorsAdded) {
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        shareCorsAdded = true;
    }

    if (shareModeAp) addCaptiveProbeRoutes();

    shareServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) { sendSharePage(request); });

    // Plain probe so a browser/curl can confirm the HTTP stack is up.
    shareServer->on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "kvxputer-file-share");
    });

    shareServer->on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        SHARE_DIAG("login");
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
        SHARE_DIAG("status");
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        // Cached sizes only — never call SD.usedBytes()/totalBytes() on AsyncTCP stack.
        String out;
        out.reserve(320);
        out += "{\"readOnly\":";
        out += kvxConfig.fileShare.readOnly ? "true" : "false";
        out += ",\"clients\":";
        out += String(shareModeAp ? WiFi.softAPgetStationNum() : 0);
        out += ",\"sd\":";
        if (shareFsCache.sdOk) {
            out += "{\"free\":\"";
            out += humanSize(shareFsCache.sdFree);
            out += "\",\"used\":\"";
            out += humanSize(shareFsCache.sdUsed);
            out += "\",\"total\":\"";
            out += humanSize(shareFsCache.sdTotal);
            out += "\"}";
        } else {
            out += "null";
        }
        out += ",\"lfs\":{\"free\":\"";
        out += humanSize(shareFsCache.lfsFree);
        out += "\",\"used\":\"";
        out += humanSize(shareFsCache.lfsUsed);
        out += "\",\"total\":\"";
        out += humanSize(shareFsCache.lfsTotal);
        out += "\"}}";
        request->send(200, "application/json", out);
        SHARE_DIAG("status.done");
    });

    shareServer->on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        SHARE_DIAG("list");
        if (!checkShareAuth(request)) {
            sendUnauthorized(request);
            return;
        }
        String vol = request->hasParam("vol") ? request->getParam("vol")->value() : "sd";
        String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
        size_t offset = 0;
        size_t limit = kListPageDefault;
        if (request->hasParam("offset")) offset = (size_t)request->getParam("offset")->value().toInt();
        if (request->hasParam("limit")) limit = (size_t)request->getParam("limit")->value().toInt();
        if (limit == 0) limit = kListPageDefault;
        if (limit > kListPageMax) limit = kListPageMax;

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

        AsyncResponseStream *response = request->beginResponseStream("application/json", 3072);
        if (!response) {
            root.close();
            request->send(500, "text/plain", "list alloc failed");
            return;
        }

        String showPath = path;
        if (showPath.length() == 0) showPath = "/";
        response->print("{\"path\":\"");
        escapeJsonTo(*response, showPath);
        response->printf("\",\"offset\":%u,\"limit\":%u,\"items\":[", (unsigned)offset, (unsigned)limit);

        size_t seen = 0;
        size_t emitted = 0;
        bool first = true;
        bool hasMore = false;
        File entry = root.openNextFile();
        while (entry) {
            if (esp_get_free_heap_size() < 6144) {
                hasMore = true;
                entry.close();
                break;
            }
            bool isDir = entry.isDirectory();
            const char *ep = entry.path();
            String fullPath = ep ? String(ep) : String();
            String name = entryBaseName(fullPath);
            uint64_t sz = isDir ? 0 : (uint64_t)entry.size();
            time_t mtime = entry.getLastWrite();
            entry.close();

            if (name.length() == 0) {
                entry = root.openNextFile();
                continue;
            }

            if (seen < offset) {
                seen++;
                entry = root.openNextFile();
                delay(0);
                continue;
            }
            if (emitted >= limit) {
                hasMore = true;
                break;
            }

            if (!first) response->print(',');
            first = false;
            response->print("{\"name\":\"");
            escapeJsonTo(*response, name);
            response->print("\",\"dir\":");
            response->print(isDir ? "true" : "false");
            response->print(",\"size\":");
            response->print((unsigned long)sz);
            response->print(",\"mtime\":");
            response->print((unsigned long)mtime);
            response->print('}');
            emitted++;
            seen++;
            entry = root.openNextFile();
            delay(0);
        }
        root.close();
        response->print("],\"hasMore\":");
        response->print(hasMore ? "true" : "false");
        response->print('}');
        request->send(response);
        SHARE_DIAG("list.done");
    });

    shareServer->on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        SHARE_DIAG("download");
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
        File f = fs->open(abs, "r");
        if (!f || f.isDirectory()) {
            if (f) f.close();
            request->send(400, "text/plain", "not a file");
            return;
        }
        size_t sz = f.size();
        // Pass open File into response — path is only used for Content-Disposition.
        String base = abs.substring(abs.lastIndexOf('/') + 1);
        AsyncWebServerResponse *response =
            request->beginResponse(f, abs, "application/octet-stream", true);
        if (!response) {
            f.close();
            request->send(500, "text/plain", "download failed");
            return;
        }
        response->addHeader("Connection", "close");
        response->addHeader("Cache-Control", "no-store");
        appendShareLogf("DN start %s (%s)", base.c_str(), humanSize(sz).c_str());
        noteTransfer('D', base, true);
        request->send(response);
        SHARE_DIAG("download.sent");
    });

    auto denyIfReadOnly = [](AsyncWebServerRequest *request) -> bool {
        if (kvxConfig.fileShare.readOnly) {
            request->send(403, "text/plain", "read-only");
            return true;
        }
        return false;
    };

    shareServer->on("/api/mkdir", HTTP_POST, [denyIfReadOnly](AsyncWebServerRequest *request) {
        SHARE_DIAG("mkdir");
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
        SHARE_DIAG("rename");
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
        SHARE_DIAG("delete");
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
        if (!fs->exists(full) || !deleteSharePathIter(*fs, full)) {
            request->send(500, "text/plain", "delete failed");
            return;
        }
        request->send(200, "text/plain", "ok");
        SHARE_DIAG("delete.done");
    });

    shareServer->on(
        "/api/upload",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            SHARE_DIAG("upload.done");
            closeShareUploadFile();
            size_t got = shareUploadReceived;
            if (shareUploadFailed) {
                int code = shareUploadFailCode;
                String msg = "upload failed";
                if (code == 409) msg = "upload busy";
                else if (code == 507) msg = "no space";
                else if (code == 403) msg = "read-only";
                else if (code == 401) msg = "Unauthorized";
                else if (code == 400) msg = "bad path";
                String uname = getShareUploadNameCopy();
                appendShareLogf("UP fail %s @%s", uname.c_str(), humanSize(got).c_str());
                noteTransfer(
                    'U', shareUploadPath.length() ? shareUploadPath : uname, false
                );
                // Remove partial on failure (except busy, which never opened a file).
                if (code != 409 && shareUploadFs && shareUploadPath.length()) {
                    shareUploadFs->remove(shareUploadPath);
                }
                shareUploadBusy = false;
                shareUploadActive = false;
                shareUploadFailed = false;
                shareUploadFailCode = 500;
                shareUploadFs = nullptr;
                shareUploadPath = "";
                resetUploadProgress();
                shareUploadGen++;
                request->send(code, "text/plain", msg);
                return;
            }
            String base = shareUploadPath;
            int slash = base.lastIndexOf('/');
            if (slash >= 0) base = base.substring(slash + 1);
            if (base.length() == 0) base = getShareUploadNameCopy();
            appendShareLogf("UP done %s (%s)", base.c_str(), humanSize(got).c_str());
            noteTransfer('U', base, true);
            shareUploadBusy = false;
            shareUploadActive = false;
            shareUploadFs = nullptr;
            shareUploadPath = "";
            resetUploadProgress();
            shareUploadGen++;
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
                SHARE_DIAG("upload.start");
                if (shareUploadBusy || shareUploadActive) {
                    failUpload(409);
                    return;
                }
                String vol = request->hasParam("vol", true) ? request->getParam("vol", true)->value()
                             : request->hasParam("vol")    ? request->getParam("vol")->value()
                                                           : "";
                String path = request->hasParam("path", true) ? request->getParam("path", true)->value()
                              : request->hasParam("path")    ? request->getParam("path")->value()
                                                             : "/";
                FS *fs = nullptr;
                String abs;
                if (!resolveSharePath(vol, path, fs, abs, nullptr)) {
                    failUpload(400);
                    return;
                }
                // Strip any client-supplied directory from multipart filename.
                int slashInName = filename.lastIndexOf('/');
                if (slashInName >= 0) filename = filename.substring(slashInName + 1);
                slashInName = filename.lastIndexOf('\\');
                if (slashInName >= 0) filename = filename.substring(slashInName + 1);
                if (filename == "." || filename == ".." || filename.length() == 0) {
                    failUpload(400);
                    return;
                }
                // Never call SD.usedBytes()/totalBytes() here — they walk the FAT from the
                // AsyncTCP task and crash. LittleFS metadata is cheap; SD uses the cache.
                if (request->contentLength() > 0 && fs == &LittleFS) {
                    if (request->contentLength() > freeBytes(*fs, false)) {
                        failUpload(507);
                        return;
                    }
                } else if (request->contentLength() > 0 && fs == &SD && shareFsCache.sdOk) {
                    if (request->contentLength() > shareFsCache.sdFree) {
                        failUpload(507);
                        return;
                    }
                }
                String unique = uniqueUploadName(*fs, abs, filename);
                String full = (abs == "/") ? ("/" + unique) : (abs + "/" + unique);
                shareUploadBusy = true;
                shareUploadActive = true;
                shareUploadFailed = false;
                shareUploadFailCode = 500;
                shareUploadFs = fs;
                shareUploadPath = full;
                setShareUploadName(unique);
                size_t total = 0;
                if (request->hasParam("size")) {
                    total = (size_t)request->getParam("size")->value().toInt();
                } else if (request->hasParam("size", true)) {
                    total = (size_t)request->getParam("size", true)->value().toInt();
                }
                resetUploadProgress();
                shareUploadTotal = total;
                shareUploadLastProgressMs = millis();
                appendShareLogf(
                    "UP start %s%s%s",
                    unique.c_str(),
                    total ? " /" : "",
                    total ? humanSize(total).c_str() : ""
                );
                closeShareUploadFile();
                shareUploadFile = fs->open(full, FILE_WRITE);
                if (!shareUploadFile) {
                    failUpload(500);
                    shareUploadBusy = false;
                    shareUploadActive = false;
                    shareUploadFs = nullptr;
                    shareUploadPath = "";
                    resetUploadProgress();
                    return;
                }

                shareUploadGen++;
                uint32_t gen = shareUploadGen;
                // POD tag freed by AsyncWebServerRequest destructor via free().
                auto *tag = (uint32_t *)malloc(sizeof(uint32_t));
                if (tag) {
                    *tag = gen;
                    request->_tempObject = tag;
                }
                request->onDisconnect([gen]() {
                    if (shareUploadActive && shareUploadGen == gen) {
                        abortShareUpload("disconnect", true);
                    }
                });
                bumpUploadProgress(0, total, true);
            }
            if (shareUploadFailed) return;
            if (len) {
                if (!shareUploadFile) {
                    failUpload(500);
                    return;
                }
                size_t written = shareUploadFile.write(data, len);
                if (written != len) {
                    closeShareUploadFile();
                    failUpload(500);
                    return;
                }
            }
            // index/len are file-part bytes only (not multipart overhead).
            size_t got = index + len;
            size_t total = shareUploadTotal;
            if (final) {
                if (!total || got > total) total = got;
                bumpUploadProgress(got, total, true);
                appendShareLogf(
                    "UP write %s (%s)", getShareUploadNameCopy().c_str(), humanSize(got).c_str()
                );
                closeShareUploadFile();
            } else {
                bumpUploadProgress(got, total, false);
            }
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
    TransferEvent ev;
    portENTER_CRITICAL(&shareMux);
    ev = shareEvent;
    portEXIT_CRITICAL(&shareMux);
    if (ev.seq == 0 || ev.direction == 0) return "Last: -";
    String d = (ev.direction == 'U') ? "UP " : "DN ";
    return "Last: " + d + String(ev.name) + (ev.ok ? " ok" : " fail");
}

static int shareLineY(int line) {
    const int sz = uiBodyFont();
    return KVX_TOPBAR_H + 2 + line * (uiLineH(sz) + 2);
}

static void drawShareProgressBar(int x, int y, int w, int h, size_t got, size_t total) {
    const uint16_t border = kvxConfig.priColor;
    const uint16_t fill = kvxConfig.secColor;
    const uint16_t bg = kvxConfig.bgColor;
    tft.drawRect(x, y, w, h, border);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, bg);
    int inner = w - 4;
    if (inner <= 0) return;
    int fillW = 0;
    if (total > 0) {
        fillW = (int)((got * (size_t)inner) / total);
        if (fillW > inner) fillW = inner;
    } else if (got > 0) {
        // Unknown total: bounce a pulse based on bytes received.
        fillW = (int)((got / 4096) % (size_t)inner);
        if (fillW < 8) fillW = 8;
        if (fillW > inner) fillW = inner;
    }
    if (fillW > 0) tft.fillRect(x + 2, y + 2, fillW, h - 4, fill);
}

static void sharePrintClipped(int x, int y, const String &text, int maxPx) {
    tft.setCursor(x, y);
    if (maxPx <= 0) {
        tft.print(text);
        return;
    }
    if (tft.textWidth(text) <= maxPx) {
        tft.print(text);
        return;
    }
    String s = text;
    while (s.length() > 1 && tft.textWidth(s + "..") > maxPx) s.remove(s.length() - 1);
    tft.print(s);
    tft.print("..");
}

static void showShareLogs() {
    ScrollableTextArea area("Share Logs");
    if (shareLogCount == 0) {
        area.addLine("(no transfers yet)");
    } else {
        char lines[kLogCap][kLogLine];
        uint8_t count = 0;
        uint8_t next = 0;
        portENTER_CRITICAL(&shareMux);
        count = shareLogCount;
        next = shareLogNext;
        memcpy(lines, shareLogLines, sizeof(lines));
        portEXIT_CRITICAL(&shareMux);
        const uint8_t start = (count < kLogCap) ? 0 : next;
        for (uint8_t i = 0; i < count; i++) {
            uint8_t idx = (start + i) % kLogCap;
            area.addLine(lines[idx]);
        }
        area.scrollToLine(area.getMaxLines());
    }
    // Keep captive DNS alive while browsing logs.
    area.draw(true);
    EscPress = false;
    while (!returnToMenu && !forceHome) {
        if (shareDnsRunning) shareDns.processNextRequest();
        if (check(EscPress)) break;
        area.updateSoft();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
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
    const int body = uiBodyFont();
    const int footY = uiFooterY(body);
    const int maxTextPx = tftWidth - padX - 4;
    int line = 0;

    tft.setTextSize(body);

    auto labelVal = [&](const char *label, const String &val, bool valGreen) {
        int y = shareLineY(line++);
        if (y + uiLineH(body) > footY - 2) return;
        tft.setTextColor(labelCol, bg);
        tft.setCursor(padX, y);
        tft.print(label);
        int labelPx = tft.textWidth(label);
        tft.setTextColor(valGreen ? green : labelCol, bg);
        sharePrintClipped(padX + labelPx, y, val, maxTextPx - labelPx);
    };

    if (shareUploadBusy) {
        // Compact layout so progress fits without overlapping the footer.
        if (shareModeAp) labelVal("SSID: ", kvxConfig.fileShare.ssid, true);
        else {
            String net = WiFi.SSID();
            if (net.length() == 0) net = "(wifi)";
            labelVal("Net: ", net, true);
        }
        labelVal("URL: ", shareHttpUrl(), true);

        size_t got = shareUploadReceived;
        size_t total = shareUploadTotal;
        int y = shareLineY(line++);
        if (y + uiLineH(body) <= footY - 2) {
            tft.setTextColor(labelCol, bg);
            const char *upLab = "UP: ";
            tft.setCursor(padX, y);
            tft.print(upLab);
            String name = getShareUploadNameCopy();
            if (name.length() == 0) name = "upload";
            sharePrintClipped(padX + tft.textWidth(upLab), y, name, maxTextPx - tft.textWidth(upLab));
        }

        const int barH = max(8, uiLineH(body) - 4);
        y = shareLineY(line++);
        if (y + barH <= footY - 2) {
            drawShareProgressBar(padX, y + 1, tftWidth - padX * 2, barH, got, total);
        }

        y = shareLineY(line++);
        if (y + uiLineH(body) <= footY - 2) {
            tft.setTextColor(green, bg);
            if (total > 0) {
                int pct = (int)((got * 100ULL) / total);
                if (pct > 100) pct = 100;
                char pctLine[48];
                snprintf(
                    pctLine, sizeof(pctLine), "%d%% %s/%s", pct, humanSize(got).c_str(),
                    humanSize(total).c_str()
                );
                sharePrintClipped(padX, y, pctLine, maxTextPx);
            } else {
                sharePrintClipped(padX, y, humanSize(got) + " received", maxTextPx);
            }
        }
    } else {
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
        labelVal("URL: ", shareHttpUrl(), true);
        labelVal("", lastTransferLine(), false);
    }

    tft.setTextSize(body);
    tft.setTextColor(TFT_RED, bg);
    const char *foot =
        shareModeAp ? "Spc pwd  Sel logs  Next QR  ESC" : "Spc pwd  Sel logs  Next URL  ESC";
    tft.drawCentreString(foot, tftWidth / 2, footY, 1);
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
    uint32_t lastUiSeq = shareUiSeq;
    uint8_t lastClients = 255;
    bool needDraw = true;
    bool wasUploading = false;
    refreshFsSizeCache(true);

    EscPress = false;
    NextPress = false;
    SelPress = false;
    KeyStroke.Clear();

    while (!returnToMenu && !forceHome) {
        if (shareDnsRunning) shareDns.processNextRequest();
        refreshFsSizeCache(false);

        // Stale upload fallback when the client vanishes without a clean disconnect.
        if (shareUploadActive && shareUploadLastProgressMs != 0 &&
            (millis() - shareUploadLastProgressMs) > kUploadStaleMs) {
            abortShareUpload("stale", true);
            needDraw = true;
        }

        if (check(EscPress)) {
            if (shareUploadBusy) {
                {
                    TftFrame frame;
                    tft.fillScreen(kvxConfig.bgColor);
                    drawKvxTopBar("File Share");
                    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
                    tft.setTextSize(uiBodyFont());
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
                abortShareUpload("user-stop", true);
            }
            break;
        }

        uint8_t clients = shareModeAp ? WiFi.softAPgetStationNum() : 0;
        if (shareUiSeq != lastUiSeq || clients != lastClients ||
            shareUploadBusy != wasUploading) {
            lastUiSeq = shareUiSeq;
            lastClients = clients;
            wasUploading = shareUploadBusy;
            if (shareScreen == 0) needDraw = true;
        }

        if (check(NextPress)) {
            uint8_t screens = shareModeAp ? 3 : 2; // STA: info + URL QR only
            shareScreen = (shareScreen + 1) % screens;
            shareQrInvert = true;
            needDraw = true;
        }

        if (shareScreen == 0 && check(SelPress)) {
            showShareLogs();
            needDraw = true;
            EscPress = false;
            SelPress = false;
            KeyStroke.Clear();
            continue;
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
                if ((c == 'l' || c == 'L') && shareScreen == 0) {
                    showShareLogs();
                    needDraw = true;
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
    ensureShareSdHandles();
    refreshFsSizeCache(true);

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
    loopOptions(options, MENU_TYPE_SUBMENU, "File Share");
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
