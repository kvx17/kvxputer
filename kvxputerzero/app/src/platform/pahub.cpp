#include "pahub.h"

#include <cstdint>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

bool probe_addr(int fd, uint8_t addr) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) return false;
    uint8_t b = 0;
    return ::read(fd, &b, 1) == 1;
}

} // namespace

bool pahub_scan(int i2c_bus, uint8_t hub_addr, std::vector<PahubChannelHit>& hits, std::string& error) {
    hits.clear();
    if (kDesktop) {
        error = "I2C unavailable";
        return false;
    }
    const std::string path = "/dev/i2c-" + std::to_string(i2c_bus);
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "cannot open " + path;
        return false;
    }
    if (ioctl(fd, I2C_SLAVE, hub_addr) < 0) {
        ::close(fd);
        error = "PaHub not present";
        return false;
    }
    for (uint8_t ch = 0; ch < 8; ++ch) {
        const uint8_t mask = static_cast<uint8_t>(1u << ch);
        if (::write(fd, &mask, 1) != 1) continue;
        PahubChannelHit hit;
        hit.channel = ch;
        for (int addr = 0x08; addr <= 0x77; ++addr) {
            if (static_cast<uint8_t>(addr) == hub_addr) continue;
            if (probe_addr(fd, static_cast<uint8_t>(addr))) hit.addresses.push_back(static_cast<uint8_t>(addr));
        }
        // Reselect hub for next channel write
        ioctl(fd, I2C_SLAVE, hub_addr);
        hits.push_back(std::move(hit));
    }
    // Disable all channels
    ioctl(fd, I2C_SLAVE, hub_addr);
    const uint8_t zero = 0;
    (void)::write(fd, &zero, 1);
    ::close(fd);
    return true;
}

} // namespace platform
