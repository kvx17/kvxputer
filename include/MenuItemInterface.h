#ifndef __MENU_ITEM_INTERFACE_H__
#define __MENU_ITEM_INTERFACE_H__

#include "root/ui/display.h"
#include <globals.h>

class MenuItemInterface {
public:
    virtual ~MenuItemInterface() = default;
    virtual void optionsMenu(void) = 0;
    virtual void drawIcon(float scale = 1) = 0;
    virtual void drawIconImg() {
        drawImg(
            *kvxConfig.themeFS(),
            kvxConfig.getThemeItemImg(themePath()),  // const String& - no heap alloc
            0,
            imgCenterY,
            true,
            kvxConfig.theme.gifDuration,
            false
        );
    }
    virtual bool hasTheme() = 0;
    virtual const String& themePath() = 0;

    bool checkTheme() { return hasTheme() && themePath().length() > 0; }
    String getName() const { return String(_name); }

    // Draw category icon centered in a tile (main menu channels).
    void drawIconAt(float scale, int centerX, int centerY, int clipW, int clipH, uint16_t bgFill) {
        int savedCX = iconCenterX;
        int savedCY = iconCenterY;
        int savedAW = iconAreaW;
        int savedAH = iconAreaH;
        int savedAX = iconAreaX;
        int savedAY = iconAreaY;

        iconAreaW = clipW;
        iconAreaH = clipH;
        iconCenterX = centerX;
        iconCenterY = centerY;
        iconAreaX = centerX - clipW / 2;
        iconAreaY = centerY - clipH / 2;

        tft.fillRect(iconAreaX, iconAreaY, iconAreaW, iconAreaH, bgFill);
        drawIcon(scale);

        iconCenterX = savedCX;
        iconCenterY = savedCY;
        iconAreaW = savedAW;
        iconAreaH = savedAH;
        iconAreaX = savedAX;
        iconAreaY = savedAY;
    }

    void draw(float scale = 1) {
        if (rotation != kvxConfigPins.rotation) resetCoordinates();
        if (!checkTheme()) {
            tft.fillRect(0, 27, tftWidth, tftHeight - 27, kvxConfig.bgColor);
            drawIcon(scale);
            drawArrows(scale);
            drawTitle(scale);
        } else {
            if (kvxConfig.theme.label)
                drawTitle(scale); // If using .GIF, labels are draw after complete, which takes some time
            drawIconImg();
            if (kvxConfig.theme.label) drawTitle(scale); // Makes sure to draw over the image
        }
        drawStatusBar();
    }

    void drawArrows(float scale = 1) {
        tft.fillRect(arrowAreaX, iconAreaY, arrowAreaW, iconAreaH, kvxConfig.bgColor);
        tft.fillRect(
            tftWidth - arrowAreaX - arrowAreaW, iconAreaY, arrowAreaW, iconAreaH, kvxConfig.bgColor
        );

        int arrowSize = scale * 10;
        int lineWidth = scale * 3;

        int arrowX = BORDER_PAD_X + 1.5 * arrowSize;
        int arrowY = iconCenterY + 1.5 * arrowSize;

        // Left Arrow
        tft.drawWideLine(
            arrowX,
            arrowY,
            arrowX + arrowSize,
            arrowY + arrowSize,
            lineWidth,
            kvxConfig.priColor,
            kvxConfig.bgColor
        );
        tft.drawWideLine(
            arrowX,
            arrowY,
            arrowX + arrowSize,
            arrowY - arrowSize,
            lineWidth,
            kvxConfig.priColor,
            kvxConfig.bgColor
        );

        // Right Arrow
        tft.drawWideLine(
            tftWidth - arrowX,
            arrowY,
            tftWidth - arrowX - arrowSize,
            arrowY + arrowSize,
            lineWidth,
            kvxConfig.priColor,
            kvxConfig.bgColor
        );
        tft.drawWideLine(
            tftWidth - arrowX,
            arrowY,
            tftWidth - arrowX - arrowSize,
            arrowY - arrowSize,
            lineWidth,
            kvxConfig.priColor,
            kvxConfig.bgColor
        );
    }

    void drawTitle(float scale = 1) {
        int titleY = iconCenterY + iconAreaH / 2 + FG;

        tft.setTextSize(FM);
        tft.drawPixel(0, 0, 0);
        tft.fillRect(arrowAreaX, titleY, tftWidth - 2 * arrowAreaX, LH * FM, kvxConfig.bgColor);
        int nchars = (tftWidth - 16) / (LW * FM);
        tft.drawCentreString(getName().substring(0, nchars), iconCenterX, titleY, 1);
    }

protected:
    const char *_name = "";
    uint8_t rotation = ROTATION;

    int iconAreaH =
        ((tftHeight - 2 * BORDER_PAD_Y) % 2 == 0 ? tftHeight - 2 * BORDER_PAD_Y
                                                 : tftHeight - 2 * BORDER_PAD_Y + 1);
    int iconAreaW = iconAreaH;

    int iconCenterX = tftWidth / 2;
    int iconCenterY = tftHeight / 2;
    int imgCenterY = 13;

    int iconAreaX = iconCenterX - iconAreaW / 2;
    int iconAreaY = iconCenterY - iconAreaH / 2;

    int arrowAreaX = BORDER_PAD_X;
    int arrowAreaW = iconAreaX - arrowAreaX;

    MenuItemInterface(const char *name) : _name(name) {}

    void clearIconArea(void) {
        tft.fillRect(iconAreaX, iconAreaY, iconAreaW, iconAreaH, kvxConfig.bgColor);
    }
    void clearImgArea(void) { tft.fillRect(7, 27, tftWidth - 14, tftHeight - 34, kvxConfig.bgColor); }
    void resetCoordinates(void) {
        // Recalculate Center and ared due to portrait/landscape changings
        if (tftWidth > tftHeight) {
            iconAreaH =
                ((tftHeight - 2 * BORDER_PAD_Y) % 2 == 0 ? tftHeight - 2 * BORDER_PAD_Y
                                                         : tftHeight - 2 * BORDER_PAD_Y + 1);
        } else {
            iconAreaH =
                ((tftWidth - 2 * BORDER_PAD_Y) % 2 == 0 ? tftWidth - 2 * BORDER_PAD_Y
                                                        : tftWidth - 2 * BORDER_PAD_Y + 1);
        }

        iconAreaW = iconAreaH;

        iconCenterX = tftWidth / 2;
        iconCenterY = tftHeight / 2;

        iconAreaX = iconCenterX - iconAreaW / 2;
        iconAreaY = iconCenterY - iconAreaH / 2;

        arrowAreaX = BORDER_PAD_X;
        arrowAreaW = iconAreaX - arrowAreaX;

        rotation = kvxConfigPins.rotation;
    }

private:
};

#endif // __MENU_ITEM_INTERFACE_H__
