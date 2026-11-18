#include "spi_device.h"

#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

} // namespace

std::vector<std::string> list_spidev() {
    std::vector<std::string> out;
    if (kDesktop) return out;
    DIR* dir = opendir("/dev");
    if (!dir) return out;
    while (dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "spidev", 6) != 0) continue;
        out.push_back(std::string("/dev/") + ent->d_name);
    }
    closedir(dir);
    return out;
}

std::unique_ptr<SpiDevice> SpiDevice::open(const SpiConfig& cfg, std::string& error) {
    error.clear();
    if (kDesktop) {
        error = "SPI unavailable";
        return nullptr;
    }
    if (cfg.path.empty()) {
        error = "SPI path empty";
        return nullptr;
    }
    const int fd = ::open(cfg.path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "SPI open failed: " + cfg.path;
        return nullptr;
    }
    uint8_t mode = cfg.mode;
    if (cfg.no_chip_select) mode = static_cast<uint8_t>(mode | SPI_NO_CS);
    uint8_t bits = cfg.bits_per_word;
    uint32_t speed = cfg.speed_hz;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        error = "SPI configure failed";
        ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<SpiDevice>(new SpiDevice(fd, cfg.path));
}

SpiDevice::SpiDevice(int fd, std::string path) : fd_(fd), path_(std::move(path)) {}

SpiDevice::~SpiDevice() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SpiDevice::transfer(const uint8_t* tx, uint8_t* rx, size_t len, std::string& error) {
    error.clear();
    if (fd_ < 0 || !tx || !rx || len == 0) {
        error = "SPI bad args";
        return false;
    }
    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = static_cast<__u32>(len);
    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) {
        error = "SPI transfer failed";
        return false;
    }
    return true;
}

bool SpiDevice::transfer(const std::vector<uint8_t>& tx, std::vector<uint8_t>& rx, std::string& error) {
    rx.assign(tx.size(), 0);
    return transfer(tx.data(), rx.data(), tx.size(), error);
}

bool SpiDevice::write_then_read(const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len,
                                std::string& error) {
    std::vector<uint8_t> buf(tx_len + rx_len, 0);
    if (tx && tx_len) std::memcpy(buf.data(), tx, tx_len);
    std::vector<uint8_t> rxbuf(buf.size(), 0);
    if (!transfer(buf.data(), rxbuf.data(), buf.size(), error)) return false;
    if (rx && rx_len) std::memcpy(rx, rxbuf.data() + tx_len, rx_len);
    return true;
}

} // namespace platform
