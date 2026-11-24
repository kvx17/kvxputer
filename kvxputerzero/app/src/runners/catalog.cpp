#include "runner.h"
#include "runners_impl.h"

#include <cstring>
#include <set>

namespace runner {
namespace {

Entry make(const char* channel, const char* key, const char* label, model::ToolId id, Kind kind, RunFn run,
           bool confirm = false, bool exclusive = false, std::vector<ArgDesc> args = {},
           std::vector<std::string> children = {}, const char* reason = nullptr) {
    Entry e;
    e.channel = channel;
    e.key = key;
    e.label = label;
    e.tool_id = id;
    e.kind = kind;
    e.confirm = confirm;
    e.needs_exclusive = exclusive;
    e.args = std::move(args);
    e.children = std::move(children);
    e.run = std::move(run);
    e.reason = reason;
    return e;
}

std::vector<Entry> build() {
    using Tid = model::ToolId;
    std::vector<Entry> out;

    // Discovery + Tools extras
    out.push_back(make("discovery", "filter", "Discovery", Tid::None, Kind::Log, proof_discovery(), false, false,
                       {{"filter", "substring", false}}));
    out.push_back(make("tools", "codec", "Codec / Hash", Tid::None, Kind::Log, proof_codec(), false, false,
                       {{"op", "b64e|b64d|hexe|hexd|urle|urld|md5|sha1|sha256", true},
                        {"text", "input text", true}}));
    out.push_back(make("tools", "calculator", "Calculator", Tid::Calculator, Kind::Log, tools_calculator(), false,
                       false, {{"expr", "expression", true}}));
    out.push_back(make("tools", "eng_calc", "Calculators", Tid::None, Kind::Log, tools_eng_calc(), false, false,
                       {{"op", "op name or index", false}, {"input", "fields", false}}));
    out.push_back(make("tools", "passgen", "Passgen", Tid::None, Kind::Log, tools_passgen()));
    out.push_back(make("tools", "totp", "TOTP", Tid::None, Kind::Log, tools_totp()));
    out.push_back(make("tools", "combo", "Combo", Tid::None, Kind::Log, tools_combo(), false, false,
                       {{"line", "first second resistance", true}}));
    out.push_back(make("tools", "qr", "QRCodes", Tid::None, Kind::StandIn, tools_qr(), false, false,
                       {{"text", "payload", true}}));
    out.push_back(make("tools", "barcode", "Barcode", Tid::None, Kind::StandIn, tools_barcode(), false, false,
                       {{"data", "payload", true}}));
    out.push_back(make("tools", "pda", "PDA", Tid::None, Kind::Menu, tools_pda()));
    out.push_back(make("tools", "timer", "Timer", Tid::None, Kind::Log, tools_timer(), false, false,
                       {{"seconds", "duration", true}}));
    out.push_back(make("tools", "matrix", "Matrix", Tid::None, Kind::Info, {}, false, false, {}, {},
                       "GUI-only display rain; no action to trigger"));
    out.push_back(make("tools", "media_player", "Media Player", Tid::MediaPlayer, Kind::Log, tools_media_player(),
                       false, false, {{"path", "wav path", true}}));
    out.push_back(make("tools", "dtmf", "DTMF", Tid::Dtmf, Kind::Log, tools_dtmf(), false, false,
                       {{"digits", "0-9*#", true}}));
    out.push_back(make("tools", "morse", "Morse", Tid::Morse, Kind::Log, tools_morse(), false, false,
                       {{"text", "message", true}}));
    out.push_back(make("tools", "microphone", "Microphone", Tid::Microphone, Kind::Log, tools_microphone()));
    out.push_back(make("tools", "mqtt", "MQTT", Tid::Mqtt, Kind::Log, tools_mqtt(), false, false,
                       {{"host", "broker", true}, {"topic", "topic", true}}));
    out.push_back(make("tools", "llm_chat", "LLM Chat", Tid::LlmChat, Kind::Log, tools_llm_chat(), false, false,
                       {{"prompt", "text", true}}));
    out.push_back(make("tools", "drone_id", "Drone ID", Tid::DroneId, Kind::Log, net_ble_scan(), false, true));
    out.push_back(make("tools", "ibutton", "iButton", Tid::IButtonHint, Kind::Info,
                       tools_info("Needs a 1-Wire accessory. Use Modules → ibutton_read.")));

    // WiFi
    out.push_back(make("wifi", "wifi_monitor_adapter", "Monitor adapter", Tid::WifiMonitorAdapter, Kind::Log,
                       net_wifi_monitor_adapter(), false, true));
    out.push_back(make("wifi", "wifi_analyzer", "WiFi Analyzer", Tid::WifiAnalyzer, Kind::StandIn,
                       net_wifi_analyzer(), false, true));
    out.push_back(make("wifi", "wifi_connect", "Connect", Tid::WifiConnect, Kind::Log, net_wifi_connect(), false,
                       false, {{"ssid", "SSID", true}, {"psk", "password", false}}));
    out.push_back(make("wifi", "wifi_disconnect", "Disconnect", Tid::WifiDisconnect, Kind::Log,
                       net_wifi_disconnect()));
    out.push_back(make("wifi", "wifi_ap_info", "AP info", Tid::WifiApInfo, Kind::Log, net_wifi_ap_info()));
    out.push_back(make("wifi", "net_toolkit", "Net Toolkit", Tid::NetToolkit, Kind::Menu, net_ping(), false, false,
                       {}, {"ping", "dig", "curl", "trace"}));
    out.push_back(make("wifi", "ping", "Ping", Tid::None, Kind::Log, net_ping(), false, false,
                       {{"host", "host", true}}));
    out.push_back(make("wifi", "dig", "Dig", Tid::None, Kind::Log, net_shell("dig", "dig"), false, false,
                       {{"name", "hostname", true}}));
    out.push_back(make("wifi", "curl", "Curl", Tid::None, Kind::Log, net_shell("curl", "curl -sS"), false, false,
                       {{"url", "URL", true}}));
    out.push_back(make("wifi", "trace", "Trace", Tid::None, Kind::Log, net_shell("trace", "traceroute"), false,
                       false, {{"host", "host", true}}));
    out.push_back(make("wifi", "scan_hosts", "Scan Hosts", Tid::ScanHosts, Kind::Log, net_scan_hosts()));
    out.push_back(make("wifi", "listen_tcp", "Listen TCP", Tid::ListenTcp, Kind::Log, net_listen_tcp(), false, false,
                       {{"port", "port", true}}));
    out.push_back(make("wifi", "client_tcp", "Client TCP", Tid::ClientTcp, Kind::Log, net_client_tcp(false), false,
                       false, {{"host", "host:port", true}}));
    out.push_back(make("wifi", "socks4", "SOCKS4 Proxy", Tid::Socks4, Kind::Log, net_socks4()));
    out.push_back(make("wifi", "telnet", "TelNET", Tid::Telnet, Kind::Log, net_client_tcp(true), false, false,
                       {{"host", "host", true}}));
    out.push_back(make("wifi", "lan_watch", "LAN Watch", Tid::LanWatch, Kind::Log, net_lan_watch()));
    out.push_back(make("wifi", "pineap_hunter", "PineAP Hunter", Tid::PineapHunter, Kind::Log, net_pineap(), false,
                       true));
    out.push_back(make("wifi", "open_networks", "Open networks", Tid::OpenNetworks, Kind::Log, net_wifi_scan(true)));
    out.push_back(make("wifi", "ap_detail", "AP detail", Tid::ApDetail, Kind::Log, net_ap_detail(), false, false,
                       {{"match", "ssid or bssid", true}}));
    out.push_back(make("wifi", "file_share", "File Share", Tid::FileShare, Kind::Log, net_file_share()));
    out.push_back(make("wifi", "dead_drop", "WiFi Dead Drop", Tid::DeadDrop, Kind::Log, net_dead_drop()));
    out.push_back(make("wifi", "roku", "Roku Remote", Tid::Roku, Kind::Log, net_roku()));
    out.push_back(make("wifi", "wireguard", "WireGuard", Tid::WireGuard, Kind::Log, net_wireguard()));
    out.push_back(make("wifi", "printer", "Printer Tools", Tid::Printer, Kind::Log, net_printer(), false, false,
                       {{"ip", "printer IP", true}}));
    out.push_back(make("wifi", "upnp_list", "UPnP list", Tid::UpnpList, Kind::Log, net_upnp()));
    out.push_back(make("wifi", "honeypot", "HoneyPot", Tid::HoneyPot, Kind::Log, net_honeypot()));
    out.push_back(make("wifi", "ssh_hint", "SSH (launcher)", Tid::SshHint, Kind::Info, net_ssh_hint()));
    out.push_back(make("wifi", "wall_of_flipper", "Wall Of Flipper", Tid::WallOfFlipper, Kind::Log, net_ble_scan(),
                       false, true));
    out.push_back(make("wifi", "check_handshakes", "Check Handshakes", Tid::CheckHandshakes, Kind::Log,
                       net_check_handshakes()));

    // NetOps (shared keys re-registered on channel)
    auto add_shared_net = [&](const char* ch) {
        out.push_back(make(ch, "net_toolkit", "Net Toolkit", Tid::NetToolkit, Kind::Menu, net_ping()));
        out.push_back(make(ch, "scan_hosts", "Scan Hosts", Tid::ScanHosts, Kind::Log, net_scan_hosts()));
        out.push_back(make(ch, "listen_tcp", "Listen TCP", Tid::ListenTcp, Kind::Log, net_listen_tcp(), false, false,
                           {{"port", "port", true}}));
        out.push_back(make(ch, "client_tcp", "Client TCP", Tid::ClientTcp, Kind::Log, net_client_tcp(false), false,
                           false, {{"host", "host", true}}));
        out.push_back(make(ch, "socks4", "SOCKS4 Proxy", Tid::Socks4, Kind::Log, net_socks4()));
        out.push_back(make(ch, "telnet", "TelNET", Tid::Telnet, Kind::Log, net_client_tcp(true), false, false,
                           {{"host", "host", true}}));
        out.push_back(make(ch, "file_share", "File Share", Tid::FileShare, Kind::Log, net_file_share()));
        out.push_back(make(ch, "printer", "Printer Tools", Tid::Printer, Kind::Log, net_printer(), false, false,
                           {{"ip", "ip", true}}));
        out.push_back(make(ch, "upnp_list", "UPnP list", Tid::UpnpList, Kind::Log, net_upnp()));
        out.push_back(make(ch, "honeypot", "HoneyPot", Tid::HoneyPot, Kind::Log, net_honeypot()));
        out.push_back(make(ch, "ssh_hint", "SSH (launcher)", Tid::SshHint, Kind::Info, net_ssh_hint()));
        out.push_back(make(ch, "check_handshakes", "Check Handshakes", Tid::CheckHandshakes, Kind::Log,
                           net_check_handshakes()));
    };
    add_shared_net("netops");
    out.push_back(make("netops", "uart_shell", "UART Shell", Tid::UartShell, Kind::Log, net_uart_shell(), false,
                       true, {{"device", "/dev/ttyUSB0", true}}));
    out.push_back(make("netops", "loot_console", "Loot Console", Tid::LootConsole, Kind::Log, net_loot_console()));
    out.push_back(make("netops", "ntlm_clean", "NTLM Clean", Tid::NtlmClean, Kind::Log, net_ntlm_clean()));
    out.push_back(make("netops", "smb_status", "SMB Status", Tid::SmbStatus, Kind::Log, net_smb_status(), false,
                       false, {{"host", "host", true}}));
    out.push_back(make("netops", "sip_options", "SIP OPTIONS", Tid::SipOptions, Kind::Log, net_sip_options(), false,
                       false, {{"host", "host", true}}));
    out.push_back(make("netops", "ldap_read", "LDAP Read", Tid::LdapRead, Kind::Log, net_ldap_read(), false, false,
                       {{"host", "host", true}}));

    // BLE
    out.push_back(make("ble", "ble_scan", "BLE Scan", Tid::BleScan, Kind::Log, net_ble_scan(), false, true));
    out.push_back(make("ble", "ble_hunter", "BLE Hunter", Tid::BleHunter, Kind::Log, net_ble_scan(), false, true,
                       {{"target", "name substring", false}}));
    out.push_back(make("ble", "ble_sniffer", "BLE Sniffer", Tid::BleSniffer, Kind::Log, net_ble_scan(), false, true));
    out.push_back(make("ble", "wall_of_flipper", "Wall Of Flipper", Tid::WallOfFlipper, Kind::Log, net_ble_scan(),
                       false, true));
    out.push_back(make("ble", "wall_of_airtag", "Wall Of Airtag", Tid::WallOfAirtag, Kind::Log, net_ble_scan(), false,
                       true));
    out.push_back(make("ble", "skimmer", "Skimmer Detector", Tid::SkimmerDetector, Kind::Log, net_ble_scan(), false,
                       true));
    out.push_back(make("ble", "drone_id", "Drone ID (BLE)", Tid::DroneId, Kind::Log, net_ble_scan(), false, true));
    out.push_back(make("ble", "ble_disconnect", "Disconnect", Tid::BleDisconnect, Kind::Log, net_ble_disconnect(),
                       false, true));
    out.push_back(make("ble", "ninebot_status", "Ninebot Status", Tid::NinebotStatus, Kind::Log,
                       net_ble_status("ninebot", "6e400001-b5a3-f393-e0a9-e50e24dcca9e"), false, true));
    out.push_back(make("ble", "amiibo_status", "Amiibolink Status", Tid::AmiiboStatus, Kind::Log,
                       net_ble_status("amiibo", ""), false, true));
    out.push_back(make("ble", "pn532_status", "PN532-BLE Status", Tid::Pn532Status, Kind::Log,
                       net_ble_status("pn532", ""), false, true));
    out.push_back(make("ble", "chameleon_status", "Chameleon Status", Tid::ChameleonStatus, Kind::Log,
                       net_ble_status("chameleon", ""), false, true));

    // Ethernet
    out.push_back(make("ethernet", "eth_link", "Link status", Tid::None, Kind::Log, net_eth_link()));
    out.push_back(make("ethernet", "scan_hosts", "Scan Hosts", Tid::ScanHosts, Kind::Log, net_scan_hosts()));

    // Files
    out.push_back(make("files", "files_browser", "Browse", Tid::FilesBrowser, Kind::Log, proof_files_list(), false,
                       false, {{"path", "relative under home", false}}));
    out.push_back(make("files", "files_share_hint", "File Share (Wi-Fi)", Tid::FilesShareHint, Kind::Info,
                       net_files_share_hint()));

    // Scripts / PC Connect
    out.push_back(make("scripts", "scripts", "Local scripts", Tid::ScriptsRunner, Kind::Log, scripts_runner()));
    out.push_back(make("scripts", "pcconnect_scripts", "PC Connect scripts", Tid::PcConnectScripts, Kind::Log,
                       pcconnect_scripts()));
    out.push_back(make("scripts", "pcconnect_howto", "PC Connect how to", Tid::PcConnectHowTo, Kind::Info,
                       pcconnect_howto()));
    out.push_back(make("pcconnect", "pcconnect_howto", "How to use", Tid::PcConnectHowTo, Kind::Info,
                       pcconnect_howto()));
    out.push_back(make("pcconnect", "pcconnect_needs", "What you need", Tid::PcConnectNeeds, Kind::Info,
                       pcconnect_needs()));
    out.push_back(make("pcconnect", "pcconnect_scripts", "Browse scripts", Tid::PcConnectScripts, Kind::Log,
                       pcconnect_scripts()));
    out.push_back(make("pcconnect", "pcconnect_export", "Export to config", Tid::PcConnectExport, Kind::Log,
                       pcconnect_export()));

    // Infrared
    out.push_back(make("infrared", "ir_use", "Use", Tid::IrUse, Kind::Log, ir_use(), true, true));
    out.push_back(make("infrared", "ir_learn", "Learn", Tid::IrLearn, Kind::Log, ir_learn(), false, true));
    out.push_back(make("infrared", "ir_browse", "Browse", Tid::IrBrowse, Kind::Log, ir_browse(), true, true));
    out.push_back(make("infrared", "ir_read", "Read", Tid::IrRead, Kind::Log, ir_read(), false, true));
    out.push_back(make("infrared", "ir_favorites", "Favorites", Tid::IrFavorites, Kind::Log, ir_favorites(), true,
                       true));
    out.push_back(make("infrared", "ir_config", "Config", Tid::IrConfig, Kind::Log, ir_config()));
    out.push_back(make("infrared", "roku", "Roku Remote", Tid::Roku, Kind::Log, net_roku()));
    out.push_back(make("infrared", "tv_b_gone", "TV-B-Gone", Tid::TvBGone, Kind::Log, ir_tv_b_gone(), true, true));
    out.push_back(make("infrared", "tagtinker", "TagTinker ESL", Tid::TagTinker, Kind::Log, ir_tagtinker(), true,
                       true));
    out.push_back(make("infrared", "ir_carrier", "IR Carrier", Tid::IrCarrier, Kind::Log, ir_carrier(), true, true));

    // GPS
    out.push_back(make("gps", "gps_wardriving", "Wardriving", Tid::GpsWardrivingMenu, Kind::Menu, {}, false, false,
                       {},
                       {"gps_scan_wifi", "gps_scan_ble", "gps_scan_both", "gps_captures", "gps_wd_config",
                        "gps_wd_master"}));
    out.push_back(make("gps", "gps_site_survey", "Site Survey", Tid::GpsSiteSurvey, Kind::Log, gps_site_survey(),
                       false, true));
    out.push_back(make("gps", "gps_tracker", "GPS Tracker", Tid::GpsTracker, Kind::Log, gps_tracker(), false, true));
    out.push_back(make("gps", "gps_config", "Config", Tid::GpsConfigMenu, Kind::Menu, {}, false, false, {},
                       {"gps_hardware", "gps_source", "gps_nmea_path", "gps_baud", "gps_status", "gps_folder"}));
    out.push_back(make("gps", "gps_scan_wifi", "Scan WiFi Networks", Tid::GpsScanWifi, Kind::Log, gps_live("wifi"),
                       false, true));
    out.push_back(make("gps", "gps_scan_ble", "Scan BLE Devices", Tid::GpsScanBle, Kind::Log, gps_live("ble"), false,
                       true));
    out.push_back(make("gps", "gps_scan_both", "Scan Both", Tid::GpsScanBoth, Kind::Log, gps_live("both"), false,
                       true));
    out.push_back(make("gps", "gps_captures", "View Captures", Tid::GpsCaptures, Kind::Log, gps_captures()));
    out.push_back(make("gps", "gps_wd_config", "WD Config", Tid::GpsWdConfig, Kind::Log, gps_config_leaf("wd")));
    out.push_back(make("gps", "gps_wd_master", "Wardriving Master", Tid::GpsWdMaster, Kind::Info,
                       tools_info("Wardriving Master stub")));
    out.push_back(make("gps", "gps_live", "Live Wardrive", Tid::GpsLive, Kind::Log, gps_live("live"), false, true));
    out.push_back(make("gps", "gps_status", "GPS Status", Tid::GpsStatus, Kind::Log, gps_status(), false, true));
    out.push_back(make("gps", "gps_source", "GPS Source", Tid::GpsSource, Kind::Log, gps_config_leaf("source"), false,
                       false, {{"value", "auto|gpsd|nmea", false}}));
    out.push_back(make("gps", "gps_hardware", "GPS Hardware", Tid::GpsHardware, Kind::Log,
                       gps_config_leaf("hardware"), false, false, {{"value", "unit_v11|cap_lora", false}}));
    out.push_back(make("gps", "gps_nmea_path", "NMEA device path", Tid::GpsNmeaPath, Kind::Log,
                       gps_config_leaf("nmea_path"), false, false, {{"path", "/dev/serial0", false}}));
    out.push_back(make("gps", "gps_baud", "Baudrate", Tid::GpsBaud, Kind::Log, gps_config_leaf("baud"), false, false,
                       {{"baud", "9600|115200", false}}));
    out.push_back(make("gps", "gps_folder", "Captures folder", Tid::GpsFolder, Kind::Log, gps_captures()));

    // Modules
    out.push_back(make("modules", "cap_probe", "Cap Probe", Tid::CapProbe, Kind::Log, modules_cap_probe(), false,
                       true));
    out.push_back(make("modules", "bus_scan", "Bus Scanner", Tid::BusScan, Kind::Log, modules_bus_scan(), false,
                       true));
    out.push_back(make("modules", "unit_scroll", "Unit Scroll", Tid::UnitScrollStatus, Kind::Info,
                       modules_unit_scroll()));
    out.push_back(make("modules", "uart_shell", "UART Shell", Tid::UartShell, Kind::Log, net_uart_shell(), false,
                       true, {{"device", "tty", true}}));
    out.push_back(make("modules", "pahub_scan", "PaHub Scan", Tid::PahubScan, Kind::Log, modules_pahub(), false,
                       true));
    out.push_back(make("modules", "ibutton_read", "iButton Read", Tid::IButtonRead, Kind::Log, modules_ibutton(),
                       false, true));
    out.push_back(make("modules", "pn532_killer", "PN532Killer UART", Tid::Pn532KillerUart, Kind::Info,
                       modules_pn532_killer()));
    out.push_back(make("modules", "uart_llm", "UART LLM", Tid::UartLlm, Kind::Info, modules_uart_llm()));
    out.push_back(make("modules", "joystick", "Unit Joystick", Tid::None, Kind::Info, modules_joystick()));
    out.push_back(make("modules", "pahub_presence", "PaHub presence", Tid::None, Kind::Log, modules_pahub_presence(),
                       false, true));

    // RF / NRF / LoRa / FM
    out.push_back(make("rf", "rf_status", "Cap Status", Tid::RfStatus, Kind::Log, radio_rf("rf_status"), false, true));
    out.push_back(make("rf", "rf_spectrum", "Spectrum", Tid::RfSpectrum, Kind::Log, radio_rf("rf_spectrum"), false,
                       true));
    out.push_back(make("rf", "rf_listen", "Listen", Tid::RfListen, Kind::Log, radio_rf("rf_listen"), false, true));
    out.push_back(make("rf", "rf_record", "Record RAW", Tid::RfRecord, Kind::Log, radio_rf("rf_record"), false, true));
    out.push_back(make("rf", "rf_replay", "Replay / Custom", Tid::RfReplay, Kind::Log, radio_rf("rf_replay"), true,
                       true));
    out.push_back(make("rf", "rf_library", "Protocol Library", Tid::RfLibrary, Kind::Log, radio_rf("rf_library"), true,
                       true));
    out.push_back(make("rf", "rf_send_test", "Send Test", Tid::RfSendTest, Kind::Log, radio_rf("rf_send_test"), true,
                       true));

    out.push_back(make("nrf24", "nrf24_info", "Information", Tid::Nrf24Info, Kind::Info, radio_nrf24("nrf24_info")));
    out.push_back(make("nrf24", "nrf24_status", "Module Status", Tid::Nrf24Status, Kind::Log,
                       radio_nrf24("nrf24_status"), false, true));
    out.push_back(make("nrf24", "nrf24_spectrum", "Spectrum", Tid::Nrf24Spectrum, Kind::Log,
                       radio_nrf24("nrf24_spectrum"), false, true));
    out.push_back(make("nrf24", "nrf24_listen", "Listen", Tid::Nrf24Listen, Kind::Log, radio_nrf24("nrf24_listen"),
                       false, true));
    out.push_back(make("nrf24", "nrf24_send", "Send Payload", Tid::Nrf24Send, Kind::Log, radio_nrf24("nrf24_send"),
                       true, true, {{"hex", "payload", true}}));

    out.push_back(make("lora", "lora_status", "Cap Status", Tid::LoraStatus, Kind::Log, radio_lora("lora_status"),
                       false, true));
    out.push_back(make("lora", "lora_chat", "Chat", Tid::LoraChat, Kind::Log, radio_lora("lora_chat"), false, true));
    out.push_back(make("lora", "lora_antenna", "Antenna Check", Tid::LoraAntenna, Kind::Log,
                       radio_lora("lora_antenna"), false, true));
    out.push_back(make("lora", "lora_freq", "Frequency", Tid::LoraFreq, Kind::Log, radio_lora("lora_freq"), false,
                       false, {{"hz", "frequency", true}}));
    out.push_back(make("lora", "lora_user", "Username", Tid::LoraUser, Kind::Log, radio_lora("lora_user"), false,
                       false, {{"name", "username", true}}));
    out.push_back(make("lora", "lora_mesh", "Meshtastic", Tid::LoraMeshHint, Kind::Info, radio_lora("lora_mesh")));

    out.push_back(make("fm", "fm_info", "Information", Tid::FmInfo, Kind::Info, radio_fm("fm_info")));
    out.push_back(make("fm", "fm_status", "Module Status", Tid::FmStatus, Kind::Log, radio_fm("fm_status"), false,
                       true));
    out.push_back(make("fm", "fm_broadcast", "Broadcast", Tid::FmBroadcast, Kind::Log, radio_fm("fm_broadcast"), true,
                       true, {{"mhz", "frequency", true}}));
    out.push_back(make("fm", "fm_stop", "Stop", Tid::FmStop, Kind::Log, radio_fm("fm_stop"), false, true));
    out.push_back(make("fm", "fm_spectrum", "Spectrum", Tid::FmSpectrum, Kind::Log, radio_fm("fm_spectrum"), false,
                       true));

    // RFID
    out.push_back(make("rfid", "rfid_status", "Module Status", Tid::RfidStatus, Kind::Log, rfid_tool("rfid_status"),
                       false, true));
    out.push_back(make("rfid", "rfid_read", "Read", Tid::RfidRead, Kind::Log, rfid_tool("rfid_read"), false, true));
    out.push_back(make("rfid", "rfid_scan", "Scan", Tid::RfidScan, Kind::Log, rfid_tool("rfid_scan"), false, true));
    out.push_back(make("rfid", "rfid_load", "Load", Tid::RfidLoad, Kind::Log, rfid_tool("rfid_load")));
    out.push_back(make("rfid", "rfid_save", "Save", Tid::RfidSave, Kind::Log, rfid_tool("rfid_save")));

    // NFC — menus + leaves
    auto nfc = [&](const char* key, const char* label, Tid id, Kind kind, bool confirm = false) {
        out.push_back(make("nfc", key, label, id, kind, nfc_tool(key), confirm, true));
    };
    nfc("nfc_status", "Module Status", Tid::NfcStatus, Kind::Log);
    nfc("nfc_read", "Read Tag", Tid::NfcRead, Kind::Log);
    nfc("nfc_emulate_ndef", "Emulate Tag", Tid::NfcEmulateNdef, Kind::Log);
    nfc("nfc_write_ndef", "Write Tag", Tid::NfcWriteNdef, Kind::Log, true);
    nfc("nfc_scan", "Scan tags", Tid::NfcScan, Kind::Log);
    nfc("nfc_load", "Load file", Tid::NfcLoad, Kind::Log);
    nfc("nfc_erase", "Erase data", Tid::NfcErase, Kind::Log, true);
    out.push_back(make("nfc", "nfc_classic_menu", "Classic...", Tid::NfcClassicMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_detect_reader", "nfc_mfkey", "nfc_dict_attack", "nfc_magic", "nfc_dump_editor"}));
    out.push_back(make("nfc", "nfc_tags_menu", "Tags...", Tid::NfcTagsMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_iso15693", "nfc_desfire", "nfc_ulc", "nfc_felica", "nfc_forced_type"}));
    out.push_back(make("nfc", "nfc_ndef_menu", "NDEF tools...", Tid::NfcNdefMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_read_records", "nfc_save_as_file", "nfc_memory_dump", "nfc_chip_info", "nfc_lock_tag",
                        "nfc_password", "nfc_format", "nfc_apdu"}));
    out.push_back(make("nfc", "nfc_lab_menu", "Lab...", Tid::NfcLabMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_apdu_script", "nfc_compare", "nfc_field_poll"}));
    out.push_back(make("nfc", "nfc_parsers_menu", "Parsers...", Tid::NfcParsersMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_amiibo", "nfc_transit", "nfc_epassport", "nfc_picopass", "nfc_eink"}));
    out.push_back(make("nfc", "nfc_companions_menu", "Companions...", Tid::NfcCompanionsMenu, Kind::Menu, {}, false,
                       false, {},
                       {"nfc_emv", "nfc_srix", "nfc_amiibolink", "nfc_pn532_ble", "nfc_pn532_uart", "nfc_chameleon"}));
    out.push_back(make("nfc", "nfc_config_menu", "Config", Tid::NfcConfigMenu, Kind::Menu, {}, false, false, {},
                       {"nfc_module", "nfc_wiring", "nfc_add_key"}));
    nfc("nfc_detect_reader", "Detect Reader", Tid::NfcDetectReader, Kind::Log);
    nfc("nfc_mfkey", "MFKey", Tid::NfcMfkey, Kind::Log);
    nfc("nfc_dict_attack", "Dict attack", Tid::NfcDictAttack, Kind::Log);
    nfc("nfc_magic", "NFC Magic", Tid::NfcMagic, Kind::Log);
    nfc("nfc_dump_editor", "Dump editor", Tid::NfcDumpEditor, Kind::Log);
    nfc("nfc_iso15693", "ISO15693 tool", Tid::NfcIso15693, Kind::Log);
    nfc("nfc_desfire", "DESFire browser", Tid::NfcDesfire, Kind::Log);
    nfc("nfc_ulc", "Ultralight-C", Tid::NfcUlc, Kind::Log);
    nfc("nfc_felica", "FeliCa services", Tid::NfcFelica, Kind::Log);
    nfc("nfc_forced_type", "Forced type read", Tid::NfcForcedType, Kind::Log);
    nfc("nfc_read_records", "Read records", Tid::NfcReadRecords, Kind::Log);
    nfc("nfc_save_as_file", "Save as file", Tid::NfcSaveAsFile, Kind::Log);
    nfc("nfc_memory_dump", "Memory dump", Tid::NfcMemoryDump, Kind::Log);
    nfc("nfc_chip_info", "Chip info", Tid::NfcChipInfo, Kind::Log);
    nfc("nfc_lock_tag", "Lock tag", Tid::NfcLockTag, Kind::Log, true);
    nfc("nfc_password", "Password", Tid::NfcPassword, Kind::Log, true);
    nfc("nfc_format", "Format memory", Tid::NfcFormat, Kind::Log, true);
    nfc("nfc_apdu", "NFC command", Tid::NfcApdu, Kind::Log);
    nfc("nfc_apdu_script", "APDU script", Tid::NfcApduScript, Kind::Log);
    nfc("nfc_compare", "Compare dump", Tid::NfcCompare, Kind::Log);
    nfc("nfc_field_poll", "Field poll", Tid::NfcFieldPoll, Kind::Log);
    nfc("nfc_amiibo", "Amiibo toolkit", Tid::NfcAmiibo, Kind::Log);
    nfc("nfc_transit", "Transit parse", Tid::NfcTransit, Kind::Log);
    nfc("nfc_epassport", "ePassport", Tid::NfcEpassport, Kind::Log);
    nfc("nfc_picopass", "Picopass / iCLASS", Tid::NfcPicopass, Kind::Log);
    nfc("nfc_eink", "NFC e-ink", Tid::NfcEink, Kind::Log);
    nfc("nfc_emv", "Read EMV", Tid::NfcEmv, Kind::Log);
    nfc("nfc_srix", "SRIX Tool", Tid::NfcSrix, Kind::Log);
    nfc("nfc_amiibolink", "Amiibolink", Tid::NfcAmiibolink, Kind::Log);
    nfc("nfc_pn532_ble", "PN532 BLE", Tid::NfcPn532Ble, Kind::Log);
    nfc("nfc_pn532_uart", "PN532 UART", Tid::NfcPn532Uart, Kind::Log);
    nfc("nfc_chameleon", "Chameleon", Tid::NfcChameleon, Kind::Log);
    nfc("nfc_module", "NFC Module", Tid::NfcModule, Kind::Log);
    nfc("nfc_wiring", "Wiring help", Tid::NfcWiring, Kind::Info);
    nfc("nfc_add_key", "Add MIF Key", Tid::NfcAddKey, Kind::Log);
    nfc("nfc_save", "Save dump help", Tid::NfcSave, Kind::Info);

    // Stand-ins / config / usb
    out.push_back(make("clock", "clock", "Clock", Tid::None, Kind::StandIn, standin_clock()));
    out.push_back(make("charge", "charge", "Charge", Tid::None, Kind::StandIn, standin_charge()));
    out.push_back(make("camera", "camera", "Camera", Tid::None, Kind::StandIn, standin_camera()));
    out.push_back(make("gallery", "gallery", "Gallery", Tid::None, Kind::StandIn, standin_gallery()));
    out.push_back(make("usb", "usb", "USB", Tid::None, Kind::Info,
                       tools_info("kvxkeyboard HID (USB / BLE keyboard, mouse, presenter) runs in the screen app: "
                                  "USB channel. BadUSB and mass storage are not available.")));
    out.push_back(make("config", "main_color", "Main color", Tid::None, Kind::Log, config_tool("main_color"), false,
                       false, {{"hex", "RRGGBB", true}}));
    out.push_back(make("config", "accent_color", "Accent color", Tid::None, Kind::Log, config_tool("accent_color"),
                       false, false, {{"hex", "RRGGBB", true}}));
    out.push_back(make("config", "background", "Background", Tid::None, Kind::Log, config_tool("background"), false,
                       false, {{"hex", "RRGGBB", true}}));
    out.push_back(make("config", "reset_colors", "Reset colors", Tid::None, Kind::Log, config_tool("reset_colors")));
    out.push_back(make("config", "brightness", "Brightness", Tid::None, Kind::Log, config_tool("brightness"), false,
                       false, {{"percent", "0-100", false}}));
    out.push_back(make("config", "dim_time", "Dim time", Tid::None, Kind::Info, tools_info("Dim time — set in GUI")));
    out.push_back(make("config", "sound", "Sound", Tid::None, Kind::Info, tools_info("Sound — set in GUI")));
    out.push_back(make("config", "reboot", "Restart", Tid::None, Kind::Log, config_tool("reboot"), true, false));
    out.push_back(make("config", "poweroff", "Power Off", Tid::None, Kind::Log, config_tool("poweroff"), true, false));
    out.push_back(make("config", "about", "About", Tid::None, Kind::Info, config_tool("about")));

    // Registry leftovers that live under "others" channel id
    out.push_back(make("others", "dtmf", "DTMF", Tid::Dtmf, Kind::Log, tools_dtmf()));
    out.push_back(make("others", "morse", "Morse", Tid::Morse, Kind::Log, tools_morse()));
    out.push_back(make("others", "microphone", "Microphone", Tid::Microphone, Kind::Log, tools_microphone()));
    out.push_back(make("others", "mqtt", "MQTT", Tid::Mqtt, Kind::Log, tools_mqtt()));
    out.push_back(make("others", "llm_chat", "LLM Chat", Tid::LlmChat, Kind::Log, tools_llm_chat()));
    out.push_back(make("others", "drone_id", "Drone ID", Tid::DroneId, Kind::Log, net_ble_scan(), false, true));
    out.push_back(make("others", "calculator", "Calculator", Tid::Calculator, Kind::Log, tools_calculator()));
    out.push_back(make("others", "media_player", "Media Player", Tid::MediaPlayer, Kind::Log, tools_media_player()));
    out.push_back(make("others", "ibutton", "iButton", Tid::IButtonHint, Kind::Info,
                       tools_info("Needs a 1-Wire accessory.")));

    return out;
}

} // namespace

const std::vector<Entry>& catalog() {
    static const std::vector<Entry> cat = build();
    return cat;
}

const Entry* find_entry(const char* channel, const char* key) {
    if (!channel || !key) return nullptr;
    for (const auto& e : catalog()) {
        if (std::strcmp(e.channel, channel) == 0 && std::strcmp(e.key, key) == 0) return &e;
    }
    return nullptr;
}

std::vector<const Entry*> entries_for_channel(const char* channel) {
    std::vector<const Entry*> out;
    if (!channel) return out;
    for (const auto& e : catalog()) {
        if (std::strcmp(e.channel, channel) == 0) out.push_back(&e);
    }
    return out;
}

std::vector<std::string> check_tool_ids_classified() {
    std::set<model::ToolId> seen;
    std::set<model::ToolId> reasoned;
    for (const auto& e : catalog()) {
        if (e.tool_id == model::ToolId::None || e.tool_id == model::ToolId::Count) continue;
        seen.insert(e.tool_id);
        if (e.reason) reasoned.insert(e.tool_id);
    }
    // matrix is not a ToolId — reason table is for catalog keys without ToolId
    std::vector<std::string> missing;
    for (int i = 1; i < static_cast<int>(model::ToolId::Count); ++i) {
        auto id = static_cast<model::ToolId>(i);
        if (!seen.count(id)) {
            const auto* d = model::find_tool(id);
            missing.push_back(std::string("unclassified ToolId ") + (d ? d->key : std::to_string(i)));
        }
    }
    return missing;
}

} // namespace runner
