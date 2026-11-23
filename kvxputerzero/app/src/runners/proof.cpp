#include "runner.h"
#include "util.h"

#include "codec_lib.h"
#include "fs_root.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace runner {
namespace {

namespace fs = std::filesystem;

int run_codec(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    if (args.size() < 2) {
        return emit_error(line, "need: <op> <text>  ops: b64e b64d hexe hexd urle urld md5 sha1 sha256");
    }
    const std::string& opname = args[0];
    std::string text;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) text.push_back(' ');
        text += args[i];
    }
    tools::codec::Op op = tools::codec::Op::B64Encode;
    if (opname == "b64e" || opname == "b64_encode") op = tools::codec::Op::B64Encode;
    else if (opname == "b64d" || opname == "b64_decode") op = tools::codec::Op::B64Decode;
    else if (opname == "hexe" || opname == "hex_encode") op = tools::codec::Op::HexEncode;
    else if (opname == "hexd" || opname == "hex_decode") op = tools::codec::Op::HexDecode;
    else if (opname == "urle" || opname == "url_encode") op = tools::codec::Op::UrlEncode;
    else if (opname == "urld" || opname == "url_decode") op = tools::codec::Op::UrlDecode;
    else if (opname == "md5") op = tools::codec::Op::Md5;
    else if (opname == "sha1") op = tools::codec::Op::Sha1;
    else if (opname == "sha256") op = tools::codec::Op::Sha256;
    else return emit_error(line, "unknown op: " + opname);

    bool ok = false;
    const std::string out = tools::codec::run(op, text, ok);
    if (line) line(out);
    return ok ? 0 : 1;
}

int run_files_list(const std::vector<std::string>& args, std::atomic<bool>& cancel, const LineFn& line) {
    fs::path root = fs::path(getenv("HOME") ? getenv("HOME") : ".");
    if (!args.empty() && !args[0].empty()) {
        std::string err;
        const std::string resolved = platform::resolve_under_root(root.string(), args[0], err);
        if (resolved.empty()) return emit_error(line, err.empty() ? "bad path" : err);
        root = resolved;
    }
    std::error_code ec;
    if (!fs::exists(root, ec)) return emit_error(line, "missing: " + root.string());
    if (fs::is_regular_file(root, ec)) {
        return emit_ok(line, "file " + root.string() + " (" + std::to_string(fs::file_size(root, ec)) + " bytes)");
    }
    if (line) line(root.string());
    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (cancelled(cancel)) return 130;
        const auto& p = ent.path();
        std::string mark = ent.is_directory(ec) ? "/" : "";
        std::ostringstream oss;
        oss << "  " << p.filename().string() << mark;
        if (ent.is_regular_file(ec)) oss << "  " << fs::file_size(p, ec);
        if (line) line(oss.str());
    }
    return 0;
}

int run_discovery(const std::vector<std::string>& args, std::atomic<bool>&, const LineFn& line) {
    const std::string filter = args.empty() ? "" : args[0];
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string f = lower(filter);
    int n = 0;
    for (const auto* d : model::all_unique_tools()) {
        if (!f.empty()) {
            std::string lab = lower(d->label);
            std::string key = lower(d->key);
            if (lab.find(f) == std::string::npos && key.find(f) == std::string::npos) continue;
        }
        if (line) line(std::string(d->channel_id) + "  " + d->key + "  " + d->label);
        ++n;
    }
    if (n == 0) return emit_ok(line, "(no matches)");
    return 0;
}

} // namespace

RunFn proof_codec() { return run_codec; }
RunFn proof_files_list() { return run_files_list; }
RunFn proof_discovery() { return run_discovery; }

} // namespace runner
