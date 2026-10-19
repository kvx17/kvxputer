#ifndef __NFC_MENU_H__
#define __NFC_MENU_H__

#include <MenuItemInterface.h>

class NFCMenu : public MenuItemInterface {
public:
    NFCMenu() : MenuItemInterface("NFC") {}

    void optionsMenu(void);
    void configMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return kvxConfig.theme.nfc; }
    const String &themePath() override { return kvxConfig.theme.paths.nfc; }
};

#endif
