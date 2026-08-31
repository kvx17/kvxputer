#include "root/ui/main_menu.h"
#include "menu_registry.h"
#include "root/ui/display.h"
#include "root/ui/kvx_main_menu.h"
#include "root/app/utils.h"
#include <globals.h>

MainMenu::MainMenu() {
    for (size_t i = 0; i < kMenuCount; i++) {
        if (menuDescriptorAvailable(kMenus[i])) _menuItems.push_back(kMenus[i].item);
    }
    _totalItems = _menuItems.size();
}

MainMenu::~MainMenu() {}

void MainMenu::begin(void) {
    returnToMenu = false;
    auto visible = buildVisibleMenus();
    _currentIndex = kvxMainMenuLoop(visible, _currentIndex);
}

void MainMenu::hideAppsMenu() {
    int index = 0;
RESTART:
    options.clear();
    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (!menuDescriptorAvailable(desc)) continue;

        String label = desc.label;
        std::vector<String> disabled = kvxConfig.disabledMenus;
        bool enabled = true;
        for (const String &d : disabled) {
            if (d == desc.id || d == desc.label) {
                enabled = false;
                break;
            }
        }
        options.push_back(
            {label,
             [id = String(desc.id), label, enabled]() {
                 if (enabled) kvxConfig.addDisabledMenu(id);
                 else kvxConfig.removeDisabledMenu(id);
             },
             enabled}
        );
    }
    options.push_back({"Show All", [=]() { kvxConfig.disabledMenus.clear(); }, true});
    addOptionToMainMenu();
    index = loopOptions(options, index);
    kvxConfig.saveFile();
    if (!returnToMenu) goto RESTART;
}

// Exposed for registry visibility checks from main_menu constructor.
bool menuDescriptorAvailable(const MenuDescriptor &desc) {
#if defined(LITE_VERSION)
    if (desc.flags & MENU_FLAG_LITE_EXCLUDED) return false;
#endif
#if !defined(LITE_VERSION) && defined(DISABLE_INTERPRETER)
    if (desc.flags & MENU_FLAG_SCRIPTS) return false;
#endif
#if !defined(FM_SI4713)
    if (desc.flags & MENU_FLAG_BOARD_FM) return false;
#endif
    (void)desc;
    return true;
}
