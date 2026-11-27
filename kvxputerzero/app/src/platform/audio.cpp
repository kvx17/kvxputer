#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace platform {
namespace {

std::string run_cmd(const std::string& cmd) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "popen failed";
    char buf[256];
    std::string out;
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    const int rc = pclose(fp);
    if (rc != 0 && out.empty()) out = "command failed";
    return out;
}

} // namespace

bool play_tone(float freq_hz, int duration_ms, std::string& error) {
    error.clear();
    // Generate a short raw PCM and pipe to aplay when available.
    const int rate = 22050;
    const int n = rate * duration_ms / 1000;
    std::vector<int16_t> samples(static_cast<size_t>(n > 0 ? n : 1));
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / rate;
        samples[static_cast<size_t>(i)] =
            static_cast<int16_t>(16000.0f * std::sin(2.0f * 3.14159265f * freq_hz * t));
    }
    const std::string path = "/tmp/kvxputerzero_tone.raw";
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            error = "cannot write tone";
            return false;
        }
        out.write(reinterpret_cast<const char*>(samples.data()),
                  static_cast<std::streamsize>(samples.size() * sizeof(int16_t)));
    }
    const std::string msg =
        run_cmd("aplay -q -t raw -f S16_LE -r " + std::to_string(rate) + " -c 1 " + path + " 2>&1");
    if (!msg.empty()) {
        error = msg.find("not found") != std::string::npos ? "audio playback unavailable (aplay)" : msg;
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
        const auto nl = error.find('\n');
        if (nl != std::string::npos) error.resize(nl);
        return false;
    }
    return true;
}

bool play_dual_tone(float f1, float f2, int duration_ms, std::string& error) {
    error.clear();
    const int rate = 22050;
    const int n = rate * duration_ms / 1000;
    std::vector<int16_t> samples(static_cast<size_t>(n > 0 ? n : 1));
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / rate;
        const float s =
            0.5f * (std::sin(2.0f * 3.14159265f * f1 * t) + std::sin(2.0f * 3.14159265f * f2 * t));
        samples[static_cast<size_t>(i)] = static_cast<int16_t>(16000.0f * s);
    }
    const std::string path = "/tmp/kvxputerzero_dual.raw";
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            error = "cannot write tone";
            return false;
        }
        out.write(reinterpret_cast<const char*>(samples.data()),
                  static_cast<std::streamsize>(samples.size() * sizeof(int16_t)));
    }
    const std::string msg =
        run_cmd("aplay -q -t raw -f S16_LE -r " + std::to_string(rate) + " -c 1 " + path + " 2>&1");
    if (!msg.empty()) {
        error = msg.find("not found") != std::string::npos ? "audio playback unavailable (aplay)" : msg;
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
        const auto nl = error.find('\n');
        if (nl != std::string::npos) error.resize(nl);
        return false;
    }
    return true;
}

bool beep(std::string& error) {
    return play_tone(880.0f, 80, error);
}

enum class MixerKind { Unknown, None, Wpctl, Pactl, Amixer };

bool have_cmd(const char* name) {
    const std::string out = run_cmd(std::string("command -v ") + name + " 2>/dev/null");
    return out.find('/') != std::string::npos;
}

bool command_ok(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    return rc == 0;
}

bool parse_wpctl(const std::string& text, OutputLevel& level) {
    const auto pos = text.find("Volume:");
    if (pos == std::string::npos) return false;
    const char* p = text.c_str() + pos + 7;
    while (*p == ' ' || *p == '\t') ++p;
    char* end = nullptr;
    const float value = std::strtof(p, &end);
    if (end == p) return false;
    level.percent = std::clamp(static_cast<int>(std::lround(value * 100.0)), 0, 100);
    level.muted = text.find("[MUTED]") != std::string::npos;
    return true;
}

bool parse_percent_token(const std::string& text, OutputLevel& level) {
    const auto mark = text.find('%');
    if (mark == std::string::npos || mark == 0) return false;
    size_t start = mark;
    while (start > 0 && text[start - 1] >= '0' && text[start - 1] <= '9') --start;
    if (start == mark) return false;
    level.percent = std::clamp(std::atoi(text.c_str() + start), 0, 100);
    level.muted = text.find("[off]") != std::string::npos || text.find("Mute: yes") != std::string::npos;
    return true;
}

std::string volume_step(int delta_percent) {
    const int mag = std::abs(delta_percent);
    return std::to_string(mag) + (delta_percent >= 0 ? "%+" : "%-");
}

bool read_wpctl(OutputLevel& level) {
    return parse_wpctl(run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null"), level);
}

bool read_pactl(OutputLevel& level) {
    const std::string volume = run_cmd("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null");
    const std::string mute = run_cmd("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null");
    if (!parse_percent_token(volume, level)) return false;
    level.muted = mute.find("yes") != std::string::npos;
    return true;
}

bool read_amixer(OutputLevel& level) {
    const std::string text = run_cmd("amixer sget Master 2>/dev/null");
    if (parse_percent_token(text, level)) return true;
    return parse_percent_token(run_cmd("amixer sget Speaker 2>/dev/null"), level);
}

MixerKind detect_mixer() {
    static MixerKind cached = MixerKind::Unknown;
    if (cached != MixerKind::Unknown) return cached;
    if (have_cmd("wpctl")) cached = MixerKind::Wpctl;
    else if (have_cmd("pactl")) cached = MixerKind::Pactl;
    else if (have_cmd("amixer")) cached = MixerKind::Amixer;
    else cached = MixerKind::None;
    return cached;
}

bool mixer_read(MixerKind kind, OutputLevel& level) {
    switch (kind) {
        case MixerKind::Wpctl: return read_wpctl(level);
        case MixerKind::Pactl: return read_pactl(level);
        case MixerKind::Amixer: return read_amixer(level);
        default: return false;
    }
}

bool mixer_write_volume(MixerKind kind, int delta_percent) {
    const std::string step = volume_step(delta_percent);
    switch (kind) {
        case MixerKind::Wpctl:
            return command_ok("wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ '" + step +
                              "' >/dev/null 2>&1");
        case MixerKind::Pactl:
            return command_ok(std::string("pactl set-sink-volume @DEFAULT_SINK@ '") +
                              (delta_percent >= 0 ? "+" : "-") + std::to_string(std::abs(delta_percent)) +
                              "%' >/dev/null 2>&1");
        case MixerKind::Amixer:
            if (command_ok("amixer -q sset Master '" + step + "' >/dev/null 2>&1")) return true;
            return command_ok("amixer -q sset Speaker '" + step + "' >/dev/null 2>&1");
        default:
            return false;
    }
}

bool mixer_toggle_mute(MixerKind kind) {
    switch (kind) {
        case MixerKind::Wpctl:
            return command_ok("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle >/dev/null 2>&1");
        case MixerKind::Pactl:
            return command_ok("pactl set-sink-mute @DEFAULT_SINK@ toggle >/dev/null 2>&1");
        case MixerKind::Amixer:
            if (command_ok("amixer -q sset Master toggle >/dev/null 2>&1")) return true;
            return command_ok("amixer -q sset Speaker toggle >/dev/null 2>&1");
        default:
            return false;
    }
}

bool with_mixer(bool toggle, int delta_percent, OutputLevel& level) {
    if (detect_mixer() == MixerKind::None) return false;
    const MixerKind order[] = {detect_mixer(), MixerKind::Wpctl, MixerKind::Pactl, MixerKind::Amixer};
    MixerKind tried = MixerKind::Unknown;
    for (MixerKind kind : order) {
        if (kind == MixerKind::Unknown || kind == MixerKind::None || kind == tried) continue;
        tried = kind;
        const bool wrote = toggle ? mixer_toggle_mute(kind) : mixer_write_volume(kind, delta_percent);
        if (!wrote) continue;
        if (!mixer_read(kind, level)) level.percent = -1;
        return true;
    }
    return false;
}

bool adjust_output_volume(int delta_percent, OutputLevel& level) {
    if (delta_percent == 0) return false;
    return with_mixer(false, delta_percent, level);
}

bool toggle_output_mute(OutputLevel& level) {
    return with_mixer(true, 0, level);
}

bool media_transport(const char* action, std::string& error) {
    error.clear();
    if (!action || (std::string(action) != "play-pause" && std::string(action) != "next" &&
                    std::string(action) != "previous")) {
        error = "unsupported media key";
        return false;
    }
    if (!have_cmd("playerctl")) {
        error = "playerctl not installed";
        return false;
    }
    if (!command_ok(std::string("playerctl ") + action + " >/dev/null 2>&1")) {
        error = "no media player";
        return false;
    }
    return true;
}

bool capture_pcm(std::vector<int16_t>& samples, int sample_rate, int duration_ms, std::string& error,
                 bool* cancel) {
    samples.clear();
    error.clear();
    if (cancel && *cancel) {
        error = "cancelled";
        return false;
    }
    const std::string path = "/tmp/kvxputerzero_cap.raw";
    const std::string cmd = "arecord -q -t raw -f S16_LE -r " + std::to_string(sample_rate) +
                            " -c 1 -d " + std::to_string((duration_ms + 999) / 1000) + " " + path + " 2>&1";
    const std::string msg = run_cmd(cmd);
    if (!msg.empty() && msg.find("not found") != std::string::npos) {
        error = "audio capture unavailable (arecord)";
        return false;
    }
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "capture failed";
        return false;
    }
    const auto sz = in.tellg();
    in.seekg(0);
    samples.resize(static_cast<size_t>(sz) / sizeof(int16_t));
    in.read(reinterpret_cast<char*>(samples.data()), sz);
    return !samples.empty();
}

bool write_wav(const std::string& path, const std::vector<int16_t>& samples, int sample_rate,
               std::string& error) {
    error.clear();
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot write wav";
        return false;
    }
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t chunk_size = 36 + data_bytes;
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunk_size), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_fmt = 1, channels = 1, bits = 16;
    uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * bits / 8);
    uint16_t block_align = static_cast<uint16_t>(channels * bits / 8);
    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    out.write(reinterpret_cast<const char*>(&audio_fmt), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_bytes), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
    return true;
}

bool play_wav(const std::string& path, std::string& error) {
    error.clear();
    const std::string msg = run_cmd("aplay -q \"" + path + "\" 2>&1");
    if (!msg.empty() && msg.find("not found") != std::string::npos) {
        error = "audio playback unavailable (aplay)";
        return false;
    }
    return true;
}

} // namespace platform
