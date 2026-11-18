#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct SpiConfig {
    std::string path; // e.g. /dev/spidev0.1
    uint32_t speed_hz{4000000};
    uint8_t mode{0}; // SPI_MODE_0 / SPI_MODE_1 …
    uint8_t bits_per_word{8};
    bool no_chip_select{false}; // SPI_NO_CS — use with userspace soft-CS (e.g. Cap NFC GPIO22)
};

class SpiDevice {
public:
    static std::unique_ptr<SpiDevice> open(const SpiConfig& cfg, std::string& error);
    ~SpiDevice();

    SpiDevice(const SpiDevice&) = delete;
    SpiDevice& operator=(const SpiDevice&) = delete;

    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len, std::string& error);
    bool transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error);
    bool write_then_read(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, std::string& error);

    const std::string& path() const { return path_; }
    bool ok() const { return fd_ >= 0; }

private:
    explicit SpiDevice(int fd, std::string path);
    int fd_{-1};
    std::string path_;
};

std::vector<std::string> list_spidev();

} // namespace platform
