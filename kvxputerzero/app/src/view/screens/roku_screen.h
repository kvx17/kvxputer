#pragma once

#include "kvx_screen.h"
#include "roku_ecp.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace screen {

class RokuScreen final : public KvxScreen {
public:
    RokuScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~RokuScreen() override;

    void on_shown() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;
    const char* typing_hint() const override;

private:
    enum class Phase { Picker, Remote };
    enum class InputMode { RemoteKeys, Keyboard };
    enum class Overlay { None, Help, Audio, Apps, Power, EnterIp };

    enum class PickerKind { Device, Rescan, EnterIp };

    struct PickerItem {
        PickerKind kind{PickerKind::Device};
        int device_index{-1};
    };

    enum class DpadCell { None, Up, Down, Left, Right, Ok };

    void start_scan();
    void apply_scan_results();
    void rebuild_picker_tiles();
    void refresh_picker_selection();
    void picker_move(int drow, int dcol);
    void picker_activate();
    void select_device(const tools::roku::Device& dev);
    void enter_remote();
    void leave_to_picker();

    void build_remote_ui();
    void destroy_remote_ui();
    void build_picker_ui();
    void destroy_picker_ui();
    void set_phase(Phase p);
    void set_input_mode(InputMode m, bool show_banner);
    void show_banner(const char* text);
    void clear_banner();
    void set_status(const std::string& s);
    void flash_dpad(DpadCell cell);
    void refresh_mode_pill();
    void refresh_remote_labels();

    void press_once(const char* key);
    void hold_down(const char* key);
    void hold_up(const char* key);
    void hold_bar(const char* key); // one-shot from action bar click
    bool handle_remote_key(uint32_t key);
    bool handle_keyboard_key(uint32_t key);
    bool handle_picker_key(uint32_t key);
    bool handle_overlay_key(uint32_t key);

    void open_help();
    void open_audio();
    void open_apps();
    void open_power();
    void open_enter_ip();
    void close_overlay();
    void rebuild_overlay();
    void launch_selected_app();
    void confirm_power();

    void on_cmd_result(const std::string& key, tools::roku::Status st);
    void arm_esc_toggle();
    void finish_esc_release();
    void exit_roku();
    static void poll_timer_cb(lv_timer_t* t);
    static void on_key_up(uint32_t key, const char* name, void* user_data);
    static bool on_early_key(uint32_t key, const char* name, bool long_pressed, void* user_data);

    void save_ip(const std::string& ip);
    std::string load_saved_ip() const;

    Phase phase_{Phase::Picker};
    InputMode mode_{InputMode::RemoteKeys};
    Overlay overlay_{Overlay::None};

    tools::roku::CommandQueue queue_;
    tools::roku::Device device_{};
    std::vector<tools::roku::Device> devices_;
    std::vector<PickerItem> picker_items_;
    int picker_sel_{0};
    int picker_scroll_row_{0};

    std::vector<tools::roku::App> apps_;
    int apps_sel_{0};
    bool apps_loading_{false};
    tools::roku::AudioInfo audio_{};
    bool audio_loading_{false};

    std::mutex scan_mu_;
    std::vector<tools::roku::Device> scan_pending_;
    bool scan_done_{false};
    bool scanning_{false};
    std::atomic<bool> cancel_scan_{false};
    std::thread scan_thread_;

    std::mutex result_mu_;
    std::string pending_status_;
    bool pending_forbidden_{false};
    bool showed_forbidden_{false};

    std::string status_text_;
    std::string typed_buffer_;
    std::string enter_ip_buf_;
    uint32_t banner_until_ms_{0};
    uint32_t flash_until_ms_{0};
    DpadCell flash_cell_{DpadCell::None};

    // Esc: short release toggles Remote/Keyboard; hold exits the tool.
    bool esc_armed_{false};
    bool esc_long_exit_{false};

    // Held ECP keys mapped from LV keys.
    std::string held_ecp_;

    lv_timer_t* poll_timer_{nullptr};

    // Picker widgets
    lv_obj_t* picker_root_{nullptr};
    lv_obj_t* picker_status_{nullptr};
    lv_obj_t* picker_grid_{nullptr};
    std::vector<lv_obj_t*> picker_tiles_;
    std::vector<lv_obj_t*> picker_icons_;
    std::vector<lv_obj_t*> picker_labels_;

    // Remote widgets
    lv_obj_t* remote_root_{nullptr};
    lv_obj_t* name_lbl_{nullptr};
    lv_obj_t* mode_pill_{nullptr};
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* typed_lbl_{nullptr};
    lv_obj_t* dpad_cells_[5]{}; // U D L R Ok
    lv_obj_t* banner_{nullptr};
    lv_obj_t* banner_lbl_{nullptr};

    // Overlay
    lv_obj_t* overlay_root_{nullptr};
};

} // namespace screen
