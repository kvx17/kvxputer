#include "launch_catalog.h"

#include <algorithm>
#include <cstring>

namespace model {
namespace {

std::string lower_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

int channel_index_of(const char* id) {
    if (!id) return -1;
    for (size_t i = 0; i < kChannelCount; ++i) {
        if (std::strcmp(kChannels[i].id, id) == 0) return static_cast<int>(i);
    }
    return -1;
}

const char* channel_label(int index) {
    if (index < 0 || static_cast<size_t>(index) >= kChannelCount) return "";
    return kChannels[static_cast<size_t>(index)].label ? kChannels[static_cast<size_t>(index)].label : "";
}

void index_target(LaunchTarget& t) {
    t.fold_label = lower_copy(t.label);
    t.fold_group = lower_copy(t.group);
    t.fold_id = lower_copy(t.id);
}

void add_page(std::vector<LaunchTarget>& out,
              const char* id,
              const char* label,
              int channel_index,
              AppPage page,
              int aux = 0) {
    LaunchTarget t;
    t.id = id;
    t.label = label;
    t.group = channel_label(channel_index);
    t.kind = LaunchKind::Page;
    t.channel_index = channel_index;
    t.page = page;
    t.aux = aux;
    index_target(t);
    out.push_back(std::move(t));
}

std::vector<LaunchTarget> build_targets() {
    std::vector<LaunchTarget> out;
    out.reserve(kChannelCount + kToolCount + 16);

    for (size_t i = 0; i < kChannelCount; ++i) {
        LaunchTarget t;
        t.id = std::string("ch:") + kChannels[i].id;
        t.label = kChannels[i].label ? kChannels[i].label : kChannels[i].id;
        t.group = "Menu";
        t.kind = LaunchKind::Channel;
        t.channel_index = static_cast<int>(i);
        index_target(t);
        out.push_back(std::move(t));
    }

    const int tools = channel_index_of("others");
    const int config = channel_index_of("config");
    const int clock = channel_index_of("clock");

    // First-level Tools entries that are screens rather than ToolId rows.
    add_page(out, "page:codec", "Codec / Hash", tools, AppPage::CodecMenu);
    add_page(out, "page:engcalc", "Calculators", tools, AppPage::EngCalcMenu);
    // aux matches tools::engcalc::Op::Oui (keep in sync with eng_calc_lib.h).
    constexpr int kOpOui = 35;
    add_page(out, "page:mac_vendor", "Vendor MAC Search", tools, AppPage::EngCalcWork, kOpOui);
    add_page(out, "page:passgen", "Passgen", tools, AppPage::Passgen);
    add_page(out, "page:totp", "TOTP", tools, AppPage::TotpMenu);
    add_page(out, "page:combo", "Combo", tools, AppPage::ComboMenu);
    add_page(out, "page:qr", "QRCodes", tools, AppPage::QrMenu);
    add_page(out, "page:barcode", "Barcode", tools, AppPage::BarcodeMenu);
    add_page(out, "page:pda", "PDA", tools, AppPage::PdaHub);
    add_page(out, "page:timer", "Timer", clock >= 0 ? clock : tools, AppPage::Timer);
    add_page(out, "page:matrix", "Matrix", tools, AppPage::Matrix);

    add_page(out, "page:color_main", "Main color", config, AppPage::ColorPicker, 0);
    add_page(out, "page:color_accent", "Accent color", config, AppPage::ColorPicker, 1);
    add_page(out, "page:color_bg", "Background", config, AppPage::ColorPicker, 2);
    add_page(out, "page:brightness", "Brightness", config, AppPage::Brightness);
    add_page(out, "page:dim", "Dim time", config, AppPage::DimTime);
    add_page(out, "page:sound", "Sound", config, AppPage::SoundMenu);
    add_page(out, "page:power", "Power", config, AppPage::PowerMenu);
    add_page(out, "page:about", "About", config, AppPage::About);
    add_page(out, "page:shortcuts", "Shortcuts", config, AppPage::ShortcutMenu);

    for (const auto* tool : all_unique_tools()) {
        if (!tool || !tool->key || tool->id == ToolId::None) continue;
        // Calculator is a tool id that opens the calculator screen. The page
        // entries above already cover the other Tools-menu screens.
        LaunchTarget t;
        t.id = std::string("tool:") + tool->key;
        t.label = tool->label ? tool->label : tool->key;
        t.channel_index = channel_index_of(tool->channel_id);
        t.group = channel_label(t.channel_index);
        t.kind = LaunchKind::Tool;
        t.tool = tool->id;
        index_target(t);
        out.push_back(std::move(t));
    }
    return out;
}

int match_rank(const LaunchTarget& t, const std::string& q) {
    if (t.fold_label == q) return 0;
    if (t.fold_label.find(q) == 0) return 1;
    if (t.fold_label.find(q) != std::string::npos) return 2;
    if (t.fold_group.find(q) != std::string::npos) return 3;
    if (t.fold_id.find(q) != std::string::npos) return 4;
    return 100;
}

} // namespace

const std::vector<LaunchTarget>& launch_targets() {
    static const std::vector<LaunchTarget> targets = build_targets();
    return targets;
}

const LaunchTarget* find_launch_target(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& t : launch_targets()) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::vector<const LaunchTarget*> search_launch_targets(const std::string& query) {
    std::vector<const LaunchTarget*> hits;
    auto q = lower_copy(query);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();
    if (q.empty()) return hits;

    struct Ranked {
        const LaunchTarget* target;
        int rank;
        size_t order;
    };
    std::vector<Ranked> ranked;
    size_t order = 0;
    for (const auto& t : launch_targets()) {
        const int rank = match_rank(t, q);
        if (rank < 100) ranked.push_back({&t, rank, order});
        ++order;
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.order < b.order;
    });
    hits.reserve(ranked.size());
    for (const auto& r : ranked) hits.push_back(r.target);
    return hits;
}

const std::vector<DiscoveryEntry>& discovery_entries() {
    // Same set as the Cardputer ADV catalog items flagged discovery, keeping
    // only the tools this port actually ships.
    static const std::vector<DiscoveryEntry> entries = {
        {ToolId::BleScan, "BLE Scan"},
        {ToolId::BleHunter, "BLE Hunter"},
        {ToolId::WallOfFlipper, "Wall Of Flipper"},
        {ToolId::WallOfAirtag, "Wall Of Airtag"},
        {ToolId::SkimmerDetector, "Skimmer Detector"},
        {ToolId::WifiAnalyzer, "WiFi Analyzer"},
        {ToolId::WifiJamDetect, "Jam Detect"},
        {ToolId::WifiDeauthIds, "Deauth IDS"},
        {ToolId::PineapHunter, "PineAP Hunter"},
        {ToolId::LanWatch, "LAN Watch"},
        {ToolId::ScanHosts, "Scan Hosts"},
        {ToolId::OpenNetworks, "Open networks"},
        {ToolId::Roku, "Roku Remote"},
        {ToolId::BadUsbHunter, "BadUSB Hunter"},
        {ToolId::RfSpectrum, "RF Spectrum"},
        {ToolId::Nrf24Spectrum, "NRF Spectrum"},
        {ToolId::GpsScanBoth, "WD Both"},
        {ToolId::GpsSiteSurvey, "Site Survey"},
        {ToolId::GpsWdMaster, "Wardriving Master"},
        {ToolId::UpnpList, "UPnP Tools"},
    };
    return entries;
}

} // namespace model
