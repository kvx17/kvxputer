#include "runners_impl.h"
#include "util.h"

#include "audio.h"
#include "barcode_lib.h"
#include "calc_lib.h"
#include "combo_lib.h"
#include "eng_calc_lib.h"
#include "passgen_lib.h"
#include "pda_lib.h"
#include "qr_lib.h"
#include "totp_lib.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <thread>

namespace runner {
namespace {

int run_calculator(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <expression>");
    std::string expr;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) expr.push_back(' ');
        expr += args[i];
    }
    tools::calc::State st;
    tools::calc::load_prefs(st);
    double result = 0;
    std::string err;
    if (!tools::calc::evaluate(expr, st, result, err)) return emit_error(line, err);
    tools::calc::save_prefs(st);
    return emit_ok(line, tools::calc::format_result(result));
}

tools::engcalc::Op parse_eng_op(const std::string& name, bool& ok) {
    ok = true;
    for (int i = 0; i < static_cast<int>(tools::engcalc::Op::Count); ++i) {
        auto op = static_cast<tools::engcalc::Op>(i);
        std::string lab = tools::engcalc::op_label(op);
        for (char& c : lab) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string n = name;
        for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lab == n || std::to_string(i) == name) return op;
    }
    ok = false;
    return tools::engcalc::Op::Subnet;
}

int run_eng_calc(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) {
        for (int i = 0; i < static_cast<int>(tools::engcalc::Op::Count); ++i) {
            auto op = static_cast<tools::engcalc::Op>(i);
            if (line) line(std::to_string(i) + "  " + tools::engcalc::op_label(op));
        }
        return emit_ok(line, "usage: eng_calc <op> <input…>");
    }
    bool ok = false;
    auto op = parse_eng_op(args[0], ok);
    if (!ok) return emit_error(line, "unknown eng calc op");
    std::string input;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) input.push_back(' ');
        input += args[i];
    }
    if (input.empty()) return emit_error(line, tools::engcalc::input_hint(op));
    bool rok = false;
    const std::string out = tools::engcalc::run(op, input, rok);
    if (line) {
        std::string cur;
        for (char c : out) {
            if (c == '\n') {
                line(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) line(cur);
    }
    return rok ? 0 : 1;
}

int run_passgen(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    tools::passgen::Options opt;
    bool save = false;
    for (const auto& a : args) {
        if (a == "save") save = true;
        else if (a.rfind("len=", 0) == 0) opt.length = std::atoi(a.c_str() + 4);
        else if (a.rfind("count=", 0) == 0) opt.count = std::atoi(a.c_str() + 6);
        else if (a == "symbols") opt.symbols = true;
    }
    if (opt.length < 4) opt.length = 4;
    if (opt.count < 1) opt.count = 1;
    auto pw = tools::passgen::generate(opt);
    for (const auto& p : pw) {
        if (line) line(p);
    }
    if (save) {
        std::string path, err;
        if (!tools::passgen::append_to_file(pw, path, err)) return emit_error(line, err);
        if (line) line("saved: " + path);
    }
    return 0;
}

int run_totp(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    const std::string cmd = args.empty() ? "list" : args[0];
    std::string err;
    std::vector<tools::totp::Account> accounts;
    if (!tools::totp::load_accounts(accounts, err) && cmd != "add") {
        if (!err.empty()) return emit_error(line, err);
    }
    if (cmd == "list" || cmd == "show") {
        const time_t now = time(nullptr);
        for (const auto& a : accounts) {
            std::string code;
            int rem = 0;
            if (!tools::totp::code_at(a, now, code, rem, err)) {
                if (line) line(a.name + "  " + err);
            } else if (line) {
                line(a.name + "  " + code + "  (" + std::to_string(rem) + "s)");
            }
        }
        if (accounts.empty()) return emit_ok(line, "(no accounts)");
        return 0;
    }
    if (cmd == "add") {
        if (args.size() < 3) return emit_error(line, "need: add <name> <base32secret>");
        tools::totp::Account a{args[1], args[2]};
        std::vector<uint8_t> key;
        if (!tools::totp::base32_decode(a.secret_b32, key)) return emit_error(line, "bad base32 secret");
        accounts.push_back(a);
        if (!tools::totp::save_accounts(accounts, err)) return emit_error(line, err);
        return emit_ok(line, "added " + a.name);
    }
    if (cmd == "del") {
        if (args.size() < 2) return emit_error(line, "need: del <name>");
        std::vector<tools::totp::Account> kept;
        for (auto& a : accounts) {
            if (a.name != args[1]) kept.push_back(std::move(a));
        }
        if (!tools::totp::save_accounts(kept, err)) return emit_error(line, err);
        return emit_ok(line, "deleted " + args[1]);
    }
    return emit_error(line, "usage: totp [list|show|add name secret|del name]");
}

int run_combo(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, tools::combo::input_hint());
    std::string input;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) input.push_back(' ');
        input += args[i];
    }
    bool ok = false;
    const std::string out = tools::combo::solve_line(input, ok);
    if (line) line(out);
    return ok ? 0 : 1;
}

int run_qr(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <text>");
    std::string text;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) text.push_back(' ');
        text += args[i];
    }
    tools::qr::Matrix m;
    std::string err;
    if (!tools::qr::encode_text(text, m, err)) return emit_error(line, err);
    if (line) line(text);
    for (int y = 0; y < m.size; ++y) {
        std::string row;
        for (int x = 0; x < m.size; ++x) {
            row += m.modules[static_cast<size_t>(y * m.size + x)] ? "##" : "  ";
        }
        if (line) line(row);
    }
    return 0;
}

int run_barcode(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: [type] <data>  types: code128 code39 codabar ean13 ean8 upca");
    tools::barcode::BarcodeType type = tools::barcode::BarcodeType::Code128;
    size_t di = 0;
    if (args[0] == "code128") {
        type = tools::barcode::BarcodeType::Code128;
        di = 1;
    } else if (args[0] == "code39") {
        type = tools::barcode::BarcodeType::Code39;
        di = 1;
    } else if (args[0] == "codabar") {
        type = tools::barcode::BarcodeType::Codabar;
        di = 1;
    } else if (args[0] == "ean13") {
        type = tools::barcode::BarcodeType::Ean13;
        di = 1;
    } else if (args[0] == "ean8") {
        type = tools::barcode::BarcodeType::Ean8;
        di = 1;
    } else if (args[0] == "upca") {
        type = tools::barcode::BarcodeType::UpcA;
        di = 1;
    }
    if (di >= args.size()) return emit_error(line, "need data");
    std::string data;
    for (size_t i = di; i < args.size(); ++i) {
        if (i > di) data.push_back(' ');
        data += args[i];
    }
    std::string bits, label, err;
    if (!tools::barcode::encode(type, data, bits, label, err)) return emit_error(line, err);
    if (line) {
        line(std::string(tools::barcode::type_label(type)) + "  " + label);
        line(bits);
    }
    return 0;
}

int run_pda(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    const std::string cmd = args.empty() ? "list" : args[0];
    if (cmd == "list") {
        for (int i = 0; i < static_cast<int>(tools::pda::Channel::Count); ++i) {
            auto ch = static_cast<tools::pda::Channel>(i);
            if (line) line(std::to_string(i) + "  " + tools::pda::channel_label(ch));
        }
        return 0;
    }
    if (cmd == "notes") {
        std::vector<tools::pda::NoteRef> notes;
        std::string err;
        tools::pda::list_notes(notes, err);
        for (const auto& n : notes) {
            if (line) line(n.title + "  " + n.path);
        }
        if (notes.empty()) return emit_ok(line, "(no notes)");
        return 0;
    }
    if (cmd == "todo") {
        std::vector<tools::pda::Task> tasks;
        std::string err;
        tools::pda::load_tasks(tasks, err);
        for (const auto& t : tasks) {
            if (line) line(std::string(t.done ? "[x] " : "[ ] ") + t.text);
        }
        return 0;
    }
    if (cmd == "memos") {
        std::vector<std::string> lines;
        std::string err;
        tools::pda::load_memos(lines, err);
        if (lines.empty()) return emit_ok(line, "(empty)");
        return emit_lines(line, lines);
    }
    return emit_error(line, "usage: pda [list|notes|todo|memos]");
}

int run_timer(const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <seconds>");
    int sec = std::atoi(args[0].c_str());
    if (sec < 1) sec = 1;
    for (int left = sec; left >= 0; --left) {
        if (cancelled(cancel)) return 130;
        if (line) line(std::to_string(left) + "s");
        if (left == 0) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return emit_ok(line, "done");
}

int run_media(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <wav-path>");
    std::string err;
    if (!platform::play_wav(args[0], err)) return emit_error(line, err.empty() ? "play failed" : err);
    return emit_ok(line, "played " + args[0]);
}

int run_dtmf(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <digits>  e.g. 123#");
    std::string err;
    for (char c : args[0]) {
        float f1 = 0, f2 = 0;
        // Classic DTMF pairs (simplified)
        switch (c) {
        case '1':
            f1 = 697;
            f2 = 1209;
            break;
        case '2':
            f1 = 697;
            f2 = 1336;
            break;
        case '3':
            f1 = 697;
            f2 = 1477;
            break;
        case '4':
            f1 = 770;
            f2 = 1209;
            break;
        case '5':
            f1 = 770;
            f2 = 1336;
            break;
        case '6':
            f1 = 770;
            f2 = 1477;
            break;
        case '7':
            f1 = 852;
            f2 = 1209;
            break;
        case '8':
            f1 = 852;
            f2 = 1336;
            break;
        case '9':
            f1 = 852;
            f2 = 1477;
            break;
        case '0':
            f1 = 941;
            f2 = 1336;
            break;
        case '*':
            f1 = 941;
            f2 = 1209;
            break;
        case '#':
            f1 = 941;
            f2 = 1477;
            break;
        case 'A':
        case 'a':
            f1 = 697;
            f2 = 1633;
            break;
        case 'B':
        case 'b':
            f1 = 770;
            f2 = 1633;
            break;
        case 'C':
        case 'c':
            f1 = 852;
            f2 = 1633;
            break;
        case 'D':
        case 'd':
            f1 = 941;
            f2 = 1633;
            break;
        default:
            continue;
        }
        if (!platform::play_dual_tone(f1, f2, 120, err)) return emit_error(line, err);
        if (line) line(std::string(1, c));
    }
    return 0;
}

int run_morse(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <text>");
    std::string text;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) text.push_back(' ');
        text += args[i];
    }
    if (line) line("morse: " + text);
    std::string err;
    for (char c : text) {
        if (c == ' ') {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }
        if (!platform::play_tone(600, 80, err)) return emit_error(line, err);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    return 0;
}

int run_microphone(const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
    int ms = 1000;
    if (!args.empty()) ms = std::atoi(args[0].c_str());
    if (ms < 100) ms = 100;
    std::vector<int16_t> samples;
    std::string err;
    bool c = false;
    if (!platform::capture_pcm(samples, 16000, ms, err, &c)) {
        if (cancelled(cancel)) return 130;
        return emit_error(line, err.empty() ? "capture failed" : err);
    }
    if (line) line("captured " + std::to_string(samples.size()) + " samples");
    const std::string path = ensure_config_subdir("mic") + "/cli.wav";
    if (!platform::write_wav(path, samples, 16000, err)) return emit_error(line, err);
    return emit_ok(line, "saved " + path);
}

int run_mqtt(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.size() < 2) return emit_error(line, "need: <host> <topic> [message]");
    if (line) {
        line("mqtt host=" + args[0] + " topic=" + args[1]);
        if (args.size() >= 3) line("publish not implemented in CLI stub — use GUI MQTT tool or mosquitto_pub");
        else line("subscribe not implemented in CLI stub — use GUI MQTT tool or mosquitto_sub");
    }
    return 0;
}

int run_llm(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.empty()) return emit_error(line, "need: <prompt>");
    std::string prompt;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) prompt.push_back(' ');
        prompt += args[i];
    }
    return emit_ok(line, "LLM Chat needs a configured UART/network endpoint (same as GUI). Prompt: " + prompt);
}

} // namespace

RunFn tools_calculator() { return run_calculator; }
RunFn tools_eng_calc() { return run_eng_calc; }
RunFn tools_passgen() { return run_passgen; }
RunFn tools_totp() { return run_totp; }
RunFn tools_combo() { return run_combo; }
RunFn tools_qr() { return run_qr; }
RunFn tools_barcode() { return run_barcode; }
RunFn tools_pda() { return run_pda; }
RunFn tools_timer() { return run_timer; }
RunFn tools_media_player() { return run_media; }
RunFn tools_dtmf() { return run_dtmf; }
RunFn tools_morse() { return run_morse; }
RunFn tools_microphone() { return run_microphone; }
RunFn tools_mqtt() { return run_mqtt; }
RunFn tools_llm_chat() { return run_llm; }
RunFn tools_info(const char* text) {
    std::string t = text ? text : "";
    return [t](const std::vector<std::string>&, std::atomic<bool>&, const LineFn& line) {
        return emit_ok(line, t);
    };
}

} // namespace runner
