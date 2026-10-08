// Milestone-1 link stubs: NimBLE sources are excluded on ESP32-P4, and hosted
// ESP-NOW symbols are not linked from the P4 WiFi-remote package.
#if defined(KVX_NO_NIMBLE)

#include "menu/ble/ble_common.h"
#include "menu/ble/ble_ninebot.h"
#include "root/ui/display.h"
#include <esp_now.h>

BLEScan *pBLEScan = nullptr;
int scanTime = SCANTIME;

void ble_scan() { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }
void stopBLEStack() {}
bool ble_scan_setup() { return false; }
void ble_test() {}
bool bleNotifyRetry(NimBLECharacteristic *, const uint8_t *, size_t, uint8_t) { return false; }
bool bleNotifyRetry(NimBLECharacteristic *, uint8_t) { return false; }
void disPlayBLESend() {}

// hidRemoteMenu is compiled from menu/ble/hid_remote/ (USB path works; BLE returns error).

void bleHunterMenu() { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }
void spamMenu() { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }
void ibeacon(const char *, const char *, int) { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }
void BleSuiteMenu() { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }

#if !defined(LITE_VERSION)
BLENinebot::BLENinebot() { displayInfo("BLE gated on Tab5\n(NimBLE/P4)", true); }
BLENinebot::~BLENinebot() {}
void BLENinebot::setup() {}
void BLENinebot::loop() {}
void BLENinebot::clientDisconnect() {}
void BLENinebot::redrawMainBorder() {}
#endif

extern "C" {

esp_err_t esp_now_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_deinit(void) { return ESP_OK; }
esp_err_t esp_now_get_version(uint32_t *version) {
    if (version) *version = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_unregister_recv_cb(void) { return ESP_OK; }
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_unregister_send_cb(void) { return ESP_OK; }
esp_err_t esp_now_send(const uint8_t *, const uint8_t *, size_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_del_peer(const uint8_t *) { return ESP_OK; }
esp_err_t esp_now_mod_peer(const esp_now_peer_info_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_set_peer_rate_config(const uint8_t *, esp_now_rate_config_t *) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t esp_now_get_peer(const uint8_t *, esp_now_peer_info_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_fetch_peer(bool, esp_now_peer_info_t *) { return ESP_ERR_NOT_SUPPORTED; }
bool esp_now_is_peer_exist(const uint8_t *) { return false; }
esp_err_t esp_now_get_peer_num(esp_now_peer_num_t *num) {
    if (num) {
        num->total_num = 0;
        num->encrypt_num = 0;
    }
    return ESP_OK;
}
esp_err_t esp_now_set_pmk(const uint8_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_set_wake_window(uint16_t) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_set_user_oui(uint8_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_get_user_oui(uint8_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_switch_channel_tx(esp_now_switch_channel_t *) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_remain_on_channel(esp_now_remain_on_channel_t *) { return ESP_ERR_NOT_SUPPORTED; }

} // extern "C"

#endif // KVX_NO_NIMBLE
