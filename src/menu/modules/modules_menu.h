#ifndef __MODULES_MENU_H__
#define __MODULES_MENU_H__

#include <MenuItemInterface.h>

class ModulesMenu : public MenuItemInterface {
public:
    ModulesMenu() : MenuItemInterface("Modules") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static String empty;
        return empty;
    }

private:
    void unitScrollMenu(void);
    void unitJoystick2Menu(void);
    void unitScrollTestScreen(void);
    void unitJoystick2TestScreen(void);
    void pahubMenu(void);
    void pahubChannelMenu(uint8_t ch);
    void pahubScanMenu(void);
    void companionBinsMenu(void);
};

#endif
