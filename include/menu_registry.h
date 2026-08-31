#ifndef __MENU_REGISTRY_H__
#define __MENU_REGISTRY_H__

#include <MenuItemInterface.h>
#include <cstddef>
#include <cstdint>

enum MenuRegistryFlags : uint8_t {
    MENU_FLAG_NONE           = 0,
    MENU_FLAG_LITE_EXCLUDED  = 1 << 0,
    MENU_FLAG_BOARD_ETHERNET = 1 << 1,
    MENU_FLAG_BOARD_FM       = 1 << 2,
    MENU_FLAG_BOARD_LORA     = 1 << 3,
    MENU_FLAG_SCRIPTS        = 1 << 4,
};

struct MenuDescriptor {
    const char *id;
    const char *label;
    MenuItemInterface *item;
    uint8_t flags;
};

extern const MenuDescriptor kMenus[];
extern const size_t kMenuCount;

bool menuDescriptorAvailable(const MenuDescriptor &desc);

std::vector<MenuItemInterface *> buildVisibleMenus();

#endif
