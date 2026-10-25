#if !defined(LITE_VERSION) && defined(HAS_LORA_CAP)

#include "mesh_ui.h"
#include "mesh_radio.h"
#include "root/storage/paths.h"
#include "root/ui/display.h"
#include "root/input/mykeyboard.h"
#include "root/app/utils.h"
#include "menu/lora/LoRaRF.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <globals.h>
#include <vector>

namespace {

using mesh::MeshConfig;
using mesh::HeardNode;
using mesh::Preset;
using mesh::RxEvent;
using mesh::kMaxChatLines;
using mesh::kMaxNodes;
using mesh::kNodeInfoIntervalMs;
using mesh::kPortNodeInfo;
using mesh::kPortText;

MeshConfig gCfg;
HeardNode gNodes[kMaxNodes];
size_t gNodeCount = 0;
std::vector<String> gChat;
int gScroll = 0;
bool gDirty = true;
uint32_t gLastNodeInfoMs = 0;

static void saveMeshSettings() {
    ensureLoraSettings();
    JsonDocument doc;
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "r");
    if (file) {
        deserializeJson(doc, file);
        file.close();
    }
    doc["mesh_region"] = "EU_868";
    doc["mesh_preset"] = (int)gCfg.preset;
    doc["mesh_channel"] = gCfg.channelName;
    doc["mesh_short"] = gCfg.shortName;
    doc["mesh_long"] = gCfg.longName;
    JsonArray psk = doc["mesh_psk"].to<JsonArray>();
    psk.clear();
    for (size_t i = 0; i < gCfg.pskStoredLen; i++) psk.add(gCfg.pskStored[i]);
    kvx::paths::ensureParentDirs(LittleFS, kvx::paths::LORA_SETTINGS);
    file = LittleFS.open(kvx::paths::LORA_SETTINGS, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
}

static void loadMeshSettings() {
    ensureLoraSettings();
    gCfg = MeshConfig{};
    gCfg.nodeNum = mesh::makeNodeNumFromMac();
    snprintf(gCfg.shortName, sizeof(gCfg.shortName), "%04x", (unsigned)(gCfg.nodeNum & 0xffff));
    snprintf(gCfg.longName, sizeof(gCfg.longName), "kvxputer %04x", (unsigned)(gCfg.nodeNum & 0xffff));
    gCfg.pskStored[0] = 1;
    gCfg.pskStoredLen = 1;
    gCfg.preset = Preset::LongFast;
    gCfg.channelName[0] = '\0';

    if (!LittleFS.exists(kvx::paths::LORA_SETTINGS)) {
        saveMeshSettings();
        return;
    }
    File file = LittleFS.open(kvx::paths::LORA_SETTINGS, "r");
    if (!file) return;
    JsonDocument doc;
    deserializeJson(doc, file);
    file.close();

    if (!doc["mesh_preset"].isNull()) {
        int p = doc["mesh_preset"].as<int>();
        if (p >= 0 && p <= 3) gCfg.preset = (Preset)p;
    }
    if (doc["mesh_channel"].is<const char *>()) {
        strlcpy(gCfg.channelName, doc["mesh_channel"].as<const char *>(), sizeof(gCfg.channelName));
    }
    if (doc["mesh_short"].is<const char *>()) {
        strlcpy(gCfg.shortName, doc["mesh_short"].as<const char *>(), sizeof(gCfg.shortName));
    }
    if (doc["mesh_long"].is<const char *>()) {
        strlcpy(gCfg.longName, doc["mesh_long"].as<const char *>(), sizeof(gCfg.longName));
    }
    if (doc["mesh_psk"].is<JsonArray>()) {
        JsonArray arr = doc["mesh_psk"].as<JsonArray>();
        size_t n = 0;
        for (JsonVariant v : arr) {
            if (n >= sizeof(gCfg.pskStored)) break;
            gCfg.pskStored[n++] = (uint8_t)v.as<int>();
        }
        if (n > 0) gCfg.pskStoredLen = n;
    }
}

static int chatFont() { return uiBodyFont(); }
static int chatLineH() { return uiLineH(chatFont()) + 2; }
static int chatMaxChars() { return max(8, (tftWidth - 12) / uiCharW(chatFont())); }
static int chatContentTop() { return 28; } // just below status-bar divider
static int chatFooterH() { return uiLineH(uiDenseFont()) + 4; }
static int chatVisibleLines() {
    int h = tftHeight - chatContentTop() - chatFooterH() - 2;
    int v = h / chatLineH();
    return v < 1 ? 1 : v;
}

static void flattenChat(std::vector<String> &lines) {
    lines.clear();
    const int maxc = chatMaxChars();
    for (const String &msg : gChat) {
        if (msg.length() == 0) {
            lines.push_back("");
            continue;
        }
        for (int i = 0; i < (int)msg.length(); i += maxc) {
            lines.push_back(msg.substring(i, i + maxc));
        }
    }
}

static void clampChatScroll(int lineCount) {
    int visible = chatVisibleLines();
    int maxScroll = lineCount > visible ? lineCount - visible : 0;
    if (gScroll < 0) gScroll = 0;
    if (gScroll > maxScroll) gScroll = maxScroll;
}

static void pushChat(const String &line) {
    gChat.push_back(line);
    if (gChat.size() > kMaxChatLines) {
        gChat.erase(gChat.begin());
        if (gScroll > 0) gScroll--;
    }
    std::vector<String> lines;
    flattenChat(lines);
    int visible = chatVisibleLines();
    gScroll = ((int)lines.size() > visible) ? (int)lines.size() - visible : 0;
    gDirty = true;
}

static HeardNode *findOrAllocNode(uint32_t num) {
    for (size_t i = 0; i < gNodeCount; i++) {
        if (gNodes[i].num == num) return &gNodes[i];
    }
    if (gNodeCount < kMaxNodes) {
        gNodes[gNodeCount].num = num;
        return &gNodes[gNodeCount++];
    }
    // Evict oldest
    size_t oldest = 0;
    for (size_t i = 1; i < gNodeCount; i++) {
        if (gNodes[i].lastHeardMs < gNodes[oldest].lastHeardMs) oldest = i;
    }
    gNodes[oldest] = HeardNode{};
    gNodes[oldest].num = num;
    return &gNodes[oldest];
}

static void touchNode(uint32_t num, float snr) {
    HeardNode *n = findOrAllocNode(num);
    n->snr = snr;
    n->lastHeardMs = millis();
}

static void applyUser(uint32_t num, const mesh::DecodedUser &u, float snr) {
    HeardNode *n = findOrAllocNode(num);
    n->snr = snr;
    n->lastHeardMs = millis();
    if (u.shortName[0]) strlcpy(n->shortName, u.shortName, sizeof(n->shortName));
    if (u.longName[0]) strlcpy(n->longName, u.longName, sizeof(n->longName));
}

static const char *shortFor(uint32_t num) {
    for (size_t i = 0; i < gNodeCount; i++) {
        if (gNodes[i].num == num && gNodes[i].shortName[0]) return gNodes[i].shortName;
    }
    return nullptr;
}

static void handleRx(const RxEvent &ev) {
    if (!ev.ok) return;
    if (ev.hdr.from == gCfg.nodeNum) return;
    touchNode(ev.hdr.from, ev.snr);
    if (ev.data.portnum == kPortText) {
        String text;
        text.reserve(ev.data.payloadLen + 1);
        for (size_t i = 0; i < ev.data.payloadLen; i++) text += (char)ev.data.payload[i];
        const char *who = shortFor(ev.hdr.from);
        char prefix[24];
        if (who) snprintf(prefix, sizeof(prefix), "%s: ", who);
        else snprintf(prefix, sizeof(prefix), "%04x: ", (unsigned)(ev.hdr.from & 0xffff));
        pushChat(String(prefix) + text);
    } else if (ev.data.portnum == kPortNodeInfo) {
        mesh::DecodedUser u;
        if (mesh::decodeUser(ev.data.payload, ev.data.payloadLen, u)) {
            applyUser(ev.hdr.from, u, ev.snr);
            gDirty = true;
        }
    }
}

static void pumpRadio() {
    RxEvent ev;
    while (mesh::meshPollRx(gCfg, ev)) handleRx(ev);
    uint32_t now = millis();
    if (now - gLastNodeInfoMs >= kNodeInfoIntervalMs) {
        gLastNodeInfoMs = now;
        mesh::meshSendNodeInfo(gCfg);
    }
}

static void drawChatScreen() {
    drawMainBorder(); // no body title — leaves main area for messages
    tft.setTextDatum(TL_DATUM);

    // App name in the top status bar (same strip as battery)
    const int titleSize = uiDenseFont();
    const int titleY = max(4, (25 - titleSize * LH) / 2);
    tft.setTextSize(titleSize);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.fillRect(12, titleY, max(72, 10 * titleSize * LW), titleSize * LH, kvxConfig.bgColor);
    tft.drawString("MESH CHAT", 12, titleY);

    std::vector<String> lines;
    flattenChat(lines);
    clampChatScroll((int)lines.size());

    const int font = chatFont();
    const int lineH = chatLineH();
    const int visible = chatVisibleLines();
    int y = chatContentTop();
    tft.setTextSize(font);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    for (int i = 0; i < visible; i++) {
        int idx = gScroll + i;
        if (idx >= (int)lines.size()) break;
        tft.drawString(lines[idx], 6, y);
        y += lineH;
    }

    tft.setTextSize(uiDenseFont());
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawString("Enter=type  ;/.=scroll  Esc", 6, tftHeight - chatFooterH());
    gDirty = false;
}

static void chatCompose() {
    String draft = keyboard("", 200, "Mesh message:");
    if (draft.length() == 0 || draft == "\x1B") {
        gDirty = true;
        return;
    }
    if (!mesh::meshSendText(gCfg, draft.c_str())) {
        displayError("Send failed / duty", true);
        gDirty = true;
        return;
    }
    pushChat(String(gCfg.shortName) + ": " + draft);
}

static void chatScrollBy(int delta) {
    std::vector<String> lines;
    flattenChat(lines);
    gScroll += delta;
    clampChatScroll((int)lines.size());
    gDirty = true;
}

static void chatLoop() {
    gDirty = true;
    gScroll = 0;
    while (true) {
        pumpRadio();
        if (gDirty) drawChatScreen();
        if (check(EscPress)) break;
        if (check(SelPress)) chatCompose();
#if defined(HAS_KEYBOARD)
        // ; and . as up/down while not composing (Cardputer Meshtastic UX)
        keyStroke ks = _getKeyPress();
        if (ks.pressed) {
            for (char c : ks.word) {
                if (c == ';') chatScrollBy(-1);
                else if (c == '.') chatScrollBy(1);
            }
        }
#endif
        if (check(PrevPress)) chatScrollBy(-1);
        if (check(NextPress)) chatScrollBy(1);
        delay(20);
    }
}

static void drawNodesScreen() {
    drawMainBorderWithTitle("Mesh Nodes");
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    int y = 40;
    char line[48];
    snprintf(line, sizeof(line), "Me %s (!%08x)", gCfg.shortName, (unsigned)gCfg.nodeNum);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString(line, 6, y);
    y += 14;
    snprintf(
        line, sizeof(line), "%.3f MHz  %s  P%d", mesh::meshFrequencyMHz(),
        mesh::presetParams(gCfg.preset).name, mesh::meshTxPowerDbm()
    );
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawString(line, 6, y);
    y += 14;
    if (gNodeCount == 0) {
        tft.drawString("No nodes heard yet", 6, y);
    } else {
        for (size_t i = 0; i < gNodeCount && y < tftHeight - 20; i++) {
            const HeardNode &n = gNodes[i];
            char who[16];
            if (n.shortName[0]) strlcpy(who, n.shortName, sizeof(who));
            else snprintf(who, sizeof(who), "%04x", (unsigned)(n.num & 0xffff));
            uint32_t ago = (millis() - n.lastHeardMs) / 1000UL;
            snprintf(line, sizeof(line), "%s  SNR%.1f  %lus", who, n.snr, (unsigned long)ago);
            tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
            tft.drawString(line, 6, y);
            y += 12;
        }
    }
    tft.setTextColor(kvxConfig.secColor, kvxConfig.bgColor);
    tft.drawString("Esc=back", 6, tftHeight - 14);
}

static void nodesLoop() {
    uint32_t lastDraw = 0;
    while (true) {
        pumpRadio();
        if (millis() - lastDraw > 500) {
            drawNodesScreen();
            lastDraw = millis();
        }
        if (check(EscPress)) break;
        delay(20);
    }
}

static void editChannelName() {
    String v = keyboard(String(gCfg.channelName), 11, "Channel name:");
    if (v == "\x1B") return;
    strlcpy(gCfg.channelName, v.c_str(), sizeof(gCfg.channelName));
    saveMeshSettings();
}

static void editShortName() {
    String v = keyboard(String(gCfg.shortName), 4, "Short name (4):");
    if (v == "\x1B" || v.length() == 0) return;
    strlcpy(gCfg.shortName, v.c_str(), sizeof(gCfg.shortName));
    saveMeshSettings();
}

static void editLongName() {
    String v = keyboard(String(gCfg.longName), 24, "Long name:");
    if (v == "\x1B") return;
    strlcpy(gCfg.longName, v.c_str(), sizeof(gCfg.longName));
    saveMeshSettings();
}

static void editPsk() {
    options = {
        {"Default (AQ==)",
         []() {
             gCfg.pskStored[0] = 1;
             gCfg.pskStoredLen = 1;
             saveMeshSettings();
         }},
        {"Hex key",
         []() {
             String hx = hex_keyboard("", 64, "AES128/256 hex PSK:");
             if (hx == "\x1B" || hx.length() < 2) return;
             size_t n = 0;
             for (size_t i = 0; i + 1 < hx.length() && n < sizeof(gCfg.pskStored); i += 2) {
                 char b[3] = {hx[i], hx[i + 1], 0};
                 gCfg.pskStored[n++] = (uint8_t)strtoul(b, nullptr, 16);
             }
             if (n > 0) {
                 gCfg.pskStoredLen = n;
                 saveMeshSettings();
             }
         }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Chan PSK");
}

static void editPreset() {
    // EU868 preset list from Meshtastic PROFILE_EU868
    options = {
        {"LongFast",   []() { gCfg.preset = Preset::LongFast; }  },
        {"LongSlow",   []() { gCfg.preset = Preset::LongSlow; }  },
        {"MediumSlow", []() { gCfg.preset = Preset::MediumSlow; }},
        {"MediumFast", []() { gCfg.preset = Preset::MediumFast; }},
    };
    int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Modem", (int)gCfg.preset);
    if (sel >= 0) {
        gCfg.preset = (Preset)sel;
        saveMeshSettings();
    }
}

static void settingsLoop() {
    while (true) {
        String presetLine = String("Preset: ") + mesh::presetParams(gCfg.preset).name;
        String chanLine = String("Channel: ") +
            (gCfg.channelName[0] ? gCfg.channelName : "(default/LongFast)");
        String pskLine = gCfg.pskStoredLen == 1 && gCfg.pskStored[0] == 1 ? "PSK: default"
                                                                          : "PSK: custom";
        String shortLine = String("Short: ") + gCfg.shortName;
        String longLine = String("Long: ") + gCfg.longName;
        String regionLine = "Region: EU868";
        options = {
            {regionLine.c_str(), []() {}},
            {presetLine.c_str(), []() { editPreset(); }},
            {chanLine.c_str(),   []() { editChannelName(); }},
            {pskLine.c_str(),    []() { editPsk(); }},
            {shortLine.c_str(),  []() { editShortName(); }},
            {longLine.c_str(),   []() { editLongName(); }},
            {"Apply radio",
             []() {
                 mesh::stopMeshRadio();
                 if (!mesh::startMeshRadio(gCfg)) displayError("Radio start failed", true);
                 else {
                     mesh::meshSendNodeInfo(gCfg);
                     displayInfo("Radio restarted", true);
                 }
             }},
            {"Back", []() {}},
        };
        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "Mesh Set");
        if (selected < 0 || selected == (int)options.size() - 1) return;
    }
}

} // namespace

void meshtasticMenu() {
    loadMeshSettings();
    clearLoraRadio();
    if (!mesh::startMeshRadio(gCfg)) {
        displayError("Meshtastic radio failed", true);
        return;
    }
    gLastNodeInfoMs = millis();
    mesh::meshSendNodeInfo(gCfg);
    pushChat(String("Joined EU868/") + mesh::presetParams(gCfg.preset).name);

    while (true) {
        options = {
            {"Chat",     []() { chatLoop(); }},
            {"Nodes",    []() { nodesLoop(); }},
            {"Settings", []() { settingsLoop(); }},
            {"Back",     []() {}},
        };
        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "Meshtastic");
        if (selected < 0 || selected == (int)options.size() - 1) break;
    }

    mesh::stopMeshRadio();
}

#endif
