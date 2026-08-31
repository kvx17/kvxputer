#include "netops_extras.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/net/wifi_common.h"

#if !defined(LITE_VERSION)
#include "menu/ethernet/ARPScanner.h"
#include "menu/wifi/clients.h"
#include "menu/wifi/responder.h"
#include "menu/wifi/tcp_utils.h"
#include <esp_netif.h>
#endif

static void evilStub(const char *name) {
    displayInfo(String(name) + "\n(port pending — see docs/EVIL_FEATURE_MAP.md)", true);
}

void evilNetOpsBuildMenu(std::vector<Option> &options) {
#if !defined(LITE_VERSION)
    options.push_back({"SSH (Bruce)", lambdaHelper(ssh_setup, String(""))});
    options.push_back({"Scan Hosts (Bruce)", [=]() {
                           bool doScan = true;
                           if (!WiFi.isConnected()) doScan = wifiConnectMenu();
                           if (doScan) {
                               esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                               if (netif) ARPScanner{netif};
                               else displayError("No netif", true);
                           }
                       }});
    options.push_back({"Listen TCP (Bruce)", listenTcpPort});
    options.push_back({"Client TCP (Bruce)", clientTCP});
    options.push_back({"Responder (Bruce)", responder});
#endif

    options.push_back({"Web Crawler", []() { evilStub("Web Crawler"); }});
    options.push_back({"Reverse TCP Tunnel", []() { evilStub("Reverse TCP Tunnel"); }});
    options.push_back({"DHCP Starvation", []() { evilStub("DHCP Starvation"); }});
    options.push_back({"Rogue DHCP STA", []() { evilStub("Rogue DHCP STA"); }});
    options.push_back({"Rogue DHCP AP", []() { evilStub("Rogue DHCP AP"); }});
    options.push_back({"Switch DNS", []() { evilStub("Switch DNS"); }});
    options.push_back({"Network Hijacking", []() { evilStub("Network Hijacking"); }});
    options.push_back({"WPAD Abuse", []() { evilStub("WPAD Abuse"); }});
    options.push_back({"Crack NTLMv2", []() { evilStub("Crack NTLMv2"); }});
    options.push_back({"Clean NTLMv2 dups", []() { evilStub("Clean NTLMv2 duplicate"); }});
    options.push_back({"UART Shell", []() { evilStub("UART Shell"); }});

    options.push_back({"Printer Tools", []() { evilStub("Printer Tools"); }});
    options.push_back({"HoneyPot", []() { evilStub("HoneyPot"); }});
    options.push_back({"EvilChatMesh", []() { evilStub("EvilChatMesh"); }});
    options.push_back({"SIP Toolkit", []() { evilStub("SIP Toolkit"); }});
    options.push_back({"CCTV Toolkit", []() { evilStub("CCTV Toolkit"); }});
    options.push_back({"SSDP Poisoner", []() { evilStub("SSDP Poisoner"); }});
    options.push_back({"SkyJack", []() { evilStub("SkyJack"); }});
    options.push_back({"UPnP Tools", []() { evilStub("UPnP Tools"); }});
    options.push_back({"LDAP Dump", []() { evilStub("LDAPDump"); }});
    options.push_back({"Autodiscover Abuse", []() { evilStub("Autodiscover Abuse"); }});
    options.push_back({"CIW Zeroclick", []() { evilStub("CIW Zeroclick"); }});
    options.push_back({"IMSI Catcher", []() { evilStub("IMSI Catcher"); }});
    options.push_back({"TagTinker ESL", []() { evilStub("TagTinker ESL"); }});
}
