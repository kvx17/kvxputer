#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace platform {

// Tone and capture helpers. Prefer miniaudio when linked; otherwise clear error / ALSA aplay/arecord.
bool play_tone(float freq_hz, int duration_ms, std::string& error);
bool play_dual_tone(float f1, float f2, int duration_ms, std::string& error);
bool beep(std::string& error);

// Capture mono PCM 16-bit at sample_rate for duration_ms into samples.
bool capture_pcm(std::vector<int16_t>& samples, int sample_rate, int duration_ms, std::string& error,
                 bool* cancel = nullptr);

bool write_wav(const std::string& path, const std::vector<int16_t>& samples, int sample_rate,
               std::string& error);
bool play_wav(const std::string& path, std::string& error);

} // namespace platform
