#include "badusb_hunter.h"

#if defined(SOC_USB_OTG_SUPPORTED) && !defined(LITE_VERSION)

#include "menu/others/audio.h"
#include "root/hal/led_control.h"
#include "root/input/mykeyboard.h"
#include "root/ui/display.h"
#include <globals.h>
#include <string.h>

#include "usb/usb_host.h"

#if __has_include("tusb.h")
#include "tusb.h"
#define BADUSB_HAS_TUD 1
#endif

// Copyright (c) 2023 Noah Axon — adapted from M5Stick-NEMO BadUSB Hunter (GPL-3).

namespace {

struct UsbDeviceInfo {
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t deviceClass = 0;
    uint8_t numInterfaces = 0;
    uint8_t interfaceClasses[8] = {};
    bool isSuspicious = false;
    bool isHidOnly = false;
    String suspicionReason;
};

static usb_host_client_handle_t s_client = nullptr;
static volatile uint8_t s_newAddr = 0;
static volatile bool s_devGone = false;
static UsbDeviceInfo s_device;
static bool s_connected = false;
static uint32_t s_lastBlinkMs = 0;
static bool s_ledOn = false;

static const char *className(uint8_t classCode) {
    switch (classCode) {
        case 0x00: return "Device";
        case 0x01: return "Audio";
        case 0x02: return "CDC-Comm";
        case 0x03: return "HID";
        case 0x05: return "Physical";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC-Data";
        case 0x0B: return "Smart Card";
        case 0x0D: return "Security";
        case 0x0E: return "Video";
        case 0x0F: return "Healthcare";
        case 0x10: return "AV";
        case 0x11: return "Billboard";
        case 0xDC: return "Diagnostic";
        case 0xE0: return "Wireless";
        case 0xEF: return "Misc";
        case 0xFE: return "App-Specific";
        case 0xFF: return "Vendor-Spec";
        default: return "Unknown";
    }
}

static void analyzeDevice(UsbDeviceInfo &device) {
    device.isSuspicious = false;
    device.isHidOnly = false;
    device.suspicionReason = "";

    int hidCount = 0;
    for (int i = 0; i < device.numInterfaces; i++) {
        if (device.interfaceClasses[i] == 0x03) hidCount++;
    }

    if (device.numInterfaces > 1 && hidCount > 0) {
        device.isSuspicious = true;
        device.suspicionReason = "Multi-interface+HID";
    }
    if (hidCount > 1) {
        device.isSuspicious = true;
        device.suspicionReason = "Multiple HID ifaces";
    }
    // Hak5 Rubber Ducky
    if (device.vid == 0x03EB && device.pid == 0x2403) {
        device.isSuspicious = true;
        device.suspicionReason = "Hak5 Rubber Ducky";
    }
    if (device.numInterfaces == hidCount && hidCount > 0) {
        device.isHidOnly = true;
        device.isSuspicious = false;
        device.suspicionReason = "HID";
    }
}

static void clientEventCb(const usb_host_client_event_msg_t *eventMsg, void *arg) {
    (void)arg;
    if (!eventMsg) return;
    if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_newAddr = eventMsg->new_dev.address;
    } else if (eventMsg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        s_devGone = true;
    }
}

static void leaveUsbDeviceMode() {
#if defined(BADUSB_HAS_TUD)
    if (tud_mounted() || tud_connected()) {
        tud_disconnect();
        delay(120);
    }
#endif
}

static bool installHost() {
    leaveUsbDeviceMode();

    usb_host_config_t hostConfig = {};
    hostConfig.skip_phy_setup = false;
    hostConfig.root_port_unpowered = false;
    hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
    hostConfig.enum_filter_cb = nullptr;

    esp_err_t err = usb_host_install(&hostConfig);
    if (err != ESP_OK) {
        Serial.printf("[BadUSBHunter] usb_host_install: %s\n", esp_err_to_name(err));
        return false;
    }

    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 5;
    clientConfig.async.client_event_callback = clientEventCb;
    clientConfig.async.callback_arg = nullptr;

    err = usb_host_client_register(&clientConfig, &s_client);
    if (err != ESP_OK) {
        Serial.printf("[BadUSBHunter] client_register: %s\n", esp_err_to_name(err));
        usb_host_uninstall();
        s_client = nullptr;
        return false;
    }
    return true;
}

static void uninstallHost() {
    if (s_client) {
        usb_host_client_unblock(s_client);
        usb_host_client_deregister(s_client);
        s_client = nullptr;
    }
    bool allFree = false;
    for (int i = 0; i < 20 && !allFree; i++) {
        uint32_t flags = 0;
        usb_host_device_free_all();
        usb_host_lib_handle_events(pdMS_TO_TICKS(50), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
    }
    usb_host_uninstall();
}

static void processDevice(uint8_t address) {
    if (!s_client) return;

    usb_device_handle_t devHdl = nullptr;
    esp_err_t err = usb_host_device_open(s_client, address, &devHdl);
    if (err != ESP_OK || !devHdl) {
        Serial.printf("[BadUSBHunter] device_open: %s\n", esp_err_to_name(err));
        return;
    }

    UsbDeviceInfo info;
    const usb_device_desc_t *devDesc = nullptr;
    if (usb_host_get_device_descriptor(devHdl, &devDesc) == ESP_OK && devDesc) {
        info.vid = devDesc->idVendor;
        info.pid = devDesc->idProduct;
        info.deviceClass = devDesc->bDeviceClass;
    }

    const usb_config_desc_t *cfgDesc = nullptr;
    if (usb_host_get_active_config_descriptor(devHdl, &cfgDesc) == ESP_OK && cfgDesc) {
        int offset = 0;
        while (offset < cfgDesc->wTotalLength && info.numInterfaces < 8) {
            const usb_standard_desc_t *next =
                (const usb_standard_desc_t *)(((const uint8_t *)cfgDesc) + offset);
            if (next->bLength == 0) break;
            if (next->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                const usb_intf_desc_t *intf = (const usb_intf_desc_t *)next;
                info.interfaceClasses[info.numInterfaces++] = intf->bInterfaceClass;
            }
            offset += next->bLength;
        }
    }

    usb_host_device_close(s_client, devHdl);
    analyzeDevice(info);
    s_device = info;
    s_connected = true;

    if (info.isSuspicious || info.isHidOnly) _tone(4000, 50);
}

static void updateLed() {
#ifdef HAS_RGB_LED
    if (!s_connected) {
        ledShowApp(0, 0, 0, 0);
        s_ledOn = false;
        return;
    }
    bool hasHid = false;
    for (int i = 0; i < s_device.numInterfaces; i++) {
        if (s_device.interfaceClasses[i] == 0x03) {
            hasHid = true;
            break;
        }
    }
    if (hasHid) {
        uint32_t now = millis();
        if (now - s_lastBlinkMs >= 250) {
            s_ledOn = !s_ledOn;
            s_lastBlinkMs = now;
            if (s_ledOn) ledShowApp(255, 0, 0, 80);
            else ledShowApp(0, 0, 0, 0);
        }
    } else {
        ledShowApp(0, 255, 0, 60);
        s_ledOn = true;
    }
#else
    (void)0;
#endif
}

static void drawWelcome() {
    drawMainBorderWithTitle("BadUSB Hunter");
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    int y = 28;
    const int lh = uiLineH(dense) + 2;
    tft.drawString("Insert USB device", 8, y, 1);
    y += lh;
    tft.drawString("USB Host ready", 8, y, 1);
    y += lh + 4;
    tft.drawString("ESC exit", 8, y, 1);
}

static void drawDevice(const UsbDeviceInfo &device) {
    drawMainBorderWithTitle("BadUSB Hunter");
    const int dense = uiDenseFont();
    tft.setTextSize(dense);
    int y = 28;
    const int lh = uiLineH(dense) + 1;
    const int x0 = 8;

    if (device.isSuspicious) {
        tft.setTextColor(TFT_RED, kvxConfig.bgColor);
        tft.drawString("!SUSPICIOUS!", x0, y, 1);
    } else if (device.isHidOnly) {
        tft.setTextColor(TFT_YELLOW, kvxConfig.bgColor);
        tft.drawString("HID-Only Device", x0, y, 1);
    } else {
        tft.setTextColor(TFT_GREEN, kvxConfig.bgColor);
        tft.drawString("Device OK", x0, y, 1);
    }
    y += lh + 2;

    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    char vidpid[20];
    snprintf(vidpid, sizeof(vidpid), "VID:PID %04X:%04X", device.vid, device.pid);
    tft.drawString(vidpid, x0, y, 1);
    y += lh;
    tft.drawString(String("Class: ") + className(device.deviceClass), x0, y, 1);
    y += lh;
    tft.drawString("Interfaces: " + String(device.numInterfaces), x0, y, 1);
    y += lh;
    for (int i = 0; i < device.numInterfaces && i < 8; i++) {
        tft.drawString(" " + String(i) + ": " + className(device.interfaceClasses[i]), x0, y, 1);
        y += lh;
    }
    if (device.isSuspicious && device.suspicionReason.length()) {
        y += 2;
        tft.setTextColor(TFT_RED, kvxConfig.bgColor);
        tft.drawString(device.suspicionReason, x0, y, 1);
    }
    tft.setTextColor(kvxConfig.priColor, kvxConfig.bgColor);
    tft.drawString("ESC exit", x0, uiFooterY(dense), 1);
}

} // namespace

void badusbHunterMenu() {
    returnToMenu = false;
    s_client = nullptr;
    s_newAddr = 0;
    s_devGone = false;
    s_connected = false;
    s_device = {};
    s_lastBlinkMs = 0;
    s_ledOn = false;

    ledTakeExclusive();
    tft.fillScreen(kvxConfig.bgColor);
    drawWelcome();

    if (!installHost()) {
        displayError("USB Host failed", true);
        ledReleaseExclusive();
        return;
    }

    bool showingWelcome = true;

    while (!returnToMenu && !forceHome) {
        if (check(EscPress)) break;

        uint32_t eventFlags = 0;
        usb_host_lib_handle_events(0, &eventFlags);
        if (s_client) usb_host_client_handle_events(s_client, 0);

        uint8_t addr = s_newAddr;
        if (addr) {
            s_newAddr = 0;
            processDevice(addr);
            drawDevice(s_device);
            showingWelcome = false;
        }

        if (s_devGone) {
            s_devGone = false;
            s_connected = false;
            s_device = {};
            drawWelcome();
            showingWelcome = true;
            ledShowApp(0, 0, 0, 0);
        }

        // Also poll address list in case callback missed a hotplug.
        if (!s_connected && s_client) {
            uint8_t list[8];
            int n = 0;
            if (usb_host_device_addr_list_fill(8, list, &n) == ESP_OK && n > 0) {
                processDevice(list[0]);
                drawDevice(s_device);
                showingWelcome = false;
            }
        }

        updateLed();
        (void)showingWelcome;
        delay(40);
    }

    uninstallHost();
    ledShowApp(0, 0, 0, 0);
    ledReleaseExclusive();
}

#endif
