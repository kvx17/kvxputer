#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace model {

// Stable tool ids used by BaseModel::active_tool_id and ScreenManager.
enum class ToolId {
    None = 0,
    // Shared net
    NetToolkit,
    ScanHosts,
    ListenTcp,
    ClientTcp,
    Socks4,
    Telnet,
    LanWatch,
    PineapHunter,
    OpenNetworks,
    ApDetail,
    FileShare,
    DeadDrop,
    Roku,
    WireGuard,
    Printer,
    UpnpList,
    HoneyPot,
    SshHint,
    // Offline loot / handshake file tools
    LootConsole,
    CheckHandshakes,
    NtlmClean,
    // Read-only STA probes
    SmbStatus,
    SipOptions,
    LdapRead,
    // BLE
    BleScan,
    BleHunter,
    BleSniffer,
    WallOfFlipper,
    WallOfAirtag,
    SkimmerDetector,
    BleDisconnect,
    NinebotStatus,
    AmiiboStatus,
    Pn532Status,
    ChameleonStatus,
    // Tools leftovers
    Dtmf,
    Morse,
    Microphone,
    Mqtt,
    LlmChat,
    DroneId,
    // Everyday apps
    Calculator,
    MediaPlayer,
    IButtonHint,
    FilesBrowser,
    FilesShareHint,
    IrUse,
    IrLearn,
    IrBrowse,
    IrRead,
    IrFavorites,
    TvBGone,
    TagTinker,
    IrCarrier,
    WifiConnect,
    WifiDisconnect,
    WifiApInfo,
    // Scripts / GPS / Modules
    ScriptsRunner,
    PcConnectHowTo,
    PcConnectNeeds,
    PcConnectScripts,
    PcConnectExport,
    GpsCaptures,
    GpsLive,
    GpsWardrivingMenu,
    GpsScanWifi,
    GpsScanBle,
    GpsScanBoth,
    GpsWdConfig,
    GpsWdMaster,
    GpsSiteSurvey,
    GpsTracker,
    GpsConfigMenu,
    GpsStatus,
    GpsSource,
    GpsHardware,
    GpsNmeaPath,
    GpsBaud,
    GpsFolder,
    UartShell,
    BusScan,
    UnitScrollStatus,
    CapProbe,
    PahubScan,
    IButtonRead,
    Pn532KillerUart,
    UartLlm,
    // Cap radios
    Nrf24Info,
    Nrf24Status,
    Nrf24Spectrum,
    Nrf24Listen,
    Nrf24Send,
    FmInfo,
    FmStatus,
    FmBroadcast,
    FmStop,
    FmSpectrum,
    RfStatus,
    RfSpectrum,
    RfListen,
    RfRecord,
    RfReplay,
    RfLibrary,
    RfSendTest,
    LoraStatus,
    LoraChat,
    LoraAntenna,
    LoraFreq,
    LoraUser,
    LoraMeshHint,
    RfidStatus,
    RfidRead,
    RfidScan,
    RfidLoad,
    RfidSave,
    NfcStatus,
    NfcRead,
    NfcScan,
    NfcLoad,
    NfcSave,
    NfcErase,
    // NFC nested menus (open OptionsList of leaves)
    NfcClassicMenu,
    NfcTagsMenu,
    NfcNdefMenu,
    NfcLabMenu,
    NfcParsersMenu,
    NfcCompanionsMenu,
    NfcConfigMenu,
    // Classic…
    NfcDetectReader,
    NfcMfkey,
    NfcDictAttack,
    NfcMagic,
    NfcDumpEditor,
    // Tags…
    NfcIso15693,
    NfcDesfire,
    NfcUlc,
    NfcFelica,
    NfcForcedType,
    // NDEF tools…
    NfcWriteNdef,
    NfcEmulateNdef,
    NfcReadRecords,
    NfcSaveAsFile,
    NfcMemoryDump,
    NfcChipInfo,
    NfcLockTag,
    NfcPassword,
    NfcFormat,
    NfcApdu,
    // Lab…
    NfcApduScript,
    NfcCompare,
    NfcFieldPoll,
    // Parsers…
    NfcAmiibo,
    NfcTransit,
    NfcEpassport,
    NfcPicopass,
    NfcEink,
    // Companions…
    NfcEmv,
    NfcSrix,
    NfcAmiibolink,
    NfcPn532Ble,
    NfcPn532Uart,
    NfcChameleon,
    // Config
    NfcModule,
    NfcWiring,
    NfcAddKey,
    Count,
};

struct ToolDesc {
    ToolId id;
    const char* key;
    const char* label;
    const char* channel_id;
};

inline constexpr ToolDesc kTools[] = {
    {ToolId::WifiConnect, "wifi_connect", "Connect", "wifi"},
    {ToolId::WifiDisconnect, "wifi_disconnect", "Disconnect", "wifi"},
    {ToolId::WifiApInfo, "wifi_ap_info", "AP info", "wifi"},
    {ToolId::NetToolkit, "net_toolkit", "Net Toolkit", "wifi"},
    {ToolId::ScanHosts, "scan_hosts", "Scan Hosts", "wifi"},
    {ToolId::ListenTcp, "listen_tcp", "Listen TCP", "wifi"},
    {ToolId::ClientTcp, "client_tcp", "Client TCP", "wifi"},
    {ToolId::Socks4, "socks4", "SOCKS4 Proxy", "wifi"},
    {ToolId::Telnet, "telnet", "TelNET", "wifi"},
    {ToolId::LanWatch, "lan_watch", "LAN Watch", "wifi"},
    {ToolId::PineapHunter, "pineap_hunter", "PineAP Hunter", "wifi"},
    {ToolId::OpenNetworks, "open_networks", "Open networks", "wifi"},
    {ToolId::ApDetail, "ap_detail", "AP detail", "wifi"},
    {ToolId::FileShare, "file_share", "File Share", "wifi"},
    {ToolId::DeadDrop, "dead_drop", "WiFi Dead Drop", "wifi"},
    {ToolId::Roku, "roku", "Roku Remote", "wifi"},
    {ToolId::WireGuard, "wireguard", "WireGuard", "wifi"},
    {ToolId::Printer, "printer", "Printer Tools", "wifi"},
    {ToolId::UpnpList, "upnp_list", "UPnP list", "wifi"},
    {ToolId::HoneyPot, "honeypot", "HoneyPot", "wifi"},
    {ToolId::SshHint, "ssh_hint", "SSH (launcher)", "wifi"},
    {ToolId::WallOfFlipper, "wall_of_flipper", "Wall Of Flipper", "wifi"},
    {ToolId::CheckHandshakes, "check_handshakes", "Check Handshakes", "wifi"},

    {ToolId::NetToolkit, "net_toolkit", "Net Toolkit", "netops"},
    {ToolId::ScanHosts, "scan_hosts", "Scan Hosts", "netops"},
    {ToolId::ListenTcp, "listen_tcp", "Listen TCP", "netops"},
    {ToolId::ClientTcp, "client_tcp", "Client TCP", "netops"},
    {ToolId::Socks4, "socks4", "SOCKS4 Proxy", "netops"},
    {ToolId::Telnet, "telnet", "TelNET", "netops"},
    {ToolId::FileShare, "file_share", "File Share", "netops"},
    {ToolId::Printer, "printer", "Printer Tools", "netops"},
    {ToolId::UpnpList, "upnp_list", "UPnP list", "netops"},
    {ToolId::HoneyPot, "honeypot", "HoneyPot", "netops"},
    {ToolId::SshHint, "ssh_hint", "SSH (launcher)", "netops"},
    {ToolId::UartShell, "uart_shell", "UART Shell", "netops"},
    {ToolId::LootConsole, "loot_console", "Loot Console", "netops"},
    {ToolId::CheckHandshakes, "check_handshakes", "Check Handshakes", "netops"},
    {ToolId::NtlmClean, "ntlm_clean", "NTLM Clean", "netops"},
    {ToolId::SmbStatus, "smb_status", "SMB Status", "netops"},
    {ToolId::SipOptions, "sip_options", "SIP OPTIONS", "netops"},
    {ToolId::LdapRead, "ldap_read", "LDAP Read", "netops"},

    {ToolId::BleScan, "ble_scan", "BLE Scan", "ble"},
    {ToolId::BleHunter, "ble_hunter", "BLE Hunter", "ble"},
    {ToolId::BleSniffer, "ble_sniffer", "BLE Sniffer", "ble"},
    {ToolId::WallOfFlipper, "wall_of_flipper", "Wall Of Flipper", "ble"},
    {ToolId::WallOfAirtag, "wall_of_airtag", "Wall Of Airtag", "ble"},
    {ToolId::SkimmerDetector, "skimmer", "Skimmer Detector", "ble"},
    {ToolId::BleDisconnect, "ble_disconnect", "Disconnect", "ble"},
    {ToolId::DroneId, "drone_id", "Drone ID (BLE)", "ble"},
    {ToolId::NinebotStatus, "ninebot_status", "Ninebot Status", "ble"},
    {ToolId::AmiiboStatus, "amiibo_status", "Amiibolink Status", "ble"},
    {ToolId::Pn532Status, "pn532_status", "PN532-BLE Status", "ble"},
    {ToolId::ChameleonStatus, "chameleon_status", "Chameleon Status", "ble"},

    {ToolId::ScanHosts, "scan_hosts", "Scan Hosts", "ethernet"},

    {ToolId::GpsWardrivingMenu, "gps_wardriving", "Wardriving", "gps"},
    {ToolId::GpsSiteSurvey, "gps_site_survey", "Site Survey", "gps"},
    {ToolId::GpsTracker, "gps_tracker", "GPS Tracker", "gps"},
    {ToolId::GpsConfigMenu, "gps_config", "Config", "gps"},
    {ToolId::GpsScanWifi, "gps_scan_wifi", "Scan WiFi Networks", "gps"},
    {ToolId::GpsScanBle, "gps_scan_ble", "Scan BLE Devices", "gps"},
    {ToolId::GpsScanBoth, "gps_scan_both", "Scan Both", "gps"},
    {ToolId::GpsCaptures, "gps_captures", "View Captures", "gps"},
    {ToolId::GpsWdConfig, "gps_wd_config", "WD Config", "gps"},
    {ToolId::GpsWdMaster, "gps_wd_master", "Wardriving Master", "gps"},
    {ToolId::GpsLive, "gps_live", "Live Wardrive", "gps"},
    {ToolId::GpsStatus, "gps_status", "GPS Status", "gps"},
    {ToolId::GpsSource, "gps_source", "GPS Source", "gps"},
    {ToolId::GpsHardware, "gps_hardware", "GPS Hardware", "gps"},
    {ToolId::GpsNmeaPath, "gps_nmea_path", "NMEA device path", "gps"},
    {ToolId::GpsBaud, "gps_baud", "Baudrate", "gps"},
    {ToolId::GpsFolder, "gps_folder", "Captures folder", "gps"},

    {ToolId::ScriptsRunner, "scripts", "Local scripts", "scripts"},
    {ToolId::PcConnectScripts, "pcconnect_scripts", "PC Connect scripts", "scripts"},
    {ToolId::PcConnectHowTo, "pcconnect_howto", "PC Connect how to", "scripts"},

    {ToolId::PcConnectHowTo, "pcconnect_howto", "How to use", "pcconnect"},
    {ToolId::PcConnectNeeds, "pcconnect_needs", "What you need", "pcconnect"},
    {ToolId::PcConnectScripts, "pcconnect_scripts", "Browse scripts", "pcconnect"},
    {ToolId::PcConnectExport, "pcconnect_export", "Export to config", "pcconnect"},

    {ToolId::BusScan, "bus_scan", "Bus Scanner", "modules"},
    {ToolId::UnitScrollStatus, "unit_scroll", "Unit Scroll", "modules"},
    {ToolId::UartShell, "uart_shell", "UART Shell", "modules"},
    {ToolId::CapProbe, "cap_probe", "Cap Probe", "modules"},
    {ToolId::PahubScan, "pahub_scan", "PaHub Scan", "modules"},
    {ToolId::IButtonRead, "ibutton_read", "iButton Read", "modules"},
    {ToolId::Pn532KillerUart, "pn532_killer", "PN532Killer UART", "modules"},
    {ToolId::UartLlm, "uart_llm", "UART LLM", "modules"},

    {ToolId::RfStatus, "rf_status", "Cap Status", "rf"},
    {ToolId::RfSpectrum, "rf_spectrum", "Spectrum", "rf"},
    {ToolId::RfListen, "rf_listen", "Listen", "rf"},
    {ToolId::RfRecord, "rf_record", "Record RAW", "rf"},
    {ToolId::RfReplay, "rf_replay", "Replay / Custom", "rf"},
    {ToolId::RfLibrary, "rf_library", "Protocol Library", "rf"},
    {ToolId::RfSendTest, "rf_send_test", "Send Test", "rf"},

    {ToolId::Nrf24Info, "nrf24_info", "Information", "nrf24"},
    {ToolId::Nrf24Status, "nrf24_status", "Module Status", "nrf24"},
    {ToolId::Nrf24Spectrum, "nrf24_spectrum", "Spectrum", "nrf24"},
    {ToolId::Nrf24Listen, "nrf24_listen", "Listen", "nrf24"},
    {ToolId::Nrf24Send, "nrf24_send", "Send Payload", "nrf24"},

    {ToolId::LoraStatus, "lora_status", "Cap Status", "lora"},
    {ToolId::LoraChat, "lora_chat", "Chat", "lora"},
    {ToolId::LoraAntenna, "lora_antenna", "Antenna Check", "lora"},
    {ToolId::LoraFreq, "lora_freq", "Frequency", "lora"},
    {ToolId::LoraUser, "lora_user", "Username", "lora"},
    {ToolId::LoraMeshHint, "lora_mesh", "Meshtastic", "lora"},

    {ToolId::FmInfo, "fm_info", "Information", "fm"},
    {ToolId::FmStatus, "fm_status", "Module Status", "fm"},
    {ToolId::FmBroadcast, "fm_broadcast", "Broadcast", "fm"},
    {ToolId::FmStop, "fm_stop", "Stop", "fm"},
    {ToolId::FmSpectrum, "fm_spectrum", "Spectrum", "fm"},

    {ToolId::RfidStatus, "rfid_status", "Module Status", "rfid"},
    {ToolId::RfidRead, "rfid_read", "Read", "rfid"},
    {ToolId::RfidScan, "rfid_scan", "Scan", "rfid"},
    {ToolId::RfidLoad, "rfid_load", "Load", "rfid"},
    {ToolId::RfidSave, "rfid_save", "Save", "rfid"},

    {ToolId::NfcStatus, "nfc_status", "Module Status", "nfc"},
    {ToolId::NfcRead, "nfc_read", "Read Tag", "nfc"},
    {ToolId::NfcEmulateNdef, "nfc_emulate_ndef", "Emulate Tag", "nfc"},
    {ToolId::NfcWriteNdef, "nfc_write_ndef", "Write Tag", "nfc"},
    {ToolId::NfcScan, "nfc_scan", "Scan tags", "nfc"},
    {ToolId::NfcLoad, "nfc_load", "Load file", "nfc"},
    {ToolId::NfcErase, "nfc_erase", "Erase data", "nfc"},
    {ToolId::NfcClassicMenu, "nfc_classic_menu", "Classic...", "nfc"},
    {ToolId::NfcTagsMenu, "nfc_tags_menu", "Tags...", "nfc"},
    {ToolId::NfcNdefMenu, "nfc_ndef_menu", "NDEF tools...", "nfc"},
    {ToolId::NfcLabMenu, "nfc_lab_menu", "Lab...", "nfc"},
    {ToolId::NfcParsersMenu, "nfc_parsers_menu", "Parsers...", "nfc"},
    {ToolId::NfcCompanionsMenu, "nfc_companions_menu", "Companions...", "nfc"},
    {ToolId::NfcConfigMenu, "nfc_config_menu", "Config", "nfc"},
    // Leaves also registered on nfc so Discovery finds unique labels
    {ToolId::NfcDetectReader, "nfc_detect_reader", "Detect Reader", "nfc"},
    {ToolId::NfcMfkey, "nfc_mfkey", "MFKey", "nfc"},
    {ToolId::NfcDictAttack, "nfc_dict_attack", "Dict attack", "nfc"},
    {ToolId::NfcMagic, "nfc_magic", "NFC Magic", "nfc"},
    {ToolId::NfcDumpEditor, "nfc_dump_editor", "Dump editor", "nfc"},
    {ToolId::NfcIso15693, "nfc_iso15693", "ISO15693 tool", "nfc"},
    {ToolId::NfcDesfire, "nfc_desfire", "DESFire browser", "nfc"},
    {ToolId::NfcUlc, "nfc_ulc", "Ultralight-C", "nfc"},
    {ToolId::NfcFelica, "nfc_felica", "FeliCa services", "nfc"},
    {ToolId::NfcForcedType, "nfc_forced_type", "Forced type read", "nfc"},
    // NfcWriteNdef / NfcEmulateNdef registered above as quick actions (Write Tag / Emulate Tag)
    {ToolId::NfcReadRecords, "nfc_read_records", "Read records", "nfc"},
    {ToolId::NfcSaveAsFile, "nfc_save_as_file", "Save as file", "nfc"},
    {ToolId::NfcMemoryDump, "nfc_memory_dump", "Memory dump", "nfc"},
    {ToolId::NfcChipInfo, "nfc_chip_info", "Chip info", "nfc"},
    {ToolId::NfcLockTag, "nfc_lock_tag", "Lock tag", "nfc"},
    {ToolId::NfcPassword, "nfc_password", "Password", "nfc"},
    {ToolId::NfcFormat, "nfc_format", "Format memory", "nfc"},
    {ToolId::NfcApdu, "nfc_apdu", "NFC command", "nfc"},
    {ToolId::NfcApduScript, "nfc_apdu_script", "APDU script", "nfc"},
    {ToolId::NfcCompare, "nfc_compare", "Compare dump", "nfc"},
    {ToolId::NfcFieldPoll, "nfc_field_poll", "Field poll", "nfc"},
    {ToolId::NfcAmiibo, "nfc_amiibo", "Amiibo toolkit", "nfc"},
    {ToolId::NfcTransit, "nfc_transit", "Transit parse", "nfc"},
    {ToolId::NfcEpassport, "nfc_epassport", "ePassport", "nfc"},
    {ToolId::NfcPicopass, "nfc_picopass", "Picopass / iCLASS", "nfc"},
    {ToolId::NfcEink, "nfc_eink", "NFC e-ink", "nfc"},
    {ToolId::NfcEmv, "nfc_emv", "Read EMV", "nfc"},
    {ToolId::NfcSrix, "nfc_srix", "SRIX Tool", "nfc"},
    {ToolId::NfcAmiibolink, "nfc_amiibolink", "Amiibolink", "nfc"},
    {ToolId::NfcPn532Ble, "nfc_pn532_ble", "PN532 BLE", "nfc"},
    {ToolId::NfcPn532Uart, "nfc_pn532_uart", "PN532 UART", "nfc"},
    {ToolId::NfcChameleon, "nfc_chameleon", "Chameleon", "nfc"},
    {ToolId::NfcModule, "nfc_module", "NFC Module", "nfc"},
    {ToolId::NfcWiring, "nfc_wiring", "Wiring help", "nfc"},
    {ToolId::NfcAddKey, "nfc_add_key", "Add MIF Key", "nfc"},
    // Keep Save as Discovery-visible helper (auto-save note)
    {ToolId::NfcSave, "nfc_save", "Save dump help", "nfc"},

    {ToolId::Dtmf, "dtmf", "DTMF", "others"},
    {ToolId::Morse, "morse", "Morse", "others"},
    {ToolId::Microphone, "microphone", "Microphone", "others"},
    {ToolId::Mqtt, "mqtt", "MQTT", "others"},
    {ToolId::LlmChat, "llm_chat", "LLM Chat", "others"},
    {ToolId::DroneId, "drone_id", "Drone ID", "others"},
    {ToolId::Calculator, "calculator", "Calculator", "others"},
    {ToolId::MediaPlayer, "media_player", "Media Player", "others"},
    {ToolId::IButtonHint, "ibutton", "iButton", "others"},

    {ToolId::FilesBrowser, "files_browser", "Browse", "files"},
    {ToolId::FilesShareHint, "files_share_hint", "File Share (Wi-Fi)", "files"},

    {ToolId::IrUse, "ir_use", "Use", "infrared"},
    {ToolId::IrLearn, "ir_learn", "Learn", "infrared"},
    {ToolId::IrBrowse, "ir_browse", "Browse", "infrared"},
    {ToolId::IrRead, "ir_read", "Read", "infrared"},
    {ToolId::IrFavorites, "ir_favorites", "Favorites", "infrared"},
    {ToolId::TvBGone, "tv_b_gone", "TV-B-Gone", "infrared"},
    {ToolId::TagTinker, "tagtinker", "TagTinker ESL", "infrared"},
    {ToolId::IrCarrier, "ir_carrier", "IR Carrier", "infrared"},
};

inline constexpr size_t kToolCount = sizeof(kTools) / sizeof(kTools[0]);

inline std::vector<const ToolDesc*> tools_for_channel(const char* channel_id) {
    std::vector<const ToolDesc*> out;
    if (!channel_id) return out;
    for (size_t i = 0; i < kToolCount; ++i) {
        if (std::strcmp(kTools[i].channel_id, channel_id) == 0) out.push_back(&kTools[i]);
    }
    return out;
}

inline const ToolDesc* find_tool(ToolId id) {
    for (size_t i = 0; i < kToolCount; ++i) {
        if (kTools[i].id == id) return &kTools[i];
    }
    return nullptr;
}

inline const ToolDesc* find_tool_by_key(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < kToolCount; ++i) {
        if (std::strcmp(kTools[i].key, key) == 0) return &kTools[i];
    }
    return nullptr;
}

inline std::vector<const ToolDesc*> all_unique_tools() {
    std::vector<const ToolDesc*> out;
    for (size_t i = 0; i < kToolCount; ++i) {
        bool seen = false;
        for (const auto* t : out) {
            if (std::strcmp(t->key, kTools[i].key) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen) out.push_back(&kTools[i]);
    }
    return out;
}

} // namespace model
