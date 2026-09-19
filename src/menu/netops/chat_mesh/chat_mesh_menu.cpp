/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * ESP-NOW mesh chat. Does not call M5.begin().
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "chat_mesh.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "esp_now.h"
#include <WiFi.h>
#include <globals.h>
#include <vector>

namespace {
struct MsgItem {
    char data[80];
    uint8_t len;
};
constexpr int kQ = 16;
MsgItem gQ[kQ];
volatile int gHead = 0, gTail = 0;
uint8_t gBcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void onChatRecv(
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_now_recv_info_t *, const uint8_t *d, int l
#else
    const uint8_t *, const uint8_t *d, int l
#endif
) {
    if (l <= 0) return;
    int next = (gHead + 1) % kQ;
    if (next == gTail) return;
    int n = l > 79 ? 79 : l;
    memcpy(gQ[gHead].data, d, n);
    gQ[gHead].data[n] = 0;
    gQ[gHead].len = (uint8_t)n;
    gHead = next;
}
} // namespace

void chatMeshMenu() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    if (esp_now_init() != ESP_OK) {
        displayError("ESP-NOW init failed", true);
        return;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, gBcast, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(gBcast)) esp_now_add_peer(&peer);
    esp_now_register_recv_cb(onChatRecv);
    gHead = gTail = 0;
    std::vector<String> msgs;
    msgs.push_back("Mesh chat ready");

    drawMainBorderWithTitle("EvilChatMesh");
    tft.setTextSize(FP);
    tft.drawString("Enter to type, ESC quit", 10, uiFooterY(FP));
    EscPress = false;
    SelPress = false;
    bool dirty = true;
    while (!check(EscPress) && !returnToMenu) {
        while (gTail != gHead) {
            msgs.push_back(String(gQ[gTail].data));
            gTail = (gTail + 1) % kQ;
            if (msgs.size() > 20) msgs.erase(msgs.begin());
            dirty = true;
        }
        if (dirty) {
            dirty = false;
            tft.fillRect(10, uiStatusY(0), tftWidth - 20, uiFooterY(FP) - uiStatusY(0), kvxConfig.bgColor);
            int y = uiStatusY(0);
            const int rowH = uiRowH(FP);
            int maxVisible = max(1, (uiFooterY(FP) - y) / rowH);
            int start = msgs.size() > maxVisible ? (int)msgs.size() - maxVisible : 0;
            for (int i = start; i < (int)msgs.size(); i++) {
                tft.drawString(msgs[i].substring(0, max(1, (tftWidth - 20) / uiCharW(FP))), 10, y);
                y += rowH;
            }
        }
        if (check(SelPress)) {
            String msg = keyboard("", 60, "Chat message");
            if (msg.length() && msg != "\x1B") {
                esp_now_send(gBcast, (uint8_t *)msg.c_str(), msg.length());
                msgs.push_back("> " + msg);
                dirty = true;
            }
            EscPress = false;
        }
        delay(30);
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    wifiDisconnect();
}
#endif
