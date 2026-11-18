#include "i2c_scan.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace platform {

std::vector<I2cHit> scan_all_i2c(std::string& error) {
    error.clear();
    std::vector<I2cHit> out;
    DIR* dir = opendir("/dev");
    if (!dir) {
        error = "cannot open /dev";
        return out;
    }
    while (dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "i2c-", 4) != 0) continue;
        int bus = -1;
        try {
            bus = std::stoi(ent->d_name + 4);
        } catch (...) {
            continue;
        }
        const std::string path = std::string("/dev/") + ent->d_name;
        const int fd = ::open(path.c_str(), O_RDWR);
        if (fd < 0) continue;
        for (int addr = 0x03; addr <= 0x77; ++addr) {
            if (ioctl(fd, I2C_SLAVE, addr) < 0) continue;
            // Quick probe: try a 0-length write / SMBus quick via read of 1 byte may NACK.
            char b = 0;
            if (::read(fd, &b, 1) >= 0 || errno == 0) {
                // Many drivers return -1 on NACK; success means device answered.
            }
            // Prefer I2C_SMBUS if available — fall back to presence via ioctl success alone is weak.
            // Use write of zero bytes through i2cdev: many kernels treat write(fd,"",0) oddly.
            // Probe with i2c_smbus_read_byte via ioctl when headers allow; else skip false positives.
            (void)b;
        }
        // Use i2cdetect-style: attempt I2C_SLAVE + write.
        for (int addr = 0x03; addr <= 0x77; ++addr) {
            if (ioctl(fd, I2C_SLAVE, addr) < 0) continue;
            const uint8_t z = 0;
            // A successful 1-byte write or read indicates ACK.
            if (::write(fd, &z, 0) == 0) {
                // length 0 write often succeeds without device — skip
            }
            uint8_t byte = 0;
            const ssize_t n = ::read(fd, &byte, 1);
            if (n == 1) {
                I2cHit hit;
                hit.bus = bus;
                hit.address = static_cast<uint8_t>(addr);
                out.push_back(hit);
            }
        }
        ::close(fd);
    }
    closedir(dir);
    if (out.empty()) {
        // Soft error: buses may exist but no ACKs, or no permission.
        error.clear();
    }
    return out;
}

} // namespace platform
