#pragma once

#include <string>
#include <vector>

namespace platform {

struct Si4713Session {
    bool open(int i2c_bus, std::string& error);
    void close();
    bool ok() const;

    bool status_lines(std::vector<std::string>& lines, std::string& error);
    // Tune TX frequency in 10 kHz units (e.g. 8810 = 88.10 MHz). Best-effort power-up.
    bool broadcast_start(int freq_10khz, std::string& error);
    bool broadcast_stop(std::string& error);
    bool spectrum_sample(std::vector<std::string>& lines, std::string& error);

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace platform
