#include "audio.h"

#include <cmath>
#include <cstdio>
#include <fstream>
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
    if (!msg.empty() && msg.find("not found") != std::string::npos) {
        error = "audio playback unavailable (aplay)";
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
    if (!msg.empty() && msg.find("not found") != std::string::npos) {
        error = "audio playback unavailable (aplay)";
        return false;
    }
    return true;
}

bool beep(std::string& error) {
    return play_tone(880.0f, 80, error);
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
