/*
 * Ported from Evil-Cardputer (Evil-M5Project) by 7h30th3r0n3.
 * Network hijack: DHCP starvation then rogue DHCP STA.
 * Combined firmware: AGPL-3.0-or-later (Bruce).
 */
#include "hijack.h"
#if defined(EVIL_EXTENSIONS)
#include "menu/netops/dhcp_starvation/dhcp_starvation.h"
#include "menu/netops/rogue_dhcp/rogue_dhcp.h"
#include "root/ui/display.h"
#include <globals.h>

void hijackMenu() {
    displayInfo("Step 1: DHCP starvation\nStep 2: Rogue DHCP STA\nESC in each tool to continue", true);
    dhcpStarvationMenu();
    displayInfo("Starting rogue DHCP STA", true);
    rogueDhcpSta();
}
#endif
