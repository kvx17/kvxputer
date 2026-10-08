// Hosted ESP-NOW symbols are not linked from the P4 WiFi-remote package.
// BLE entry points live in menu/ble and the hosted-C6 backend.
#if defined(ARDUINO_M5STACK_TAB5)

#include <esp_now.h>

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

#endif // ARDUINO_M5STACK_TAB5
