#pragma once

#include "runner.h"

namespace runner {

RunFn proof_codec();
RunFn proof_files_list();
RunFn proof_discovery();

RunFn tools_calculator();
RunFn tools_eng_calc();
RunFn tools_passgen();
RunFn tools_totp();
RunFn tools_combo();
RunFn tools_qr();
RunFn tools_barcode();
RunFn tools_pda();
RunFn tools_timer();
RunFn tools_media_player();
RunFn tools_dtmf();
RunFn tools_morse();
RunFn tools_microphone();
RunFn tools_mqtt();
RunFn tools_llm_chat();
RunFn tools_info(const char* text);

RunFn net_wifi_scan(bool open_only);
RunFn net_wifi_connect();
RunFn net_wifi_disconnect();
RunFn net_wifi_ap();
RunFn net_wifi_ap_info();
RunFn net_jam_detect();
RunFn net_deauth_ids();
RunFn net_badusb_hunter();
RunFn net_scan_hosts();
RunFn net_listen_tcp();
RunFn net_client_tcp(bool telnet);
RunFn net_ping();
RunFn net_shell(const char* title, const char* prefix);
RunFn net_upnp();
RunFn net_lan_watch();
RunFn net_eth_link();
RunFn net_ble_scan();
RunFn net_ble_status(const char* name_substr, const char* uuid_substr);
RunFn net_ble_disconnect();
RunFn net_sip_options();
RunFn net_ldap_read();
RunFn net_web_crawler();
RunFn net_cctv();
RunFn net_ssh_hint();
RunFn net_files_share_hint();
RunFn net_wireguard();
RunFn net_dead_drop();
RunFn net_honeypot();
RunFn net_roku();
RunFn net_printer();
RunFn net_socks4();
RunFn net_file_share();
RunFn net_wifi_monitor_adapter();
RunFn net_wifi_analyzer();
RunFn net_ap_detail();
RunFn net_loot_console();
RunFn net_check_handshakes();
RunFn net_ntlm_clean();
RunFn net_smb_status();
RunFn net_pineap();
RunFn net_uart_shell();

RunFn scripts_runner();
RunFn pcconnect_howto();
RunFn pcconnect_needs();
RunFn pcconnect_scripts();
RunFn pcconnect_export();

RunFn ir_use();
RunFn ir_learn();
RunFn ir_browse();
RunFn ir_read();
RunFn ir_favorites();
RunFn ir_config();
RunFn ir_tv_b_gone();
RunFn ir_tagtinker();
RunFn ir_carrier();

RunFn gps_status();
RunFn gps_live(const char* mode);
RunFn gps_config_leaf(const char* leaf);
RunFn gps_captures();
RunFn gps_tracker();
RunFn gps_site_survey();

RunFn modules_bus_scan();
RunFn modules_cap_probe();
RunFn modules_unit_scroll();
RunFn modules_pahub();
RunFn modules_companion_bins();
RunFn modules_ibutton();
RunFn modules_joystick();
RunFn modules_pahub_presence();
RunFn modules_pn532_killer();
RunFn modules_uart_llm();

RunFn radio_rf(const char* key);
RunFn radio_nrf24(const char* key);
RunFn radio_lora(const char* key);
RunFn radio_fm(const char* key);

RunFn rfid_tool(const char* key);
RunFn nfc_tool(const char* key);

RunFn standin_clock();
RunFn standin_charge();
RunFn standin_camera();
RunFn standin_gallery();
RunFn config_tool(const char* key);

} // namespace runner
