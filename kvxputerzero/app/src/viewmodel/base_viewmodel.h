#pragma once

#include "base_model.h"
#include "subjects.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* channel_index_subject();
    lv_subject_t* menu_index_subject();
    lv_subject_t* pri_color_subject();
    lv_subject_t* sec_color_subject();
    lv_subject_t* quit_requested_subject();

    lv_subject_t* greeting_subject();
    lv_subject_t* counter_subject();
    lv_subject_t* bold_text_subject();
    lv_subject_t* info_visible_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void go_home();
    void go_back();

    int channel_index() const;
    void set_channel_index(int index);
    void move_channel(int delta);
    void open_selected_channel();

    int menu_index() const;
    void set_menu_index(int index);
    void move_menu(int delta);

    void open_codec_menu();
    void open_codec_op(int op);
    void open_eng_calc_menu();
    void open_eng_calc_op(int op);
    void open_timer();
    void open_config_menu();
    void open_color_picker(model::ColorTarget target);
    void open_about();

    void open_passgen();
    void open_totp_menu();
    void open_totp_show();
    void open_totp_add();
    void open_totp_help();
    void open_combo_menu();
    void open_combo_work();
    void open_combo_help();
    void open_qr_menu();
    void open_qr_custom();
    void open_qr_save();
    void open_qr_pix();
    void open_qr_remove();
    void open_qr_view();
    void open_barcode_menu();
    void open_barcode_work(int type);
    void open_barcode_view();
    void set_qr_view_data(std::vector<uint8_t> modules, int size, std::string payload);
    void set_barcode_view_data(std::string bits, std::string label, std::string payload);

    void open_pda_hub();
    void open_pda_channel(int channel);
    void open_pda_list(model::PdaKind kind);
    void open_pda_edit(model::PdaEditMode mode, bool multiline = false);
    void open_pda_view();
    void open_pda_calendar();
    void open_pda_day();
    void open_pda_world_clock();
    void open_pda_rebind();
    void open_pda_stock_calc();
    void persist_pda_config();

    model::ColorTarget color_target() const;
    uint32_t pri_color() const;
    uint32_t sec_color() const;
    void set_pri_color(uint32_t rgb);
    void set_sec_color(uint32_t rgb);
    void persist_theme();

    int codec_op() const;
    int eng_calc_op() const;
    int timer_seconds() const;
    void set_timer_seconds(int seconds);

    const std::string& work_input() const;
    void set_work_input(std::string value);
    const std::string& work_output() const;
    void set_work_output(std::string value);

    int barcode_type() const;
    model::GlyphKind glyph_kind() const;
    const std::string& glyph_payload() const;
    const std::string& glyph_bits() const;
    const std::string& glyph_label() const;
    bool glyph_invert() const;
    void set_glyph_invert(bool invert);
    const std::vector<uint8_t>& qr_modules() const;
    int qr_size() const;
    int totp_index() const;
    void set_totp_index(int index);

    const model::ChannelDesc& current_channel() const;
    model::BaseModel& model();

    void request_quit();

    void show_apple_page();
    void show_butter_page();
    void toggle_page();
    void increment_counter();
    void decrement_counter();
    void toggle_bold_text();
    void toggle_info();

private:
    void publish_all();

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::StringSubject<32> greeting_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::IntSubject channel_index_subject_;
    reactive::IntSubject menu_index_subject_;
    reactive::IntSubject pri_color_subject_;
    reactive::IntSubject sec_color_subject_;
    reactive::IntSubject counter_subject_;
    reactive::BoolSubject bold_text_subject_;
    reactive::BoolSubject info_visible_subject_;
    reactive::BoolSubject quit_requested_subject_;
};

} // namespace viewmodel
