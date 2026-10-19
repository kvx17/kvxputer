#ifndef __PC_CONNECT_MENU_H__
#define __PC_CONNECT_MENU_H__

#include <MenuItemInterface.h>

/** Top-level main-menu tile that launches USB PC Connect. */
class PcConnectMenu : public MenuItemInterface {
public:
    PcConnectMenu() : MenuItemInterface("PC Connect") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;
    bool hasTheme() { return kvxConfig.theme.usb; }
    const String &themePath() override { return kvxConfig.theme.paths.usb; }
};

#endif
