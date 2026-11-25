#include "about_screen.h"

#include "asset_manager.h"
#include "device_status.h"
#include "kvx_style.h"
#include "net_info.h"
#include "preset_links.h"
#include "qr_lib.h"
#include "theme.h"
#include "ui_const.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace screen {
namespace {

constexpr int kLineScroll = 14;
constexpr int kPageScroll = 56;
// Panel viewport is ~106px. The code sits beside its label so the whole glyph fits.
constexpr int kQrPx = 96;
// Thin white rim. The modules themselves scale to fill the rest of the square.
constexpr int kQuietPx = 4;

std::string trim_copy(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string read_line_file(const char* path) {
    std::ifstream in(path);
    std::string line;
    if (!std::getline(in, line)) return {};
    if (!line.empty() && line.back() == '\0') line.pop_back();
    return trim_copy(line);
}

std::string pretty_os() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        line = line.substr(12);
        if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
            line = line.substr(1, line.size() - 2);
        }
        return line;
    }
    return {};
}

std::string cpu_line() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    std::string model;
    int cores = 0;
    while (std::getline(in, line)) {
        if (line.rfind("processor", 0) == 0) ++cores;
        const auto colon = line.find(':');
        if (colon == std::string::npos || !model.empty()) continue;
        const std::string key = trim_copy(line.substr(0, colon));
        if (key == "model name" || key == "Processor" || key == "Hardware" || key == "Model") {
            model = trim_copy(line.substr(colon + 1));
        }
    }
    if (model.empty() && cores <= 0) return {};
    if (cores > 0) {
        model += model.empty() ? std::to_string(cores) + " cores" : " (" + std::to_string(cores) + ")";
    }
    return model;
}

std::string fmt_kib(uint64_t kib) {
    double v = static_cast<double>(kib);
    const char* unit = "KiB";
    if (v >= 1024.0) {
        v /= 1024.0;
        unit = "MiB";
    }
    if (v >= 1024.0) {
        v /= 1024.0;
        unit = "GiB";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, unit);
    return buf;
}

std::string mem_line() {
    std::ifstream in("/proc/meminfo");
    std::string line;
    uint64_t total = 0;
    uint64_t avail = 0;
    while (std::getline(in, line)) {
        unsigned long long n = 0;
        if (std::sscanf(line.c_str(), "MemTotal: %llu kB", &n) == 1) total = n;
        else if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &n) == 1) avail = n;
    }
    if (total == 0) return {};
    const uint64_t used = total > avail ? total - avail : 0;
    return fmt_kib(used) + " / " + fmt_kib(total);
}

std::string disk_line() {
    struct statvfs st {};
    if (statvfs("/", &st) != 0 || st.f_blocks == 0) return {};
    const uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize / 1024;
    const uint64_t free = static_cast<uint64_t>(st.f_bavail) * st.f_frsize / 1024;
    const uint64_t used = total > free ? total - free : 0;
    return fmt_kib(used) + " / " + fmt_kib(total);
}

std::string uptime_line() {
    std::ifstream in("/proc/uptime");
    double seconds = 0;
    if (!(in >> seconds) || seconds < 0) return {};
    const int total = static_cast<int>(seconds);
    const int days = total / 86400;
    const int hours = (total % 86400) / 3600;
    const int mins = (total % 3600) / 60;
    char buf[32];
    if (days > 0) std::snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
    else if (hours > 0) std::snprintf(buf, sizeof(buf), "%dh %dm", hours, mins);
    else std::snprintf(buf, sizeof(buf), "%dm", mins);
    return buf;
}

std::string load_line() {
    std::ifstream in("/proc/loadavg");
    double a = 0, b = 0, c = 0;
    if (!(in >> a >> b >> c)) return {};
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", a, b, c);
    return buf;
}

std::string dns_line() {
    std::ifstream in("/etc/resolv.conf");
    std::string line;
    std::string out;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string key, addr;
        iss >> key >> addr;
        if (key != "nameserver" || addr.empty()) continue;
        if (!out.empty()) out += " ";
        out += addr;
    }
    return out;
}

lv_obj_t* add_text(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, view::kScreenWidth - 24);
    lv_obj_set_style_text_font(label, font ? font : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

} // namespace

AboutScreen::AboutScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : KvxScreen(view_model, assets) {
    init();
}

AboutScreen::~AboutScreen() {
    for (auto* img : images_) {
        if (img && lv_obj_is_valid(img)) lv_image_set_src(img, nullptr);
    }
}

bool AboutScreen::paint_qr(QrSlot& slot, const char* url) {
    tools::qr::Matrix matrix;
    std::string err;
    if (!url || !tools::qr::encode_text(url, matrix, err) || matrix.size <= 0) return false;

    slot.pixels.assign(static_cast<size_t>(kQrPx * kQrPx * 4), 0);
    auto set_px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        auto* p = slot.pixels.data() + static_cast<size_t>(y * kQrPx + x) * 4;
        const lv_color32_t c = lv_color32_make(r, g, b, 255);
        std::memcpy(p, &c, 4);
    };
    for (int y = 0; y < kQrPx; ++y) {
        for (int x = 0; x < kQrPx; ++x) set_px(x, y, 255, 255, 255);
    }

    const int inner = kQrPx - kQuietPx * 2;
    for (int y = 0; y < matrix.size; ++y) {
        const int y0 = kQuietPx + y * inner / matrix.size;
        const int y1 = kQuietPx + (y + 1) * inner / matrix.size;
        for (int x = 0; x < matrix.size; ++x) {
            if (matrix.modules[static_cast<size_t>(y * matrix.size + x)] == 0) continue;
            const int x0 = kQuietPx + x * inner / matrix.size;
            const int x1 = kQuietPx + (x + 1) * inner / matrix.size;
            for (int py = y0; py < y1; ++py) {
                for (int px = x0; px < x1; ++px) {
                    if (px < 0 || py < 0 || px >= kQrPx || py >= kQrPx) continue;
                    set_px(px, py, 0, 0, 0);
                }
            }
        }
    }

    std::memset(&slot.dsc, 0, sizeof(slot.dsc));
    slot.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot.dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    slot.dsc.header.w = kQrPx;
    slot.dsc.header.h = kQrPx;
    slot.dsc.header.stride = kQrPx * 4;
    slot.dsc.data_size = static_cast<uint32_t>(slot.pixels.size());
    slot.dsc.data = slot.pixels.data();
    return true;
}

void AboutScreen::add_link(const char* title, const char* shown, const char* url, QrSlot& slot) {
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    const lv_font_t* small = assets().load_standard_font(10);
    if (!font) font = &lv_font_montserrat_12;
    if (!small) small = font;

    auto* row = lv_obj_create(panel_);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_margin_top(row, 8, 0);

    if (paint_qr(slot, url)) {
        auto* img = lv_image_create(row);
        lv_image_set_src(img, &slot.dsc);
        lv_obj_set_size(img, kQrPx, kQrPx);
        for (auto*& stored : images_) {
            if (!stored) {
                stored = img;
                break;
            }
        }
    } else {
        add_text(row, "QR unavailable", small, tok.ink_dim);
    }

    auto* col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);

    const int text_w = view::kScreenWidth - 40 - kQrPx;
    auto* heading = add_text(col, title, font, tok.accent);
    lv_obj_set_width(heading, text_w);
    auto* link = add_text(col, shown, small, tok.ink);
    lv_obj_set_width(link, text_w);
}

void AboutScreen::build() {
    make_header(content(), "About");
    const auto tok = tokens();
    const lv_font_t* font = assets().load_standard_font(12);
    const lv_font_t* small = assets().load_standard_font(10);
    if (!font) font = &lv_font_montserrat_12;
    if (!small) small = font;

    panel_ = view::make_panel(content(), 18, tok);
    lv_obj_set_style_pad_top(panel_, 4, 0);
    lv_obj_set_style_pad_bottom(panel_, 4, 0);
    lv_obj_set_style_pad_row(panel_, 3, 0);

    add_text(panel_, "kvxputerzero " KVX_APP_VERSION " " KVX_APP_REVISION, font, tok.accent);
    add_text(panel_,
             "kvxputerzero is the kvxputer toolkit as a Cardputer Zero app. It is a Wii-style "
             "channel home with Wi-Fi, NetOps, BLE, radio, infrared, GPS, RFID, NFC, files, and "
             "everyday tools, running on the Zero's Linux image.\n"
             "\n"
             "Use it only on networks and devices you own or have permission to test.\n"
             "\n"
             "This is a one-person project, made in my own time for education. I do not profit "
             "from it. If you want to contribute, you can buy me a coffee.",
             font, tok.ink);

    add_link("Repository", "github.com/kvx17/kvxputer", tools::links::kRepoUrl, repo_qr_);
    add_link("GitHub", "github.com/kvx17", tools::links::kGithubUrl, github_qr_);
    add_link("Buy me a coffee", "buymeacoffee.com/kvx17", tools::links::kCoffeeUrl, coffee_qr_);

    auto* device = add_text(panel_, "This device", font, tok.accent);
    lv_obj_set_style_margin_top(device, 8, 0);

    auto add_fact = [&](const char* label, const std::string& value) {
        if (value.empty()) return;
        view::make_kv_row(panel_, label, value.c_str(), small, tok);
    };

    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1) == 0) add_fact("host", host);
    if (const passwd* pw = getpwuid(geteuid())) add_fact("user", pw->pw_name);
    add_fact("os", pretty_os());
    struct utsname uts {};
    if (uname(&uts) == 0) {
        add_fact("kernel", uts.release);
        add_fact("arch", uts.machine);
    }
    add_fact("board", read_line_file("/proc/device-tree/model"));
    add_fact("cpu", cpu_line());
    add_fact("mem", mem_line());
    add_fact("disk", disk_line());
    add_fact("uptime", uptime_line());
    add_fact("load", load_line());

    const auto st = platform::read_device_status();
    if (st.battery_present && st.battery_percent >= 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d%%%s", static_cast<int>(st.battery_percent),
                      st.battery_charging ? " charging" : "");
        add_fact("battery", buf);
    }
    if (st.wifi_present) {
        if (st.wifi_connected && st.wifi_strength_percent >= 0) {
            add_fact("wifi", std::to_string(st.wifi_strength_percent) + "%");
        } else {
            add_fact("wifi", st.wifi_connected ? "up" : "down");
        }
    } else if (st.ethernet_present) {
        add_fact("ethernet", st.ethernet_connected ? "up" : "down");
    }

    std::string err;
    const auto iface = platform::default_route_iface(err);
    add_fact("iface", iface.name);
    if (!iface.ipv4.empty()) {
        std::string ip = iface.ipv4;
        if (iface.prefix >= 0) ip += "/" + std::to_string(iface.prefix);
        add_fact("ipv4", ip);
    }
    add_fact("mac", iface.hw_address);
    add_fact("gateway", iface.gateway);
    add_fact("dns", dns_line());

    scroll_hint_ = lv_label_create(content());
    lv_label_set_text(scroll_hint_, "");
    lv_obj_set_style_text_font(scroll_hint_, small, 0);
    lv_obj_set_style_text_color(scroll_hint_, tok.ink_dim, 0);
    lv_obj_align(scroll_hint_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    update_scroll_hint();
}

void AboutScreen::update_scroll_hint() {
    if (!panel_ || !scroll_hint_) return;
    const int32_t scroll_y = lv_obj_get_scroll_y(panel_);
    const int32_t bottom = lv_obj_get_scroll_bottom(panel_);
    if (bottom <= 2 && scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "");
        return;
    }
    if (bottom <= 2) {
        lv_label_set_text(scroll_hint_, "^");
    } else if (scroll_y <= 2) {
        lv_label_set_text(scroll_hint_, "v");
    } else {
        lv_label_set_text(scroll_hint_, "^v");
    }
}

void AboutScreen::scroll_by(int px) {
    if (!panel_) return;
    const int32_t y = lv_obj_get_scroll_y(panel_);
    lv_obj_scroll_to_y(panel_, y + px, LV_ANIM_OFF);
    update_scroll_hint();
}

bool AboutScreen::handle_key(uint32_t key) {
    if (key == LV_KEY_DOWN) {
        scroll_by(kLineScroll);
        return true;
    }
    if (key == LV_KEY_UP) {
        scroll_by(-kLineScroll);
        return true;
    }
    if (key == '.') {
        scroll_by(kPageScroll);
        return true;
    }
    if (key == ',') {
        scroll_by(-kPageScroll);
        return true;
    }
    return KvxScreen::handle_key(key);
}

std::array<view::widgets::ActionSlot, 5> AboutScreen::nav_actions() {
    return {{
        {view::ICON_ARROW_BACK, [this]() { view_model().go_back(); }},
        {view::ICON_CARET_UP, [this]() { scroll_by(-kLineScroll); }},
        {"", {}},
        {view::ICON_CARET_DOWN, [this]() { scroll_by(kLineScroll); }},
        {view::ICON_HOUSE, [this]() { view_model().go_home(); }},
    }};
}

} // namespace screen
