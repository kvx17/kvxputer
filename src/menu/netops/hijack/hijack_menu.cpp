/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Hijack session: starvation, rogue DHCP STA, captive portal.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "hijack.h"
#if defined(EVIL_EXTENSIONS)
#include "menu/netops/dhcp_starvation/dhcp_starvation.h"
#include "menu/netops/rogue_dhcp/rogue_dhcp.h"
#include "menu/wifi/evil_portal.h"
#include "root/ui/display.h"
#include <globals.h>

void hijackMenu() {
    displayInfo("1 Starvation\n2 Rogue DHCP STA\n3 Captive portal\nESC skips a step", true);
    dhcpStarvationMenu();
    if (returnToMenu) return;
    displayInfo("Rogue DHCP STA", true);
    rogueDhcpSta();
    if (returnToMenu) return;
    displayInfo("Starting portal", true);
    EvilPortal();
}
#endif
