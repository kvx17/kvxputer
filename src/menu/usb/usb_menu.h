#ifndef __USB_MENU_H__
#define __USB_MENU_H__

#include <MenuItemInterface.h>

class UsbMenu : public MenuItemInterface {
public:
    UsbMenu() : MenuItemInterface("USB") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.usb; }
    const String &themePath() override { return kvxConfig.theme.paths.usb; }
};

#endif
