#include "runner.h"
#include "channel_registry.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

std::atomic<bool> g_cancel{false};
bool g_raw_out = false;

void on_sigint(int) { g_cancel.store(true); }

// In raw mode '\n' alone advances the row without returning to column 0 (staircase).
void out(const std::string& s) {
    if (!g_raw_out) {
        std::cout << s;
        return;
    }
    for (char c : s) {
        if (c == '\n') std::cout << "\r\n";
        else std::cout << c;
    }
}

void outln(const std::string& s = {}) {
    out(s);
    out("\n");
}

void print_line(const std::string& s) {
    outln(s);
    std::cout << std::flush;
}

struct TermRaw {
    termios old{};
    bool ok{false};
    TermRaw() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &old) != 0) return;
        termios raw = old;
        cfmakeraw(&raw);
        raw.c_lflag |= ISIG; // keep Ctrl-C
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            ok = true;
            g_raw_out = true;
        }
    }
    ~TermRaw() {
        if (ok) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old);
            g_raw_out = false;
        }
    }
    void cooked() {
        if (!ok) return;
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        g_raw_out = false;
    }
    void raw_again() {
        if (!ok) return;
        termios again = old;
        cfmakeraw(&again);
        again.c_lflag |= ISIG;
        tcsetattr(STDIN_FILENO, TCSANOW, &again);
        g_raw_out = true;
    }
};

int term_rows() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return 24;
}

int term_cols() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 40;
}

std::string truncate(const std::string& s, int max_w) {
    if (max_w < 4 || static_cast<int>(s.size()) <= max_w) return s;
    return s.substr(0, static_cast<size_t>(max_w - 1)) + "…";
}

int run_entry(const runner::Entry& e, const std::vector<std::string>& args, bool yes) {
    if (e.reason) {
        print_line(e.reason);
        return 0;
    }
    if (e.kind == runner::Kind::Menu) {
        print_line(std::string(e.label) + " children:");
        for (const auto& c : e.children) print_line(std::string("  ") + c);
        return 0;
    }
    if (!e.run) {
        print_line("no runner");
        return 1;
    }
    size_t need = 0;
    for (const auto& a : e.args) {
        if (a.required) ++need;
    }
    if (args.size() < need) {
        print_line("missing args:");
        for (const auto& a : e.args) {
            print_line(std::string("  ") + a.name + (a.required ? " *" : "") + "  " + a.prompt);
        }
        return 2;
    }
    if (e.confirm && !yes) {
        print_line(std::string("need --yes for ") + e.key);
        return 3;
    }
    if (e.needs_exclusive && runner::graphical_app_busy()) {
        print_line("quit graphical kvxputerzero first");
        return 4;
    }
    g_cancel.store(false);
    return e.run(args, g_cancel, print_line);
}

int cmd_list(const std::vector<std::string>& args) {
    const char* ch = args.empty() ? nullptr : args[0].c_str();
    if (ch) {
        for (const auto* e : runner::entries_for_channel(ch)) {
            print_line(std::string(e->key) + "  " + e->label);
        }
        return 0;
    }
    for (const auto& chd : model::kChannels) {
        print_line(std::string(chd.id) + "  " + chd.label);
    }
    return 0;
}

int cmd_run(std::vector<std::string> args, bool yes) {
    if (args.size() < 2) {
        print_line("usage: run [--yes] <channel> <tool-key> [args…]");
        return 2;
    }
    std::vector<std::string> cleaned;
    for (auto& a : args) {
        if (a == "--yes") yes = true;
        else cleaned.push_back(std::move(a));
    }
    if (cleaned.size() < 2) {
        print_line("usage: run [--yes] <channel> <tool-key> [args…]");
        return 2;
    }
    const auto* e = runner::find_entry(cleaned[0].c_str(), cleaned[1].c_str());
    if (!e) {
        print_line("unknown: " + cleaned[0] + " " + cleaned[1]);
        return 1;
    }
    std::vector<std::string> rest(cleaned.begin() + 2, cleaned.end());
    return run_entry(*e, rest, yes);
}

int cmd_check() {
    auto missing = runner::check_tool_ids_classified();
    if (missing.empty()) {
        print_line("ok: all ToolIds classified");
        return 0;
    }
    for (const auto& m : missing) print_line(m);
    return 1;
}

char read_key() {
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == 0x1b) {
        unsigned char seq[2]{};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
            if (seq[1] == 'A') return 'k';
            if (seq[1] == 'B') return 'j';
        }
        return 27;
    }
    return static_cast<char>(c);
}

// Compact left-aligned list. Window scrolls so selection stays visible on small phones.
void draw_menu(const std::string& title, const std::vector<std::string>& items, int sel, int& window_top) {
    const int cols = term_cols();
    const int rows = term_rows();
    const int header = 3; // title + hint + blank
    const int footer = 1;
    int view = rows - header - footer;
    if (view < 5) view = 5;

    if (sel < window_top) window_top = sel;
    if (sel >= window_top + view) window_top = sel - view + 1;
    if (window_top < 0) window_top = 0;
    const int total = static_cast<int>(items.size());
    if (window_top > total - view && total > view) window_top = total - view;
    if (window_top < 0) window_top = 0;

    const int label_w = cols > 6 ? cols - 4 : 20; // "> " + label, leave a little margin

    out("\033[2J\033[H");
    outln(truncate(title, cols > 2 ? cols - 1 : 20));
    outln(truncate("j/k Enter  q back", cols > 2 ? cols - 1 : 20));
    outln();

    const int end = std::min(total, window_top + view);
    for (int i = window_top; i < end; ++i) {
        const bool on = (i == sel);
        std::string row = (on ? "> " : "  ") + truncate(items[static_cast<size_t>(i)], label_w);
        outln(row);
    }
    if (total > view) {
        outln(truncate("[" + std::to_string(sel + 1) + "/" + std::to_string(total) + "]", cols - 1));
    }
    std::cout << std::flush;
}

int pick_menu(TermRaw& raw, const std::string& title, const std::vector<std::string>& items, int& sel) {
    int top = 0;
    while (true) {
        draw_menu(title, items, sel, top);
        const char k = read_key();
        if (k == 'q' || k == 27) return -1;
        if (k == 'j') sel = (sel + 1) % static_cast<int>(items.size());
        if (k == 'k') sel = (sel - 1 + static_cast<int>(items.size())) % static_cast<int>(items.size());
        if (k == '\r' || k == '\n') return sel;
        (void)raw;
    }
}

int interactive() {
    TermRaw raw;

    // One entry per channel from the registry (no duplicates).
    std::vector<std::string> channel_ids;
    std::vector<std::string> channel_labels;
    for (const auto& ch : model::kChannels) {
        channel_ids.emplace_back(ch.id);
        channel_labels.emplace_back(ch.label);
    }

    const std::vector<std::pair<std::string, std::string>> tools_menu = {
        {"calculator", "Calculator"}, {"eng_calc", "Calculators"}, {"codec", "Codec / Hash"},
        {"passgen", "Passgen"},       {"totp", "TOTP"},           {"combo", "Combo"},
        {"qr", "QRCodes"},            {"barcode", "Barcode"},     {"pda", "PDA"},
        {"media_player", "Media Player"}, {"timer", "Timer"},     {"matrix", "Matrix"},
        {"dtmf", "DTMF"},             {"morse", "Morse"},         {"microphone", "Microphone"},
        {"mqtt", "MQTT"},             {"llm_chat", "LLM Chat"},   {"drone_id", "Drone ID"},
        {"ibutton", "iButton"},
    };

    int ch_sel = 0;
    while (true) {
        if (pick_menu(raw, "kvxputerzero", channel_labels, ch_sel) < 0) return 0;

        const std::string cid = channel_ids[static_cast<size_t>(ch_sel)];
        // Registry uses "others" for the Tools tile.
        const bool is_tools = (cid == "others" || cid == "tools");

        std::vector<std::string> items;
        std::vector<const runner::Entry*> entries;
        if (is_tools) {
            for (const auto& t : tools_menu) {
                items.push_back(t.second);
                entries.push_back(runner::find_entry("tools", t.first.c_str()));
            }
        } else if (cid == "discovery") {
            items.push_back("All tools");
            entries.push_back(runner::find_entry("discovery", "filter"));
        } else if (cid == "usb") {
            auto* e = runner::find_entry("usb", "usb");
            items.push_back(e ? e->label : "USB");
            entries.push_back(e);
        } else if (cid == "clock" || cid == "charge" || cid == "camera" || cid == "gallery") {
            auto* e = runner::find_entry(cid.c_str(), cid.c_str());
            items.push_back(e ? e->label : cid);
            entries.push_back(e);
        } else if (cid == "config") {
            for (const auto* e : runner::entries_for_channel("config")) {
                items.push_back(e->label);
                entries.push_back(e);
            }
        } else {
            for (const auto* e : runner::entries_for_channel(cid.c_str())) {
                if (cid == "wifi" && (std::strcmp(e->key, "ping") == 0 || std::strcmp(e->key, "dig") == 0 ||
                                      std::strcmp(e->key, "curl") == 0 || std::strcmp(e->key, "trace") == 0)) {
                    continue;
                }
                items.push_back(e->label);
                entries.push_back(e);
            }
        }

        if (items.empty()) {
            out("\033[2J\033[H");
            outln("(empty)");
            outln("q back");
            std::cout << std::flush;
            while (true) {
                const char k = read_key();
                if (k == 'q' || k == 27 || k == '\r' || k == '\n') break;
            }
            continue;
        }

        int tool_sel = 0;
        while (true) {
            const std::string title = channel_labels[static_cast<size_t>(ch_sel)];
            if (pick_menu(raw, title, items, tool_sel) < 0) break;

            const runner::Entry* e = entries[static_cast<size_t>(tool_sel)];
            if (!e) continue;

            if (e->kind == runner::Kind::Menu && !e->children.empty()) {
                std::vector<std::string> kids;
                std::vector<const runner::Entry*> kent;
                for (const auto& ck : e->children) {
                    const auto* ce = runner::find_entry(e->channel, ck.c_str());
                    if (!ce) ce = runner::find_entry(cid.c_str(), ck.c_str());
                    kids.push_back(ce ? ce->label : ck);
                    kent.push_back(ce);
                }
                int ks = 0;
                if (pick_menu(raw, e->label, kids, ks) < 0) continue;
                e = kent[static_cast<size_t>(ks)];
                if (!e) continue;
            }

            out("\033[2J\033[H");
            outln(truncate(e->label, term_cols() - 1));
            outln();
            std::cout << std::flush;

            std::vector<std::string> args;
            raw.cooked();
            for (const auto& a : e->args) {
                while (true) {
                    std::cout << a.name << (a.required ? "*:" : ":") << " " << std::flush;
                    std::string line;
                    if (!std::getline(std::cin, line)) break;
                    if (line.empty() && a.required) {
                        std::cout << "required\n";
                        continue;
                    }
                    if (!line.empty()) args.push_back(line);
                    break;
                }
            }
            bool yes = !e->confirm;
            if (e->confirm) {
                std::cout << "OK " << e->key << "? [y/N] " << std::flush;
                std::string ans;
                std::getline(std::cin, ans);
                yes = (ans == "y" || ans == "Y" || ans == "yes");
            }

            // Stay cooked while tool prints; re-enter raw after pause key.
            const int rc = run_entry(*e, args, yes);
            std::cout << "\n[" << rc << "] Enter\n" << std::flush;
            {
                std::string pause;
                std::getline(std::cin, pause);
            }
            raw.raw_again();
        }
    }
}

void usage() {
    print_line("kvxputerzero-cli — SSH/TTY front-end for kvxputerzero tools");
    print_line("  kvxputerzero-cli");
    print_line("  kvxputerzero-cli list [channel]");
    print_line("  kvxputerzero-cli run [--yes] <channel> <tool-key> [args…]");
    print_line("  kvxputerzero-cli --check");
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.empty()) {
        if (!isatty(STDIN_FILENO)) {
            usage();
            return 2;
        }
        return interactive();
    }
    if (args[0] == "-h" || args[0] == "--help") {
        usage();
        return 0;
    }
    if (args[0] == "--check" || args[0] == "check") return cmd_check();
    if (args[0] == "list") {
        args.erase(args.begin());
        return cmd_list(args);
    }
    if (args[0] == "run") {
        args.erase(args.begin());
        return cmd_run(std::move(args), false);
    }
    usage();
    return 2;
}
