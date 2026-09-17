#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

struct AppCatalogItem {
    const char *id;
    const char *label;
    const char *group; // shortcut picker category
    bool discovery;
    bool (*available)();
    void (*launch)();
};

struct AppShortcutTarget {
    String id;
    String label;
};

const std::vector<AppCatalogItem> &appCatalogItems();
bool appCatalogAvailable(const AppCatalogItem &item);
bool appCatalogLaunch(const String &id);
String appCatalogLabel(const String &id);
std::vector<AppShortcutTarget> appCatalogShortcutTargets();
bool appCatalogKeyReserved(char c);
bool appCatalogHandleMainscreenKeys();
void setMainscreenShortcutsMenu();
