#include "menu_icons.h"

#include "ui_const.h"

#include "lvgl.h"

#include <cstring>

namespace view {
namespace {

const char* page_icon(const std::string& id) {
    if (id == "page:codec") return ICON_MENU_CODEC;
    if (id == "page:engcalc") return ICON_MENU_CALCS;
    if (id == "page:passgen") return ICON_MENU_PASSGEN;
    if (id == "page:totp") return ICON_MENU_TOTP;
    if (id == "page:combo") return ICON_MENU_COMBO;
    if (id == "page:qr") return ICON_MENU_QR;
    if (id == "page:barcode") return ICON_MENU_BARCODE;
    if (id == "page:pda") return ICON_MENU_PDA;
    if (id == "page:timer") return ICON_MENU_TIMER;
    if (id == "page:matrix") return ICON_CH_SCRIPTS;
    if (id == "page:color_main") return ICON_MENU_MAIN_CLR;
    if (id == "page:color_accent") return ICON_MENU_ACCENT;
    if (id == "page:color_bg") return ICON_MENU_BG;
    if (id == "page:brightness") return ICON_SUN;
    if (id == "page:dim") return ICON_MOON;
    if (id == "page:sound") return ICON_CH_FM;
    if (id == "page:power") return ICON_CH_CHARGE;
    if (id == "page:about") return ICON_MENU_ABOUT;
    if (id == "page:shortcuts") return ICON_CH_CONFIG;
    return ICON_CH_TOOLS;
}

} // namespace

const char* channel_icon(const char* id) {
    if (!id) return ICON_CH_TOOLS;
    if (std::strcmp(id, "wifi") == 0) return ICON_CH_WIFI;
    if (std::strcmp(id, "discovery") == 0) return ICON_CH_DISCOVERY;
    if (std::strcmp(id, "netops") == 0) return ICON_CH_NETOPS;
    if (std::strcmp(id, "ble") == 0) return ICON_CH_BLE;
    if (std::strcmp(id, "rf") == 0) return ICON_CH_RF;
    if (std::strcmp(id, "nrf24") == 0) return ICON_CH_NRF24;
    if (std::strcmp(id, "lora") == 0) return ICON_CH_LORA;
    if (std::strcmp(id, "fm") == 0) return ICON_CH_FM;
    if (std::strcmp(id, "infrared") == 0) return ICON_CH_IR;
    if (std::strcmp(id, "ethernet") == 0) return ICON_CH_ETH;
    if (std::strcmp(id, "usb") == 0) return ICON_CH_USB;
    if (std::strcmp(id, "gps") == 0) return ICON_CH_GPS;
    if (std::strcmp(id, "rfid") == 0) return ICON_CH_RFID;
    if (std::strcmp(id, "nfc") == 0) return ICON_CH_NFC;
    if (std::strcmp(id, "files") == 0) return ICON_CH_FILES;
    if (std::strcmp(id, "camera") == 0) return ICON_CH_CAMERA;
    if (std::strcmp(id, "gallery") == 0) return ICON_CH_GALLERY;
    if (std::strcmp(id, "scripts") == 0) return ICON_CH_SCRIPTS;
    if (std::strcmp(id, "pcconnect") == 0) return ICON_CH_USB;
    if (std::strcmp(id, "clock") == 0) return ICON_CH_CLOCK;
    if (std::strcmp(id, "charge") == 0) return ICON_CH_CHARGE;
    if (std::strcmp(id, "others") == 0) return ICON_CH_TOOLS;
    if (std::strcmp(id, "modules") == 0) return ICON_CH_MODULES;
    if (std::strcmp(id, "config") == 0) return ICON_CH_CONFIG;
    return ICON_CH_TOOLS;
}

const char* tool_icon(model::ToolId id) {
    switch (id) {
    case model::ToolId::Calculator:
        return ICON_MENU_CALCS;
    case model::ToolId::MediaPlayer:
    case model::ToolId::FmBroadcast:
        return ICON_PLAY;
    case model::ToolId::FmStop:
        return ICON_STOP;
    case model::ToolId::Dtmf:
    case model::ToolId::Morse:
    case model::ToolId::Microphone:
    case model::ToolId::FmSpectrum:
    case model::ToolId::RfSpectrum:
    case model::ToolId::Nrf24Spectrum:
        return ICON_CH_FM;
    case model::ToolId::Mqtt:
    case model::ToolId::NetToolkit:
    case model::ToolId::WireGuard:
        return ICON_CH_NETOPS;
    case model::ToolId::LlmChat:
    case model::ToolId::UartLlm:
    case model::ToolId::UartShell:
    case model::ToolId::SshHint:
    case model::ToolId::ScriptsRunner:
    case model::ToolId::PcConnectScripts:
        return ICON_CH_SCRIPTS;
    case model::ToolId::DroneId:
    case model::ToolId::GpsTracker:
    case model::ToolId::GpsLive:
        return ICON_CH_GPS;
    case model::ToolId::FileShare:
    case model::ToolId::FilesBrowser:
    case model::ToolId::FilesShareHint:
    case model::ToolId::DeadDrop:
    case model::ToolId::GpsCaptures:
    case model::ToolId::GpsFolder:
        return ICON_CH_FILES;
    case model::ToolId::HidRemote:
        return ICON_CH_USB;
    case model::ToolId::Roku:
    case model::ToolId::TvBGone:
    case model::ToolId::IrUse:
    case model::ToolId::IrLearn:
    case model::ToolId::IrBrowse:
    case model::ToolId::IrRead:
    case model::ToolId::IrFavorites:
        return ICON_CH_IR;
    case model::ToolId::IButtonHint:
    case model::ToolId::IButtonRead:
    case model::ToolId::RfidRead:
    case model::ToolId::RfidScan:
        return ICON_CH_RFID;
    case model::ToolId::WallOfFlipper:
    case model::ToolId::WallOfAirtag:
    case model::ToolId::SkimmerDetector:
    case model::ToolId::PineapHunter:
    case model::ToolId::BleHunter:
        return ICON_CH_DISCOVERY;
    case model::ToolId::WifiAnalyzer:
    case model::ToolId::WifiConnect:
    case model::ToolId::WifiAp:
    case model::ToolId::WifiApInfo:
    case model::ToolId::WifiJamDetect:
    case model::ToolId::WifiDeauthIds:
    case model::ToolId::OpenNetworks:
        return ICON_CH_WIFI;
    case model::ToolId::BadUsbHunter:
        return ICON_CH_USB;
    case model::ToolId::NfcRead:
    case model::ToolId::NfcScan:
    case model::ToolId::NfcWriteNdef:
    case model::ToolId::NfcEmulateNdef:
        return ICON_CH_NFC;
    case model::ToolId::Printer:
        return ICON_INFO;
    case model::ToolId::CapProbe:
    case model::ToolId::BusScan:
        return ICON_CH_MODULES;
    default:
        break;
    }
    if (const auto* tool = model::find_tool(id)) return channel_icon(tool->channel_id);
    return ICON_CH_TOOLS;
}

const char* icon_for_target(const model::LaunchTarget& target) {
    if (target.kind == model::LaunchKind::Channel) {
        const char* id = target.id.c_str();
        if (std::strncmp(id, "ch:", 3) == 0) return channel_icon(id + 3);
        return channel_icon(nullptr);
    }
    if (target.kind == model::LaunchKind::Page) return page_icon(target.id);
    return tool_icon(target.tool);
}

void add_target_icon(lv_obj_t* row, const lv_font_t* icon_font, const KvxTokens& tokens,
                     const model::LaunchTarget& target) {
    if (!row) return;
    const char* glyph = icon_for_target(target);
    if (!glyph || !glyph[0]) return;
    const bool menu = target.kind == model::LaunchKind::Channel;
    lv_obj_t* parent = row;
    if (!menu && tokens.light_bg) {
        parent = lv_obj_create(row);
        lv_obj_remove_style_all(parent);
        lv_obj_set_size(parent, 14, 14);
        lv_obj_set_style_radius(parent, 3, 0);
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x202020), 0);
        lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
        lv_obj_set_style_margin_right(parent, 4, 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    }
    auto* icon = lv_label_create(parent);
    lv_label_set_text(icon, glyph);
    if (icon_font) lv_obj_set_style_text_font(icon, icon_font, 0);
    lv_obj_set_style_text_color(icon, menu ? tokens.accent : lv_color_hex(0xFFFFFF), 0);
    if (parent == row) {
        lv_obj_set_style_pad_right(icon, 4, 0);
    } else {
        lv_obj_center(icon);
    }
}

} // namespace view
