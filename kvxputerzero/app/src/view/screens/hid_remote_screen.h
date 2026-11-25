#pragma once

// kvxkeyboard HID: the Cardputer Zero acts as a USB or Bluetooth LE keyboard,
// mouse, presenter and media remote. Port of the ESP32 hid_remote app.

#include "hid_lib.h"
#include "hid_session.h"
#include "kvx_screen.h"
#include "linux_input.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace screen {

class HidRemoteScreen final : public KvxScreen {
public:
    HidRemoteScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~HidRemoteScreen() override;

    void on_shown() override;

protected:
    void build() override;
    bool handle_key(uint32_t key) override;
    std::array<view::widgets::ActionSlot, 5> nav_actions() override;
    const char* typing_hint() const override;

private:
    using Mode = tools::hid::Mode;

    enum class Phase {
        Transport, // pick USB / BLE (/ simulator on desktop)
        UsbWait,   // gadget up, waiting for the host to enumerate
        Hosts,     // BLE host slots 1-6
        HostMenu,  // options for one slot
        Menu,      // mode list
        Settings,
        Shortcuts,
        Edit,      // one-line text entry
        Mode,      // a running HID mode
    };

    struct Row {
        std::string text;
        std::function<void()> enter;
        std::function<void(int)> adjust; // Left / Right on settings rows
    };

    enum class CheckState { Ok, Wait, Todo, Fail };
    struct Check {
        CheckState state;
        std::string text;
    };

    struct HoldKey {
        bool down{false};
        bool fired{false};      // hold action ran; swallow until fully released
        uint32_t start{0};      // first press of this hold (0 = idle)
        uint32_t last_release{0};
        uint32_t tap_due{0};    // send the tap at this tick if no repeat arrives
        platform::KeyMods mods{};
    };

    struct Cell {
        lv_obj_t* box{nullptr};
        int id{-1};
    };

    // --- phases & chrome ---
    void set_phase(Phase p);
    void rebuild_body();
    void refresh_status();
    void show_message(const std::string& text, bool error = false);

    // --- lists ---
    void build_list(const std::vector<Row>& rows, int sel);
    void list_move(int delta);
    void list_enter();
    void list_adjust(int dir);
    std::vector<Row> rows_for_phase();
    std::vector<Row> transport_rows();
    std::vector<Row> menu_rows();
    std::vector<Row> settings_rows();
    std::vector<Row> shortcut_rows();
    std::vector<Row> host_menu_rows();

    // --- transport / hosts ---
    void start_transport(tools::hid::TransportPref pref);
    void stop_transport();
    void tick_usb_wait(uint32_t now);
    std::vector<Check> usb_checklist();
    void build_usb_checklist();
    platform::hid::LinkState link_state();
    std::string connected_addr();
    void sync_known_hosts();
    void build_hosts_grid();
    void select_slot(int slot);
    void pair_into_slot(int slot);
    void hosts_enter();
    void poll_hosts();
    std::string slot_name(int slot);

    // --- text entry ---
    void begin_edit(const std::string& title, const std::string& initial, size_t max_len,
                    std::function<void(const std::string&)> done, Phase back, bool secret = false);
    bool handle_edit_key(uint32_t key);
    void refresh_edit();

    // --- modes ---
    void enter_mode(Mode m);
    void leave_mode();
    void build_mode_ui();
    lv_obj_t* make_cell(int id, int x, int y, int w, int h, const char* key, const char* desc);
    void flash(int id);
    void unflash_all();
    void refresh_mode_labels();

    bool handle_mode_key(uint32_t key);
    bool handle_presenter_key(uint32_t key);
    bool handle_media_key(uint32_t key);
    bool handle_mouse_key(uint32_t key);
    bool handle_shorts_key(uint32_t key);
    bool handle_clicker_key(uint32_t key);
    bool handle_jiggler_key(uint32_t key);
    bool handle_ptt_key(uint32_t key);
    void keyboard_send(uint32_t key, const platform::KeyMods& mods);
    bool forward_media(uint32_t key);
    void mirror_append(const std::string& s);
    void mouse_arrow(uint32_t key);
    void ptt_set(bool talking);
    void jiggler_schedule();
    void tick_mode(uint32_t now);
    void send_arrow(uint8_t usage);
    void adjust_setting(int& field, int delta, int lo, int hi);

    static void timer_cb(lv_timer_t* t);
    static bool on_early_key(uint32_t key, const char* name, bool long_pressed, void* user_data);
    static void on_key_up(uint32_t key, const char* name, void* user_data);
    bool early_key(uint32_t key, bool long_pressed);
    void key_up(uint32_t key);
    void on_hold(uint32_t key);
    void hold_press(HoldKey& h);
    void hold_release(HoldKey& h);
    void fire_hold(HoldKey& h, bool is_esc);
    void tick_hold(HoldKey& h, bool is_esc, uint32_t now);

    tools::hid::Settings settings_;
    std::unique_ptr<platform::hid::Session> session_;
    Phase phase_{Phase::Transport};
    Mode mode_{Mode::Presenter};

    // Chrome
    lv_obj_t* status_lbl_{nullptr};
    lv_obj_t* pill_{nullptr};
    lv_obj_t* body_{nullptr};
    std::string message_;
    bool message_error_{false};
    uint32_t message_until_{0};
    uint32_t last_status_ms_{0};

    // Lists
    lv_obj_t* list_{nullptr};
    std::vector<lv_obj_t*> list_rows_;
    std::vector<Row> rows_;
    int sel_{0};
    int sel_by_phase_[9]{};

    // Hosts
    std::vector<lv_obj_t*> slot_tiles_;
    int host_menu_slot_{0};
    int pairing_slot_{0};
    int waiting_slot_{0};
    uint32_t pending_slot_key_{0};
    bool auto_advance_{false};
    std::string usb_error_;
    uint32_t last_usb_retry_{0};
    std::future<std::pair<std::unique_ptr<platform::hid::Session>, std::string>> usb_retry_;
    std::string usb_rows_sig_;
    std::string last_live_addr_;
    bool forget_all_armed_{false};

    // Edit
    std::string edit_title_;
    std::string edit_buf_;
    size_t edit_max_{24};
    std::function<void(const std::string&)> edit_done_;
    Phase edit_back_{Phase::Menu};
    bool edit_secret_{false};
    lv_obj_t* edit_lbl_{nullptr};

    // Mode widgets / state
    std::vector<Cell> cells_;
    lv_obj_t* mode_text_{nullptr};
    lv_obj_t* mode_hint_{nullptr};
    lv_obj_t* mode_state_{nullptr};
    uint32_t flash_until_{0};
    Phase shortcuts_back_{Phase::Menu};

    std::string mirror_;
    HoldKey esc_;
    HoldKey tab_;

    uint32_t last_arrow_key_{0};
    uint32_t last_arrow_ms_{0};
    int arrow_streak_{0};

    int shorts_remap_{0}; // 0 run, 1 wait up, 2 wait down
    uint32_t shorts_pending_up_{0};

    bool running_{false}; // clicker / jiggler
    uint32_t next_action_ms_{0};
    unsigned long clicks_{0};
    unsigned long moves_{0};
    bool talking_{false};
    bool ptt_latched_{false};

    std::mt19937 rng_{std::random_device{}()};
    lv_timer_t* timer_{nullptr};
};

} // namespace screen
