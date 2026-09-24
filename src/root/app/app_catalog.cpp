#include "root/app/app_catalog.h"

#include "root/hal/led_control.h"
#include "root/input/mykeyboard.h"
#include "root/net/webInterface.h"
#include "root/net/wifi_common.h"
#include "root/net/wg.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include "root/ui/main_menu.h"
#include "root/ui/settings.h"
#include "root/app/utils.h"
#include <menu_registry.h>
#include <globals.h>

#include "menu/wifi/ap_info.h"
#include "menu/wifi/clients.h"
#include "menu/wifi/evil_portal.h"
#include "menu/wifi/karma_attack.h"
#include "menu/wifi/netcut.h"
#include "menu/wifi/responder.h"
#include "menu/wifi/socks4_proxy.h"
#include "menu/wifi/tcp_utils.h"
#include "menu/wifi/wifi_atks.h"
#include "menu/ble/ble_common.h"
#include "menu/ble/ble_spam.h"
#include "menu/ble/hid_remote/hid_remote.h"
#include "menu/gps/wardriving.h"
#include "menu/nrf24/nrf_common.h"
#include "menu/nrf24/nrf_jammer.h"
#include "menu/nrf24/nrf_spectrum.h"
#include "menu/rf/rf_scan.h"
#include "menu/rf/rf_spectrum.h"
#include "menu/rf/rf_waterfall.h"
#include "menu/rf/rf_jammer.h"
#include "menu/infrared/TV-B-Gone.h"
#include "menu/infrared/custom_ir.h"
#include "menu/infrared/ir_read.h"
#include "menu/infrared/kremote/kremote.h"
#include "menu/rfid/chameleon.h"
#include "menu/rfid/tag_o_matic.h"
#include "menu/others/badusb_ble/ducky_typer.h"
#include "menu/others/calculator.h"
#include "menu/others/qrcode_menu.h"
#include "menu/others/timer.h"
#if defined(HAS_NS4168_SPKR)
#include "menu/others/audio.h"
#include "menu/others/media_player.h"
#elif defined(BUZZ_PIN)
#include "menu/others/audio.h"
#endif
#ifndef LITE_VERSION
#include "menu/others/pda/pda_menu.h"
#endif
#include "menu/charge/charge_screen.h"

#ifndef LITE_VERSION
#include "menu/ethernet/ARPScanner.h"
#include "menu/wifi/channel_analyzer.h"
#include "menu/wifi/jam_detect.h"
#include "menu/wifi/sniffer.h"
#include "menu/wifi/pwnagotchi/pwnagotchi.h"
#include "menu/wifi/wifi_recover.h"
#include "menu/ble/BLE_Suite.h"
#include "menu/ble/ble_ninebot.h"
#include "menu/nrf24/nrf_mousejack.h"
#include "menu/rf/record.h"
#include "menu/rf/rf_bruteforce.h"
#include "menu/rf/rf_listen.h"
#include "menu/rf/rf_send.h"
#include "menu/infrared/ir_jammer.h"
#include "menu/rfid/amiibo.h"
#include "menu/rfid/emv_reader.hpp"
#include "menu/rfid/pn532ble.h"
#include "menu/rfid/PN532KillerTools.h"
#include "menu/rfid/rfid125.h"
#include "menu/gps/gps_tracker.h"
#include "menu/netops/reverseShell/reverseShell.h"
#include "menu/others/ibutton.h"
#include "menu/others/mic.h"
#include "root/serial/connect/file_sharing.h"
#include "root/storage/massStorage.h"
#include <esp_netif.h>
#else
#include "menu/ble/ble_sniffer.h"
#endif

#if defined(EVIL_EXTENSIONS)
#include "menu/ble/findmy/findmy.h"
#include "menu/ble/name_flood/name_flood.h"
#include "menu/ble/skimmer/skimmer.h"
#include "menu/ble/wall_of_airtag/wall_of_airtag.h"
#include "menu/gps/wardriving_master/wardriving_master.h"
#include "menu/infrared/tagtinker/tagtinker.h"
#include "menu/netops/autodiscover/autodiscover.h"
#include "menu/netops/cctv/cctv.h"
#include "menu/netops/chat_mesh/chat_mesh.h"
#include "menu/netops/ciw/ciw.h"
#include "menu/netops/dhcp_starvation/dhcp_starvation.h"
#include "menu/netops/hijack/hijack.h"
#include "menu/netops/honeypot/honeypot.h"
#include "menu/netops/imsi_eap/imsi_eap.h"
#include "menu/netops/ldap/ldap.h"
#include "menu/netops/ntlm/ntlm.h"
#include "menu/netops/printer/printer.h"
#include "menu/netops/reverse_tcp/reverse_tcp.h"
#include "menu/netops/rogue_dhcp/rogue_dhcp.h"
#include "menu/netops/sip/sip.h"
#include "menu/netops/skyjack/skyjack.h"
#include "menu/netops/ssdp/ssdp.h"
#include "menu/netops/switch_dns/switch_dns.h"
#include "menu/netops/uart_shell/uart_shell.h"
#include "menu/netops/upnp/upnp.h"
#include "menu/netops/web_crawler/web_crawler.h"
#include "menu/netops/wpad/wpad.h"
#include "menu/others/llm_chat/llm_chat.h"
#include "menu/wifi/aircrack/aircrack.h"
#include "menu/wifi/c5_serial/c5_serial.h"
#include "menu/wifi/csi_radar/csi_radar.h"
#include "menu/wifi/dead_drop/dead_drop.h"
#include "menu/wifi/handshake_master/handshake_master.h"
#include "menu/wifi/open_wifi/open_wifi.h"
#include "menu/wifi/probe/probe.h"
#include "menu/wifi/wall_of_flipper/wall_of_flipper.h"
#endif

static bool alwaysOn() { return true; }
#ifndef LITE_VERSION
static bool notLite() { return true; }
#else
static bool notLite() { return false; }
#endif
#if defined(EVIL_EXTENSIONS)
static bool evilOn() { return true; }
#else
static bool evilOn() { return false; }
#endif
static bool devModeOn() { return kvxConfig.devMode; }

// --- WiFi ---
static void launchWifiSta() { wifiConnectMenu(WIFI_STA); }
static void launchWifiAp() {
    wifiConnectMenu(WIFI_AP);
    displayInfo("pwd: " + kvxConfig.wifiAp.pwd, true);
}
static void launchWifiOff() { wifiDisconnect(); }
static void launchApInfo() { displayAPInfo(); }
static void launchWifiAtk() { wifi_atk_menu(); }
static void launchEvilPortal() { EvilPortal(); }
static void launchNetcut() { netcutMenu(); }
static void launchWifiConfig() { mainMenu.wifiMenu.configMenu(); }
static void launchBeacon() { beaconAttack(); }
static void launchDeauthFlood() { deauthFloodAttack(); }
static void launchEnhancedDeauth() { enhancedDeauthMenu(); }
#ifndef LITE_VERSION
static void launchListenTcp() { listenTcpPort(); }
static void launchClientTcp() { clientTCP(); }
static void launchSocks4() { socks4Proxy(1080); }
static void launchTelnet() { telnet_setup(); }
static void launchSsh() { ssh_setup(""); }
static void launchSniffer() { sniffer_setup(); }
static void launchChannelAnalyzer() { channel_analyzer_setup(); }
static void launchJamDetect() { jam_detect_setup(); }
static void launchScanHosts() {
    bool doScan = true;
    if (!WiFi.isConnected()) doScan = wifiConnectMenu();
    if (!doScan) return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) ARPScanner{netif};
    else displayError("No netif", true);
}
static void launchWireguard() { wg_setup(); }
static void launchResponder() { responder(); }
static void launchKvxgotchi() { kvxgotchi_start(); }
static void launchWifiRecover() { wifi_recover_menu(); }
static void launchKarma() { karma_setup(); }
#endif
#if defined(EVIL_EXTENSIONS)
static void launchProbe() { probeMenu(); }
static void launchHandshakes() { handshakeMasterMenu(); }
static void launchWallFlipper() { wallOfFlipperMenu(); }
static void launchDeadDrop() { deadDropMenu(); }
static void launchOpenWifi() { openWifiMenu(); }
static void launchAircrack() { aircrackMenu(); }
static void launchCsiRadar() { csiRadarMenu(); }
static void launchC5Serial() { c5SerialMenu(); }
#endif

// --- BLE ---
#ifndef LITE_VERSION
static void launchHidBle() { hidRemoteMenu(HID_REMOTE_LAUNCH_BLE); }
static void launchMediaCmds() { MediaCommands(hid_ble, true); }
static void launchBleScan() { ble_scan(); }
static void launchIbeacon() { ibeacon("Bruce", "e4c159a0-8c82-11e6-bdf4-0800200c9a66", 0x004C); }
static void launchBadBle() { ducky_setup(hid_ble, true); }
static void launchBleKeyboard() { ducky_keyboard(hid_ble, true); }
static void launchBleSuite() { BleSuiteMenu(); }
static void launchNinebot() { BLENinebot(); }
static void launchPresenter() { PresenterMode(hid_ble, true); }
#else
static void launchBleSniffer() { BLE_SnifferMenu(); }
#endif
static void launchBleSpam() { spamMenu(); }
#if defined(EVIL_EXTENSIONS)
static void launchNameFlood() { nameFloodMenu(); }
static void launchWallAirtag() { wallOfAirtagMenu(); }
static void launchFindMy() { findMyMenu(); }
static void launchSkimmer() { skimmerMenu(); }
#endif

// --- RF ---
static void launchRfScan() { RFScan(); }
static void launchRfSpectrum() { rf_spectrum(); }
static void launchRfConfig() { mainMenu.rfMenu.configMenu(); }
#ifndef LITE_VERSION
static void launchRfRawRecord() { rf_raw_record(); }
static void launchCustomSubghz() { sendCustomRF(); }
static void launchRssiSpectrum() { rf_CC1101_rssi(); }
static void launchSpectogram() { rf_waterfall(); }
static void launchSquareWave() { rf_SquareWave(); }
#if (defined(BUZZ_PIN) || (defined(HAS_NS4168_SPKR) && defined(RF_LISTEN_H)))
static void launchRfListen() { rf_listen(); }
#endif
static void launchRfBruteforce() { rf_bruteforce(); }
static void launchRfJammer() { RFJammer(true); }
#endif

// --- IR ---
static void launchKremote() { kremoteMenu(); }
static void launchTvBGone() { StartTvBGone(); }
static void launchCustomIr() { otherIRcodes(); }
static void launchIrRead() { IrRead(); }
static void launchIrConfig() { mainMenu.irMenu.configMenu(); }
#ifndef LITE_VERSION
static void launchIrJammer() { startIrJammer(); }
#endif
#if defined(EVIL_EXTENSIONS)
static void launchTagTinker() { tagTinkerMenu(); }
#endif

// --- RFID ---
#if !defined(REMOVE_RFID_HW_INTERFACE)
static void launchTagRead() { TagOMatic(); }
static void launchTagScan() { TagOMatic(TagOMatic::SCAN_MODE); }
static void launchTagLoad() { TagOMatic(TagOMatic::LOAD_MODE); }
static void launchTagErase() { TagOMatic(TagOMatic::ERASE_MODE); }
static void launchTagWriteNdef() { TagOMatic(TagOMatic::WRITE_NDEF_MODE); }
static void launchTagEmulateNdef() { TagOMatic(TagOMatic::EMULATE_NDEF_MODE); }
#endif
static void launchChameleon() { Chameleon(); }
static void launchRfidConfig() { mainMenu.rfidMenu.configMenu(); }
#ifndef LITE_VERSION
static void launchEmv() { EMVReader(); }
static void launchRfid125() { RFID125(); }
static void launchAmiibo() { Amiibo(); }
static void launchPn532Ble() { Pn532ble(); }
#if !defined(REMOVE_RFID_HW_INTERFACE)
static void launchPn532Killer() { PN532KillerTools(); }
#endif
#endif

// --- GPS ---
static void launchWardrivingMenu() { mainMenu.gpsMenu.wardrivingMenu(); }
static void launchWardrivingWifi() { Wardriving(true, false); }
static void launchWardrivingBle() { Wardriving(false, true); }
static void launchWardrivingBoth() { Wardriving(true, true); }
static void launchGpsConfig() { mainMenu.gpsMenu.configMenu(); }
#ifndef LITE_VERSION
static void launchGpsTracker() { GPSTracker(); }
#endif
#if defined(EVIL_EXTENSIONS)
static void launchWardrivingMaster() { wardrivingMasterMenu(); }
#endif

// --- NRF24 ---
static void launchNrfInfo() { nrf_info(); }
static void launchNrfSpectrum() { nrf_spectrum(); }
static void launchNrfJammer() { nrf_jammer(); }
static void launchNrfConfig() { mainMenu.nrf24Menu.configMenu(); }
#ifndef LITE_VERSION
static void launchNrfMousejack() { nrf_mousejack(); }
#endif

// --- NetOps ---
#ifndef LITE_VERSION
static void launchReverseShell() { ReverseShell(); }
static void launchNetopsScanHosts() { launchScanHosts(); }
#endif
#if defined(EVIL_EXTENSIONS)
static void launchWebCrawler() { webCrawlerMenu(); }
static void launchReverseTcp() { reverseTcpMenu(); }
static void launchDhcpStarvation() { dhcpStarvationMenu(); }
static void launchRogueDhcp() { rogueDhcpMenu(); }
static void launchSwitchDns() { switchDnsMenu(); }
static void launchHijack() { hijackMenu(); }
static void launchWpad() { wpadMenu(); }
static void launchNtlm() { ntlmMenu(); }
static void launchUartShell() { uartShellMenu(); }
static void launchPrinter() { printerMenu(); }
static void launchHoneypot() { honeypotMenu(); }
static void launchChatMesh() { chatMeshMenu(); }
static void launchSip() { sipMenu(); }
static void launchSsdp() { ssdpMenu(); }
static void launchSkyjack() { skyjackMenu(); }
static void launchLdap() { ldapMenu(); }
static void launchAutodiscover() { autodiscoverMenu(); }
static void launchCiw() { ciwMenu(); }
static void launchImsiEap() { imsiEapMenu(); }
static void launchUpnp() { upnpMenu(); }
static void launchCctv() { cctvMenu(); }
#endif

// --- Files / USB / Others / Clock / Charge ---
static void launchWebUi() { loopOptionsWebUi(); }
static void launchLittleFs() { loopSD(LittleFS); }
static void launchSdCard() {
    if (setupSdCard()) loopSD(SD);
}
static void launchQrcode() { qrcode_menu(); }
static void launchCalculator() { calculatorApp(); }
#if defined(HAS_NS4168_SPKR)
static void launchMediaPlayer() { mediaPlayerApp(); }
#endif
#ifndef LITE_VERSION
static void launchPda() { pdaMenu(); }
#endif
static void launchTimer() { Timer(); }
static void launchClock() { runClockLoop(true); }
static void launchCharge() { runChargeLoop(); }
#ifndef LITE_VERSION
static void launchFileSend() { FileSharing().sendFile(); }
static void launchFileRecv() { FileSharing().receiveFile(); }
static void launchHidUsb() { hidRemoteMenu(HID_REMOTE_LAUNCH_USB); }
static void launchBadUsb() { ducky_setup(hid_usb, false); }
static void launchIbutton() { setup_ibutton(); }
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
static void launchMicTest() { mic_test(); }
static void launchMicRecord() { mic_record_app(); }
static void launchMicMenu() { mainMenu.othersMenu.micMenu(); }
#endif
#if defined(SOC_USB_OTG_SUPPORTED)
static void launchMassStorage() { MassStorage(); }
#endif
#endif
#if defined(EVIL_EXTENSIONS)
static void launchLlmChat() { llmChatMenu(); }
#endif

// --- Settings ---
static void launchCfgDisplay() { mainMenu.configMenu.displayUIMenu(); }
#ifdef HAS_RGB_LED
static void launchCfgLed() { mainMenu.configMenu.ledMenu(); }
static void launchLedColor() { setLedColorConfig(); }
static void launchLedEffect() { setLedEffectConfig(); }
static void launchLedBright() { setLedBrightnessConfig(); }
#endif
#if !defined(LITE_VERSION) && (defined(BUZZ_PIN) || defined(HAS_NS4168_SPKR))
static void launchCfgAudio() { mainMenu.configMenu.audioMenu(); }
#endif
static void launchCfgSystem() { mainMenu.configMenu.systemMenu(); }
static void launchCfgPower() { mainMenu.configMenu.powerMenu(); }
static void launchCfgAdvanced() { mainMenu.configMenu.advancedMenu(); }
static void launchCfgPins() { mainMenu.configMenu.pinsMenu(); }
static void launchCfgDev() { mainMenu.configMenu.devMenu(); }
static void launchBrightness() { setBrightnessMenu(); }
static void launchDimmer() { setDimmerTimeMenu(); }
static void launchUiColor() { setUIColor(); }
static void launchAccentColor() { setAccentColor(); }
static void launchTheme() { setTheme(); }
static void launchSetClock() { setClock(); }
static void launchStartupApp() { setStartupApp(); }
static void launchKbLang() { setKeyboardLanguage(); }
static void launchNetCreds() { setNetworkCredsMenu(); }
static void launchDeviceInfo() { showDeviceInfo(); }
static void launchDeepSleep() { goToDeepSleep(); }
static void launchSleepMode() { setSleepMode(); }
#ifndef LITE_VERSION
static void launchBadUsbBleCfg() { setBadUSBBLEMenu(); }
#endif

static bool launchMenuById(const String &id) {
    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (!menuDescriptorAvailable(desc)) continue;
        if (id == desc.id || id == desc.label) {
            if (desc.item) desc.item->optionsMenu();
            return true;
        }
    }
    return false;
}

const std::vector<AppCatalogItem> &appCatalogItems() {
    static const std::vector<AppCatalogItem> items = {
        // WiFi
        {"wifi_sta", "Connect STA", "WiFi", false, alwaysOn, launchWifiSta},
        {"wifi_ap", "Start AP", "WiFi", false, alwaysOn, launchWifiAp},
        {"wifi_off", "Turn Off WiFi", "WiFi", false, alwaysOn, launchWifiOff},
        {"wifi_ap_info", "AP info", "WiFi", false, alwaysOn, launchApInfo},
        {"wifi_atks", "Wifi Atks", "WiFi", false, alwaysOn, launchWifiAtk},
        {"evil_portal", "Evil Portal", "WiFi", false, alwaysOn, launchEvilPortal},
        {"netcut", "NetCut", "WiFi", false, alwaysOn, launchNetcut},
        {"wifi_config", "WiFi Config", "WiFi", false, alwaysOn, launchWifiConfig},
        {"beacon_spam", "Beacon SPAM", "WiFi", false, alwaysOn, launchBeacon},
        {"deauth_flood", "Deauth Flood", "WiFi", false, alwaysOn, launchDeauthFlood},
        {"enhanced_deauth", "Enhanced Deauth", "WiFi", false, alwaysOn, launchEnhancedDeauth},
#ifndef LITE_VERSION
        {"listen_tcp", "Listen TCP", "WiFi", false, notLite, launchListenTcp},
        {"client_tcp", "Client TCP", "WiFi", false, notLite, launchClientTcp},
        {"socks4", "SOCKS4 Proxy", "WiFi", false, notLite, launchSocks4},
        {"telnet", "TelNET", "WiFi", false, notLite, launchTelnet},
        {"ssh", "SSH", "WiFi", false, notLite, launchSsh},
        {"sniffer", "Sniffer", "WiFi", true, notLite, launchSniffer},
        {"channel_analyzer", "Channel Analyzer", "WiFi", true, notLite, launchChannelAnalyzer},
        {"jam_detect", "Jam Detect", "WiFi", true, notLite, launchJamDetect},
        {"scan_hosts", "Scan Hosts", "WiFi", true, notLite, launchScanHosts},
        {"wireguard", "Wireguard", "WiFi", false, notLite, launchWireguard},
        {"responder", "Responder", "WiFi", false, notLite, launchResponder},
        {"kvxgotchi", "Kvxgotchi", "WiFi", false, notLite, launchKvxgotchi},
        {"wifi_recover", "WiFi Pass Recovery", "WiFi", false, notLite, launchWifiRecover},
        {"karma", "Karma Attack", "WiFi", false, notLite, launchKarma},
#endif
#if defined(EVIL_EXTENSIONS)
        {"probes", "Probes", "WiFi", false, evilOn, launchProbe},
        {"handshakes", "Handshakes", "WiFi", false, evilOn, launchHandshakes},
        {"wall_of_flipper", "Wall Of Flipper", "WiFi", true, evilOn, launchWallFlipper},
        {"dead_drop", "WiFi Dead Drop", "WiFi", false, evilOn, launchDeadDrop},
        {"open_wifi", "Open Wifi Checker", "WiFi", true, evilOn, launchOpenWifi},
        {"aircrack", "Aircrack", "WiFi", false, evilOn, launchAircrack},
        {"csi_radar", "CSI Radar", "WiFi", true, evilOn, launchCsiRadar},
        {"c5_serial", "ESP32C5 Serial", "WiFi", false, evilOn, launchC5Serial},
#endif

        // BLE
#ifndef LITE_VERSION
        {"hid_ble", "kvxkeyboard HID", "BLE", false, notLite, launchHidBle},
        {"media_cmds", "Media Cmds", "BLE", false, notLite, launchMediaCmds},
        {"ble_scan", "BLE Scan", "BLE", true, alwaysOn, launchBleScan},
        {"ibeacon", "iBeacon", "BLE", false, notLite, launchIbeacon},
        {"bad_ble", "Bad BLE", "BLE", false, notLite, launchBadBle},
        {"ble_keyboard", "BLE Keyboard", "BLE", false, notLite, launchBleKeyboard},
        {"ble_suite", "BLE Suite", "BLE", false, notLite, launchBleSuite},
        {"ninebot", "Ninebot", "BLE", false, notLite, launchNinebot},
        {"presenter", "Presenter", "BLE", false, notLite, launchPresenter},
#else
        {"ble_sniffer", "BLE Sniffer", "BLE", false, alwaysOn, launchBleSniffer},
#endif
        {"ble_spam", "BLE Spam", "BLE", false, alwaysOn, launchBleSpam},
#if defined(EVIL_EXTENSIONS)
        {"name_flood", "BLE Name Flood", "BLE", false, evilOn, launchNameFlood},
        {"wall_of_airtag", "Wall Of Airtag", "BLE", true, evilOn, launchWallAirtag},
        {"findmy", "FindMyEvil", "BLE", false, evilOn, launchFindMy},
        {"skimmer", "Skimmer Detector", "BLE", true, evilOn, launchSkimmer},
#endif

        // RF
        {"rf_scan", "Scan/copy", "RF", false, alwaysOn, launchRfScan},
        {"rf_spectrum", "RF Spectrum", "RF", true, alwaysOn, launchRfSpectrum},
        {"rf_config", "RF Config", "RF", false, alwaysOn, launchRfConfig},
#ifndef LITE_VERSION
        {"rf_raw_record", "Record RAW", "RF", false, notLite, launchRfRawRecord},
        {"custom_subghz", "Custom SubGhz", "RF", false, notLite, launchCustomSubghz},
        {"rssi_spectrum", "RSSI Spectrum", "RF", true, notLite, launchRssiSpectrum},
        {"spectogram", "Spectogram", "RF", true, notLite, launchSpectogram},
        {"squarewave_spec", "SquareWave Spec", "RF", false, notLite, launchSquareWave},
#if (defined(BUZZ_PIN) || (defined(HAS_NS4168_SPKR) && defined(RF_LISTEN_H)))
        {"rf_listen", "Listen", "RF", false, notLite, launchRfListen},
#endif
        {"rf_bruteforce", "Bruteforce", "RF", false, notLite, launchRfBruteforce},
        {"rf_jammer", "Jammer", "RF", false, notLite, launchRfJammer},
#endif

        // IR
        {"kremote", "Universal remote", "IR", false, alwaysOn, launchKremote},
        {"tv_b_gone", "TV-B-Gone", "IR", false, alwaysOn, launchTvBGone},
        {"custom_ir", "Custom IR", "IR", false, alwaysOn, launchCustomIr},
        {"ir_read", "IR Read", "IR", false, alwaysOn, launchIrRead},
        {"ir_config", "IR Config", "IR", false, alwaysOn, launchIrConfig},
#ifndef LITE_VERSION
        {"ir_jammer", "IR Jammer", "IR", false, notLite, launchIrJammer},
#endif
#if defined(EVIL_EXTENSIONS)
        {"tagtinker", "TagTinker ESL", "IR", false, evilOn, launchTagTinker},
#endif

        // RFID
#if !defined(REMOVE_RFID_HW_INTERFACE)
        {"rfid_read", "Read tag", "RFID", false, alwaysOn, launchTagRead},
        {"rfid_scan", "Scan tags", "RFID", false, alwaysOn, launchTagScan},
        {"rfid_load", "Load file", "RFID", false, alwaysOn, launchTagLoad},
        {"rfid_erase", "Erase data", "RFID", false, alwaysOn, launchTagErase},
        {"rfid_write_ndef", "Write NDEF", "RFID", false, alwaysOn, launchTagWriteNdef},
        {"rfid_emulate_ndef", "Emulate NDEF", "RFID", false, alwaysOn, launchTagEmulateNdef},
#endif
        {"chameleon", "Chameleon", "RFID", false, alwaysOn, launchChameleon},
        {"rfid_config", "RFID Config", "RFID", false, alwaysOn, launchRfidConfig},
#ifndef LITE_VERSION
        {"emv_reader", "Read EMV", "RFID", false, notLite, launchEmv},
        {"rfid125", "Read 125kHz", "RFID", false, notLite, launchRfid125},
        {"amiibo", "Amiibolink", "RFID", false, notLite, launchAmiibo},
        {"pn532_ble", "PN532 BLE", "RFID", false, notLite, launchPn532Ble},
#if !defined(REMOVE_RFID_HW_INTERFACE)
        {"pn532_uart", "PN532 UART", "RFID", false, notLite, launchPn532Killer},
#endif
#endif

        // GPS
        {"wardriving_menu", "Wardriving", "GPS", false, alwaysOn, launchWardrivingMenu},
        {"wardriving_wifi", "WD WiFi", "GPS", false, alwaysOn, launchWardrivingWifi},
        {"wardriving_ble", "WD BLE", "GPS", false, alwaysOn, launchWardrivingBle},
        {"wardriving", "WD Both", "GPS", true, alwaysOn, launchWardrivingBoth},
        {"gps_config", "GPS Config", "GPS", false, alwaysOn, launchGpsConfig},
#ifndef LITE_VERSION
        {"gps_tracker", "GPS Tracker", "GPS", false, notLite, launchGpsTracker},
#endif
#if defined(EVIL_EXTENSIONS)
        {"wardriving_master", "Wardriving Master", "GPS", true, evilOn, launchWardrivingMaster},
#endif

        // NRF24
        {"nrf_info", "NRF Info", "NRF24", false, alwaysOn, launchNrfInfo},
        {"nrf_spectrum", "NRF Spectrum", "NRF24", true, alwaysOn, launchNrfSpectrum},
        {"nrf_jammer", "NRF Jammer", "NRF24", false, alwaysOn, launchNrfJammer},
        {"nrf_config", "NRF Config", "NRF24", false, alwaysOn, launchNrfConfig},
#ifndef LITE_VERSION
        {"nrf_mousejack", "MouseJack", "NRF24", false, notLite, launchNrfMousejack},
#endif

        // NetOps
#ifndef LITE_VERSION
        {"reverse_shell", "Reverse Shell", "NetOps", false, notLite, launchReverseShell},
        {"netops_scan_hosts", "Scan Hosts", "NetOps", false, notLite, launchNetopsScanHosts},
#endif
#if defined(EVIL_EXTENSIONS)
        {"web_crawler", "Web Crawler", "NetOps", false, evilOn, launchWebCrawler},
        {"reverse_tcp", "Reverse TCP Tunnel", "NetOps", false, evilOn, launchReverseTcp},
        {"dhcp_starvation", "DHCP Starvation", "NetOps", false, evilOn, launchDhcpStarvation},
        {"rogue_dhcp", "Rogue DHCP", "NetOps", false, evilOn, launchRogueDhcp},
        {"switch_dns", "Switch DNS", "NetOps", false, evilOn, launchSwitchDns},
        {"hijack", "Network Hijacking", "NetOps", false, evilOn, launchHijack},
        {"wpad", "WPAD Abuse", "NetOps", false, evilOn, launchWpad},
        {"ntlm", "NTLMv2", "NetOps", false, evilOn, launchNtlm},
        {"uart_shell", "UART Shell", "NetOps", false, evilOn, launchUartShell},
        {"printer", "Printer Tools", "NetOps", false, evilOn, launchPrinter},
        {"honeypot", "HoneyPot", "NetOps", false, evilOn, launchHoneypot},
        {"chat_mesh", "EvilChatMesh", "NetOps", false, evilOn, launchChatMesh},
        {"sip", "SIP Toolkit", "NetOps", false, evilOn, launchSip},
        {"ssdp", "SSDP Poisoner", "NetOps", false, evilOn, launchSsdp},
        {"skyjack", "SkyJack", "NetOps", false, evilOn, launchSkyjack},
        {"ldap", "LDAP Dump", "NetOps", false, evilOn, launchLdap},
        {"autodiscover", "Autodiscover Abuse", "NetOps", false, evilOn, launchAutodiscover},
        {"ciw", "CIW Zeroclick", "NetOps", false, evilOn, launchCiw},
        {"imsi_eap", "EAP Identity Sniff", "NetOps", false, evilOn, launchImsiEap},
        {"upnp", "UPnP Tools", "NetOps", true, evilOn, launchUpnp},
        {"cctv", "CCTV Toolkit", "NetOps", true, evilOn, launchCctv},
#endif

        // Files
        {"webui", "WebUI", "Files", false, alwaysOn, launchWebUi},
        {"littlefs", "LittleFS", "Files", false, alwaysOn, launchLittleFs},
        {"sd_card", "SD Card", "Files", false, alwaysOn, launchSdCard},
#ifndef LITE_VERSION
        {"file_send", "Send File", "Files", false, notLite, launchFileSend},
        {"file_recv", "Recv File", "Files", false, notLite, launchFileRecv},
#if defined(SOC_USB_OTG_SUPPORTED)
        {"mass_storage_files", "Mass Storage", "Files", false, notLite, launchMassStorage},
#endif
#endif

        // USB
#ifndef LITE_VERSION
        {"hid_usb", "kvxkeyboard HID", "USB", false, notLite, launchHidUsb},
        {"bad_usb", "BadUSB", "USB", false, notLite, launchBadUsb},
#if defined(SOC_USB_OTG_SUPPORTED)
        {"mass_storage", "Mass Storage", "USB", false, notLite, launchMassStorage},
#endif
#endif

        // Tools
        {"qrcode", "QRCodes", "Tools", false, alwaysOn, launchQrcode},
        {"tools_calc", "Calculator", "Tools", false, alwaysOn, launchCalculator},
#if defined(HAS_NS4168_SPKR)
        {"media_player", "Media Player", "Tools", false, notLite, launchMediaPlayer},
#endif
#ifndef LITE_VERSION
        {"pda", "PDA", "Tools", false, notLite, launchPda},
        {"ibutton", "iButton", "Tools", false, notLite, launchIbutton},
#if defined(MIC_SPM1423) || defined(MIC_INMP441)
        {"mic_menu", "Microphone", "Tools", false, notLite, launchMicMenu},
        {"mic_test", "Mic Spectrum", "Tools", false, notLite, launchMicTest},
        {"mic_record", "Mic Record", "Tools", false, notLite, launchMicRecord},
#endif
#endif
#if defined(EVIL_EXTENSIONS)
        {"llm_chat", "LLM Chat", "Tools", false, evilOn, launchLlmChat},
#endif

        // Clock / Charge
        {"clock", "Clock", "Clock", false, alwaysOn, launchClock},
        {"timer", "Timer", "Clock", false, alwaysOn, launchTimer},
        {"charge", "Charge", "Clock", false, alwaysOn, launchCharge},

        // Settings
        {"cfg_display", "Display & UI", "Settings", false, alwaysOn, launchCfgDisplay},
#ifdef HAS_RGB_LED
        {"cfg_led", "LED Config", "Settings", false, alwaysOn, launchCfgLed},
        {"led_color", "LED Color", "Settings", false, alwaysOn, launchLedColor},
        {"led_effect", "LED Effect", "Settings", false, alwaysOn, launchLedEffect},
        {"led_bright", "LED Brightness", "Settings", false, alwaysOn, launchLedBright},
#endif
#if !defined(LITE_VERSION) && (defined(BUZZ_PIN) || defined(HAS_NS4168_SPKR))
        {"cfg_audio", "Audio Config", "Settings", false, notLite, launchCfgAudio},
#endif
        {"cfg_system", "System Config", "Settings", false, alwaysOn, launchCfgSystem},
        {"cfg_power", "Power", "Settings", false, alwaysOn, launchCfgPower},
        {"cfg_advanced", "Advanced", "Settings", false, alwaysOn, launchCfgAdvanced},
        {"cfg_pins", "Pins Setup", "Settings", false, alwaysOn, launchCfgPins},
        {"cfg_dev", "Dev Mode", "Settings", false, devModeOn, launchCfgDev},
        {"brightness", "Brightness", "Settings", false, alwaysOn, launchBrightness},
        {"dim_time", "Dim Time", "Settings", false, alwaysOn, launchDimmer},
        {"ui_color", "UI Color", "Settings", false, alwaysOn, launchUiColor},
        {"accent_color", "Accent Color", "Settings", false, alwaysOn, launchAccentColor},
        {"ui_theme", "UI Theme", "Settings", false, alwaysOn, launchTheme},
        {"set_clock", "Clock", "Settings", false, alwaysOn, launchSetClock},
        {"startup_app", "Startup App", "Settings", false, alwaysOn, launchStartupApp},
        {"kb_lang", "Keyboard Language", "Settings", false, alwaysOn, launchKbLang},
        {"net_creds", "Network Creds", "Settings", false, alwaysOn, launchNetCreds},
#ifndef LITE_VERSION
        {"badusb_ble_cfg", "BadUSB/BLE", "Settings", false, notLite, launchBadUsbBleCfg},
#endif
        {"device_info", "About", "Settings", false, alwaysOn, launchDeviceInfo},
        {"deep_sleep", "Deep Sleep", "Settings", false, alwaysOn, launchDeepSleep},
        {"sleep_mode", "Sleep", "Settings", false, alwaysOn, launchSleepMode},
    };
    return items;
}

bool appCatalogAvailable(const AppCatalogItem &item) { return item.available && item.available(); }

bool appCatalogLaunch(const String &id) {
    for (const auto &item : appCatalogItems()) {
        if (id == item.id && appCatalogAvailable(item)) {
            item.launch();
            return true;
        }
    }
    return launchMenuById(id);
}

String appCatalogLabel(const String &id) {
    for (const auto &item : appCatalogItems()) {
        if (id == item.id) return String(item.label);
    }
    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (id == desc.id || id == desc.label) return String(desc.label);
    }
    return id;
}

std::vector<AppShortcutTarget> appCatalogShortcutTargets() {
    std::vector<AppShortcutTarget> targets;

    auto already = [&](const String &id) {
        for (const auto &t : targets) {
            if (t.id == id) return true;
        }
        return false;
    };

    for (size_t i = 0; i < kMenuCount; i++) {
        const MenuDescriptor &desc = kMenus[i];
        if (!menuDescriptorAvailable(desc)) continue;
        bool disabled = false;
        for (const String &d : kvxConfig.disabledMenus) {
            if (d == desc.id || d == desc.label) {
                disabled = true;
                break;
            }
        }
        if (disabled) continue;
        String id = desc.id;
        if (already(id)) continue;
        targets.push_back({id, String("Menu: ") + desc.label});
    }

    for (const auto &item : appCatalogItems()) {
        if (!appCatalogAvailable(item)) continue;
        String id = item.id;
        if (already(id)) continue;
        targets.push_back({id, String("App: ") + item.label});
    }

    return targets;
}

bool appCatalogKeyReserved(char c) {
    const uint8_t u = (uint8_t)c;
    switch (u) {
        case ';':
        case ',':
        case '.':
        case '/':
        case '`':
        case '\n':
        case '\r':
        case '\t':
        case 0x08:
        case 0x1B:
        case 0x28: // Enter
        case 0x2A: // Backspace / Del
        case 0x2B: // Tab HID
        case 0x7F: // ASCII DEL
        case 0x80: // Ctrl
        case 0x81: // Shift
        case 0x82: // Alt
        case 0x83: // Opt
        case 0xB1: // FN + Esc
        case 0xB3: // Tab (keyboard word)
        case 0xD4: // FN + Del
        case 0xD7: // FN + Right
        case 0xD8: // FN + Left
        case 0xD9: // FN + Down
        case 0xDA: // FN + Up
        case 0xFF: // FN
            return true;
        default: return false;
    }
}

static bool appCatalogKeystrokeReserved(const keyStroke &key) {
    if (key.enter || key.del || key.fn || key.alt || key.ctrl || key.gui) return true;
    if (!key.modifier_keys.empty()) return true;
    for (char c : key.word) {
        if (appCatalogKeyReserved(c)) return true;
    }
    for (uint8_t h : key.hid_keys) {
        if (appCatalogKeyReserved((char)h)) return true;
    }
    return false;
}

static bool purgeReservedShortcuts() {
    bool changed = false;
    auto it = kvxConfig.mainscreenShortcuts.begin();
    while (it != kvxConfig.mainscreenShortcuts.end()) {
        bool drop = it->first.isEmpty();
        const String &k = it->first;
        for (size_t i = 0; !drop && i < k.length(); i++) {
            if (appCatalogKeyReserved(k[i])) drop = true;
        }
        if (drop) {
            it = kvxConfig.mainscreenShortcuts.erase(it);
            changed = true;
        } else ++it;
    }
    return changed;
}

#ifdef HAS_KEYBOARD
static bool strokeHasCode(uint8_t code) {
    for (char w : KeyStroke.word) {
        if ((uint8_t)w == code) return true;
    }
    return false;
}

static bool systemComboHeld(char raw, uint8_t fnHid) {
    if (strokeHasCode((uint8_t)raw) || strokeHasCode(fnHid)) return true;
    return isCardputerKeyHeld(raw);
}

static void consumeSystemShortcutKeys() {
    UpPress = false;
    DownPress = false;
    PrevPress = false;
    NextPress = false;
    NextPagePress = false;
    PrevPagePress = false;
    EscPress = false;
    KeyStroke.Clear();
}

static void drawSystemShortcutHud(const String &msg) {
    const int h = 26;
    tft.fillRect(0, tftHeight - h, tftWidth, h, kvxConfig.secColor);
    tft.setTextSize(FM);
    tft.setTextColor(kvxConfig.priColor, kvxConfig.secColor);
    tft.drawCentreString(msg, tftWidth / 2, tftHeight - h + 6, 1);
}

static void applySystemBrightness(int dir) {
    dimmer = false;
    isScreenOff = false;
    resetPowerSaveTimer();
    uint8_t v = nextBrightnessValue(kvxConfig.bright, dir);
    setBrightness(v, false);
    kvxConfig.setBright(v);
    _setBrightness(v);
    currentScreenBrightness = v;
    drawSystemShortcutHud("Bright " + String(v) + "%");
}

static void volumeTickBeep() {
#if defined(HAS_NS4168_SPKR) || defined(BUZZ_PIN)
    if (!kvxConfig.soundEnabled || kvxConfig.soundVolume <= 0) return;
    AnyKeyPress = false;
    playVolumeTickBeep((uint8_t)kvxConfig.soundVolume);
#endif
}

static void applySystemVolume(int dir, bool mute) {
    int v = mute ? 0 : nextVolumeValue(kvxConfig.soundVolume, dir);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    kvxConfig.soundVolume = v;
    if (v == 0) drawSystemShortcutHud("Muted");
    else drawSystemShortcutHud("Vol " + String(v) + "%");
    if (!mute) volumeTickBeep();
    kvxConfig.saveFile();
}

// FN+;/. brightness, FN+,/ / volume, FN+space mute. Chord opens shortcut editor.
// Returns true when a submenu was opened (caller must redraw the grid).
static bool handleMainscreenSystemShortcuts() {
    static unsigned long lastAdjMs = 0;
    static bool muteLatched = false;
    static bool chordLatched = false;

    const bool chord = isSystemShortcutChordHeld();
    if (chord) {
        consumeSystemShortcutKeys();
        if (!chordLatched) {
            chordLatched = true;
            while (isSystemShortcutChordHeld() || EscPress) {
                EscPress = false;
                KeyStroke.Clear();
                delay(20);
            }
            resetHeldNavKeys();
            setMainscreenShortcutsMenu();
            return true;
        }
        return false;
    }
    chordLatched = false;

    const bool fnHeld = isFnKeyHeld() || KeyStroke.fn;
    if (!fnHeld) {
        muteLatched = false;
        return false;
    }

    int brightDir = 0;
    int volDir = 0;
    bool mute = false;
    if (systemComboHeld(';', 0xDA)) brightDir = 1;
    else if (systemComboHeld('.', 0xD9)) brightDir = -1;
    else if (systemComboHeld(',', 0xD8)) volDir = -1;
    else if (systemComboHeld('/', 0xD7)) volDir = 1;
    else if (systemComboHeld(' ', ' ')) mute = true;
    else {
        muteLatched = false;
        return false;
    }

    consumeSystemShortcutKeys();

    if (mute) {
        if (!muteLatched) {
            muteLatched = true;
            applySystemVolume(0, true);
        }
        return false;
    }
    muteLatched = false;

    const unsigned long now = millis();
    if (now - lastAdjMs < 180) return false;
    lastAdjMs = now;

    if (brightDir) applySystemBrightness(brightDir);
    else applySystemVolume(volDir, false);
    return false;
}
#endif

bool appCatalogHandleMainscreenKeys() {
#ifdef HAS_KEYBOARD
    static bool purged = false;
    if (!purged) {
        purged = true;
        if (purgeReservedShortcuts()) kvxConfig.saveFile();
    }

    if (handleMainscreenSystemShortcuts()) return true;

    // Never touch navigation frames. Arrow keys also put ;,./ into KeyStroke.word,
    // so only run when no Up/Down/Prev/Next/Sel/Esc pulse is pending.
    if (!KeyStroke.pressed) return false;
    if (UpPress || DownPress || PrevPress || NextPress || SelPress || EscPress) return false;
    if (appCatalogKeystrokeReserved(KeyStroke)) return false;

    String appId;
    bool hit = false;
    vTaskSuspend(xHandle);
    for (char c : KeyStroke.word) {
        if (appCatalogKeyReserved(c)) continue;
        String k;
        k += c;
        auto it = kvxConfig.mainscreenShortcuts.find(k);
        if (it == kvxConfig.mainscreenShortcuts.end()) continue;
        appId = it->second;
        hit = true;
        KeyStroke.Clear();
        break;
    }
    vTaskResume(xHandle);
    if (!hit) return false;
    ledSetStatus(LED_STATUS_BUSY);
    appCatalogLaunch(appId);
    ledSetStatus(LED_STATUS_IDLE);
    return true;
#else
    return false;
#endif
}

static void bindMainscreenShortcut(const String &appId) {
    drawMainBorderWithTitle("Press a key");
    tft.setTextSize(FP);
    tft.drawString("Arrows Enter Esc ` FN Tab", 8, 40);
    tft.drawString("Shift Ctrl Opt Alt Del no", 8, 54);
    EscPress = false;
    while (!check(EscPress) && !forceHome) {
        keyStroke key = _getKeyPress();
        if (!key.pressed) {
            delay(20);
            continue;
        }
        if (check(EscPress) || forceHome) return;
        if (appCatalogKeystrokeReserved(key) || key.word.empty()) continue;
        char c = key.word[0];
        if (appCatalogKeyReserved(c)) continue;
        String k;
        k += c;
        kvxConfig.mainscreenShortcuts[k] = appId;
        kvxConfig.saveFile();
        displayInfo(k + " -> " + appCatalogLabel(appId), true);
        return;
    }
}

static bool menuTileAvailable(const MenuDescriptor &desc) {
    if (!menuDescriptorAvailable(desc)) return false;
    for (const String &d : kvxConfig.disabledMenus) {
        if (d == desc.id || d == desc.label) return false;
    }
    return true;
}

static void pickShortcutInGroup(const String &group) {
    std::vector<Option> opts;

    if (group == "Main menus") {
        for (size_t i = 0; i < kMenuCount; i++) {
            const MenuDescriptor &desc = kMenus[i];
            if (!menuTileAvailable(desc)) continue;
            String id = desc.id;
            opts.push_back({desc.label, [id]() { bindMainscreenShortcut(id); }});
        }
    } else {
        for (const auto &item : appCatalogItems()) {
            if (!appCatalogAvailable(item)) continue;
            if (String(item.group) != group) continue;
            String id = item.id;
            opts.push_back({item.label, [id]() { bindMainscreenShortcut(id); }});
        }
    }

    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, group.c_str());
}

static void pickShortcutApp() {
    // Preferred group order for the first picker screen.
    static const char *kPreferredGroups[] = {
        "Main menus",
        "WiFi",
        "BLE",
        "RF",
        "IR",
        "RFID",
        "GPS",
        "NRF24",
        "NetOps",
        "Files",
        "USB",
        "Others",
        "Clock",
        "Settings",
    };

    auto groupHasItems = [&](const String &group) -> bool {
        if (group == "Main menus") {
            for (size_t i = 0; i < kMenuCount; i++) {
                if (menuTileAvailable(kMenus[i])) return true;
            }
            return false;
        }
        for (const auto &item : appCatalogItems()) {
            if (appCatalogAvailable(item) && String(item.group) == group) return true;
        }
        return false;
    };

    std::vector<String> groups;
    for (const char *g : kPreferredGroups) {
        if (groupHasItems(g)) groups.push_back(g);
    }
    // Any extra groups from catalog not in the preferred list.
    for (const auto &item : appCatalogItems()) {
        if (!appCatalogAvailable(item)) continue;
        String g = item.group;
        bool known = false;
        for (const auto &existing : groups) {
            if (existing == g) {
                known = true;
                break;
            }
        }
        if (!known) groups.push_back(g);
    }

    std::vector<Option> opts;
    for (const auto &g : groups) {
        String group = g;
        opts.push_back({g, [group]() { pickShortcutInGroup(group); }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Bind Target");
}

void setMainscreenShortcutsMenu() {
    if (purgeReservedShortcuts()) kvxConfig.saveFile();
    while (true) {
        std::vector<Option> opts;
        for (const auto &pair : kvxConfig.mainscreenShortcuts) {
            String key = pair.first;
            String line = key + " -> " + appCatalogLabel(pair.second);
            opts.push_back({line, [key]() {
                                kvxConfig.mainscreenShortcuts.erase(key);
                                kvxConfig.saveFile();
                            }});
        }
        opts.push_back({"Add binding", pickShortcutApp});
        opts.push_back({"Clear all", []() {
                            kvxConfig.mainscreenShortcuts.clear();
                            kvxConfig.saveFile();
                        }});
        opts.push_back({"Back", []() {}});
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Mainscreen Shortcuts");
        if (sel == -1 || sel == (int)opts.size() - 1 || forceHome) return;
    }
}
