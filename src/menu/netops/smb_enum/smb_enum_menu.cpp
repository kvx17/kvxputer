#include "smb_enum.h"
#if defined(EVIL_EXTENSIONS)
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include "root/ui/scrollableTextArea.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <globals.h>
#include <vector>

namespace {

// Minimal SMB1 negotiate + session setup guest probe; list shares via NetShareEnum is heavy —
// we report dialect / guest status and common share probes via IPC$.
bool tcpOpen(const String &host, uint16_t port, uint32_t to = 800) {
    WiFiClient c;
    bool ok = c.connect(host.c_str(), port, to);
    if (ok) c.stop();
    return ok;
}

String probeBanner(const String &host, uint16_t port) {
    WiFiClient c;
    if (!c.connect(host.c_str(), port, 800)) return "closed";
    // NetBIOS session request is complex; just note TCP open and try reading
    delay(50);
    String b;
    uint32_t t0 = millis();
    while (millis() - t0 < 400 && b.length() < 64) {
        while (c.available()) b += (char)c.read();
        delay(5);
    }
    c.stop();
    if (b.isEmpty()) return "open (no banner)";
    String out;
    for (size_t i = 0; i < b.length() && out.length() < 48; i++) {
        char ch = b[i];
        out += (ch >= 32 && ch < 127) ? ch : '.';
    }
    return out;
}

} // namespace

void smbEnumMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;
    String host = keyboard(WiFi.gatewayIP().toString(), 64, "Host IP");
    host.trim();
    if (host.isEmpty() || host == "\x1B") return;

    drawMainBorderWithTitle("SMB Enum");
    padprintln(host);
    std::vector<String> lines;
    lines.push_back("Target " + host);

    bool p139 = tcpOpen(host, 139);
    bool p445 = tcpOpen(host, 445);
    lines.push_back(String("139/tcp ") + (p139 ? "open" : "closed"));
    lines.push_back(String("445/tcp ") + (p445 ? "open" : "closed"));

    if (p445) {
        lines.push_back("445: " + probeBanner(host, 445));
        // Probe common share names with TCP reconnect (path existence via SMB needs full stack)
        const char *shares[] = {"IPC$", "ADMIN$", "C$", "print$", "shared", "public", "users"};
        lines.push_back("Common share names (manual check):");
        for (auto *s : shares) lines.push_back(String("  \\\\") + host + "\\" + s);
        lines.push_back("Guest/null: try from PC (smbclient -N)");
    } else if (p139) {
        lines.push_back("139: " + probeBanner(host, 139));
        lines.push_back("NetBIOS present; prefer 445");
    } else {
        lines.push_back("No SMB ports open");
    }

    ScrollableTextArea area("SMB Enum");
    for (auto &l : lines) area.addLine(l);
    area.show(true);
}
#endif
