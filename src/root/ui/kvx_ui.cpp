#include "kvx_ui.h"
#include "root/ui/display.h"

static const uint16_t KVX_PURPLE = 0x9818;
static const uint16_t KVX_GREEN = 0x07E0;
static const uint16_t KVX_BG = 0x0841;

void drawKvxSubmenu(int index, std::vector<Option> &options, const char *title) {
    tft.fillScreen(KVX_BG);

    tft.setTextSize(FP);
    tft.setTextColor(KVX_GREEN, KVX_BG);
    tft.drawString(title, 6, 26);

    const int lineH = FM * LH + 4;
    const int startY = 42;
    const int visible = max(1, (tftHeight - startY - 6) / lineH);
    int scroll = 0;
    if (index >= visible) scroll = index - visible + 1;

    for (int i = scroll; i < (int)options.size() && i < scroll + visible; i++) {
        int y = startY + (i - scroll) * lineH;
        bool sel = (i == index);
        String line = sel ? String("> ") + options[i].label : String("  ") + options[i].label;

        tft.setTextSize(FM);
        if (!options[i].enabled) {
            tft.setTextColor(TFT_DARKGREY, KVX_BG);
        } else if (sel) {
            tft.setTextColor(KVX_PURPLE, KVX_BG);
        } else {
            tft.setTextColor(KVX_GREEN, KVX_BG);
        }
        tft.drawString(line, 6, y, 1);
    }
}
