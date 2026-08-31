#include "netops_menu.h"
#include "root/ui/display.h"
#include "root/app/utils.h"
#include "root/net/wifi_common.h"
#include <globals.h>

#if !defined(LITE_VERSION)
#include "menu/ethernet/ARPScanner.h"
#include "menu/wifi/clients.h"
#include "menu/wifi/responder.h"
#include "menu/wifi/tcp_utils.h"
#include "menu/netops/reverseShell/reverseShell.h"
#include <esp_netif.h>
#endif

#if defined(EVIL_EXTENSIONS)
#include "menu/netops/web_crawler/web_crawler.h"
#include "menu/netops/reverse_tcp/reverse_tcp.h"
#include "menu/netops/dhcp_starvation/dhcp_starvation.h"
#include "menu/netops/rogue_dhcp/rogue_dhcp.h"
#include "menu/netops/switch_dns/switch_dns.h"
#include "menu/netops/hijack/hijack.h"
#include "menu/netops/wpad/wpad.h"
#include "menu/netops/ntlm/ntlm.h"
#include "menu/netops/uart_shell/uart_shell.h"
#include "menu/netops/printer/printer.h"
#include "menu/netops/honeypot/honeypot.h"
#include "menu/netops/chat_mesh/chat_mesh.h"
#include "menu/netops/sip/sip.h"
#include "menu/netops/cctv/cctv.h"
#include "menu/netops/ssdp/ssdp.h"
#include "menu/netops/skyjack/skyjack.h"
#include "menu/netops/upnp/upnp.h"
#include "menu/netops/ldap/ldap.h"
#include "menu/netops/autodiscover/autodiscover.h"
#include "menu/netops/ciw/ciw.h"
#include "menu/netops/imsi_eap/imsi_eap.h"
#endif

void NetOpsMenu::optionsMenu() {
    options.clear();

#if !defined(LITE_VERSION)
    options.push_back({"SSH", lambdaHelper(ssh_setup, String(""))});
    options.push_back({"Scan Hosts", [=]() {
                           bool doScan = true;
                           if (!WiFi.isConnected()) doScan = wifiConnectMenu();
                           if (doScan) {
                               esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                               if (netif) ARPScanner{netif};
                               else displayError("No netif", true);
                           }
                       }});
    options.push_back({"Listen TCP", listenTcpPort});
    options.push_back({"Client TCP", clientTCP});
    options.push_back({"Responder", responder});
    options.push_back({"Reverse Shell", ReverseShell});
#endif

#if defined(EVIL_EXTENSIONS)
    options.push_back({"Web Crawler", webCrawlerMenu});
    options.push_back({"Reverse TCP Tunnel", reverseTcpMenu});
    options.push_back({"DHCP Starvation", dhcpStarvationMenu});
    options.push_back({"Rogue DHCP", rogueDhcpMenu});
    options.push_back({"Switch DNS", switchDnsMenu});
    options.push_back({"Network Hijacking", hijackMenu});
    options.push_back({"WPAD Abuse", wpadMenu});
    options.push_back({"NTLMv2", ntlmMenu});
    options.push_back({"UART Shell", uartShellMenu});
    options.push_back({"Printer Tools", printerMenu});
    options.push_back({"HoneyPot", honeypotMenu});
    options.push_back({"EvilChatMesh", chatMeshMenu});
    options.push_back({"SIP Toolkit", sipMenu});
    options.push_back({"CCTV Toolkit", cctvMenu});
    options.push_back({"SSDP Poisoner", ssdpMenu});
    options.push_back({"SkyJack", skyjackMenu});
    options.push_back({"UPnP Tools", upnpMenu});
    options.push_back({"LDAP Dump", ldapMenu});
    options.push_back({"Autodiscover Abuse", autodiscoverMenu});
    options.push_back({"CIW Zeroclick", ciwMenu});
    options.push_back({"EAP Identity Sniff", imsiEapMenu});
#elif defined(LITE_VERSION)
    options.push_back(
        {"Extensions disabled", []() { displayInfo("Build with -DEVIL_EXTENSIONS=1", true); }}
    );
#endif

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "NetOps");
}

void NetOpsMenu::drawIcon(float scale) {
    clearIconArea();
    int w = scale * 40;
    int h = scale * 28;
    int t = scale * 3;
    if (w % 2) w++;
    if (h % 2) h++;

    tft.fillCircle(iconCenterX - w / 2, iconCenterY, t + 1, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + w / 2, iconCenterY - h / 2, t + 1, kvxConfig.priColor);
    tft.fillCircle(iconCenterX + w / 2, iconCenterY + h / 2, t + 1, kvxConfig.priColor);
    tft.drawLine(
        iconCenterX - w / 2, iconCenterY, iconCenterX + w / 2, iconCenterY - h / 2, kvxConfig.priColor
    );
    tft.drawLine(
        iconCenterX - w / 2, iconCenterY, iconCenterX + w / 2, iconCenterY + h / 2, kvxConfig.priColor
    );
}
