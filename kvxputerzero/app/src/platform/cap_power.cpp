#include "cap_power.h"

#include "gpio_line.h"
#include "spi_device.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace platform {
namespace {

#if defined(USE_DESKTOP) && USE_DESKTOP
constexpr bool kDesktop = true;
#else
constexpr bool kDesktop = false;
#endif

constexpr char kExt5vLed[] = "ext_5v_out";
constexpr char kUsbGpioFunLed[] = "ext_usb_gpio_fun";
constexpr unsigned kGpioPowerEn = 26; // Cap POWER_EN / LoRa RST
constexpr unsigned kGpioBusy = 22;
constexpr unsigned kGpioIrq = 23;
constexpr unsigned kGpioRfSw0 = 14; // CC1101 RF_SW0
constexpr unsigned kGpioGdo0 = 15;  // CC1101 GDO0
constexpr int kPi4ioeBus = 1;
constexpr uint8_t kPi4ioeAddr = 0x43;
constexpr uint8_t kPi4ioeDir = 0x03;
constexpr uint8_t kPi4ioeOut = 0x05;
constexpr uint8_t kPi4ioeHiZ = 0x07;

struct CapHeld {
    bool ext5v_owned{false};
    int prev_ext5v{0};
    bool gpio26_held{false};
    bool rf_sw_held{false};
    bool busy_held{false};
    bool irq_held{false};
    GpioLine power26;
    GpioLine rf_sw0;
    GpioLine gdo0;
    GpioLine busy;
    GpioLine irq;
};

CapHeld g_held;

std::string led_brightness_path(const char* name) {
    return std::string("/sys/class/leds/") + name + "/brightness";
}

bool led_exists(const char* name) {
    return ::access(led_brightness_path(name).c_str(), F_OK) == 0;
}

int read_led_brightness(const char* name) {
    std::ifstream in(led_brightness_path(name));
    int v = 0;
    if (in) in >> v;
    return v;
}

bool write_led_brightness(const char* name, int value, std::string& error) {
    const std::string path = led_brightness_path(name);
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + path + " failed (need gpio group write?)";
        return false;
    }
    const char c = value > 0 ? '1' : '0';
    const ssize_t n = ::write(fd, &c, 1);
    ::close(fd);
    if (n != 1) {
        error = "write " + path + " failed";
        return false;
    }
    return true;
}

bool cancelled(std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}

void sleep_ms(int ms, std::atomic<bool>* cancel) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cancelled(cancel)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool run_pinctrl(const std::vector<std::string>& args, std::string& error) {
    if (args.empty()) return true;
    if (::access("/usr/bin/pinctrl", X_OK) != 0 && ::access("/bin/pinctrl", X_OK) != 0) {
        // Optional on some images; GPIO chip path still works for many lines.
        return true;
    }
    std::vector<std::string> storage;
    storage.push_back("pinctrl");
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) argv.push_back(s.data());
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork pinctrl failed";
        return false;
    }
    if (pid == 0) {
        ::execvp("pinctrl", argv.data());
        _exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        error = "wait pinctrl failed";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "pinctrl failed";
        return false;
    }
    return true;
}

bool i2c_write_reg(int bus, uint8_t addr, uint8_t reg, uint8_t val, std::string& error) {
    const std::string path = "/dev/i2c-" + std::to_string(bus);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + path + " failed";
        return false;
    }
    if (::ioctl(fd, I2C_SLAVE, addr) < 0) {
        error = "I2C_SLAVE failed";
        ::close(fd);
        return false;
    }
    const uint8_t buf[2] = {reg, val};
    const ssize_t n = ::write(fd, buf, 2);
    ::close(fd);
    if (n != 2) {
        error = "I2C write failed";
        return false;
    }
    return true;
}

bool i2c_read_reg(int bus, uint8_t addr, uint8_t reg, uint8_t& val, std::string& error) {
    const std::string path = "/dev/i2c-" + std::to_string(bus);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + path + " failed";
        return false;
    }
    if (::ioctl(fd, I2C_SLAVE, addr) < 0) {
        error = "I2C_SLAVE failed";
        ::close(fd);
        return false;
    }
    if (::write(fd, &reg, 1) != 1) {
        error = "I2C reg select failed";
        ::close(fd);
        return false;
    }
    if (::read(fd, &val, 1) != 1) {
        error = "I2C read failed";
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
}

bool enable_pi4ioe_ant(std::string& error) {
    // Direction: bit0=1 => P0 output. Clear Hi-Z. Set output high.
    uint8_t dir = 0;
    if (!i2c_read_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeDir, dir, error)) return false;
    dir |= 0x01;
    if (!i2c_write_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeDir, dir, error)) return false;
    uint8_t hiz = 0;
    if (!i2c_read_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeHiZ, hiz, error)) return false;
    hiz &= static_cast<uint8_t>(~0x01u);
    if (!i2c_write_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeHiZ, hiz, error)) return false;
    uint8_t out = 0;
    if (!i2c_read_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeOut, out, error)) return false;
    out |= 0x01;
    return i2c_write_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeOut, out, error);
}

} // namespace

std::string default_cap_spidev_path() {
    if (kDesktop) return "/dev/spidev0.1";
    if (::access("/dev/spidev0.1", F_OK) == 0) return "/dev/spidev0.1";
    const auto list = list_spidev();
    for (const auto& p : list) {
        if (p.find("spidev0.1") != std::string::npos) return p;
    }
    if (!list.empty()) return list.front();
    return "/dev/spidev0.1";
}

bool enable_cap_power(CapKindHint kind, std::string& error, std::atomic<bool>* cancel) {
    error.clear();
    if (kDesktop) {
        error = "Cap / module not present";
        return false;
    }

    // Prefer GPIO mode on HAT_P0/P1 when the LED exists.
    if (led_exists(kUsbGpioFunLed)) {
        std::string ign;
        (void)write_led_brightness(kUsbGpioFunLed, 0, ign); // 0 ≈ GPIO mode on vendor images
    }

    // Pinmux Cap control lines when pinctrl is present (matches Cap-CC1101-SubG-Chat).
    std::string pin_err;
    if (kind == CapKindHint::Cc1101) {
        (void)run_pinctrl({"set", "14", "a5"}, pin_err);
        (void)run_pinctrl({"set", "15", "a5"}, pin_err);
    }
    (void)run_pinctrl({"set", "26", "op", "dh"}, pin_err);

    if (!g_held.gpio26_held) {
        g_held.power26 = GpioLine("/dev/gpiochip0", kGpioPowerEn);
        if (!g_held.power26.request_output(true, error)) {
            // Soft-fail: some images keep POWER_EN via pinctrl only.
            error.clear();
        } else {
            g_held.gpio26_held = true;
        }
    } else {
        (void)g_held.power26.set_value(true, error);
        error.clear();
    }

    if (led_exists(kExt5vLed)) {
        const int cur = read_led_brightness(kExt5vLed);
        if (cur <= 0) {
            g_held.prev_ext5v = cur;
            if (!write_led_brightness(kExt5vLed, 1, error)) return false;
            g_held.ext5v_owned = true;
        }
    } else {
        error = "ext_5v_out LED class missing";
        return false;
    }

    // Cap power rail settle (official CC1101 app waits ~1.5s on first enable).
    static bool settled = false;
    if (!settled) {
        sleep_ms(kind == CapKindHint::Cc1101 ? 1500 : 400, cancel);
        if (cancelled(cancel)) {
            error = "cancelled";
            return false;
        }
        settled = true;
    }

    if (kind == CapKindHint::Cc1101) {
        // RF_SW0 high selects 868/915 band (with RF_SW1 high on Cap PCB).
        if (!g_held.rf_sw_held) {
            g_held.rf_sw0 = GpioLine("/dev/gpiochip0", kGpioRfSw0);
            std::string e;
            if (g_held.rf_sw0.request_output(true, e)) {
                g_held.rf_sw_held = true;
            }
        } else {
            (void)g_held.rf_sw0.set_value(true, error);
            error.clear();
        }
        // GDO0 as input for future RX IRQ use.
        g_held.gdo0 = GpioLine("/dev/gpiochip0", kGpioGdo0);
        std::string e;
        (void)g_held.gdo0.request_input(e);
    }

    if (kind == CapKindHint::Sx1262) {
        // Pulse RST (GPIO26): already high; drop then raise.
        if (g_held.gpio26_held) {
            (void)g_held.power26.set_value(false, error);
            sleep_ms(20, cancel);
            (void)g_held.power26.set_value(true, error);
            sleep_ms(20, cancel);
            error.clear();
        }
        if (!g_held.busy_held) {
            g_held.busy = GpioLine("/dev/gpiochip0", kGpioBusy);
            std::string e;
            if (g_held.busy.request_input(e)) g_held.busy_held = true;
        }
        if (!g_held.irq_held) {
            g_held.irq = GpioLine("/dev/gpiochip0", kGpioIrq);
            std::string e;
            if (g_held.irq.request_input(e)) g_held.irq_held = true;
        }
        std::string ant_err;
        if (!enable_pi4ioe_ant(ant_err)) {
            // Soft-fail: Cap may still respond on SPI without antenna path.
            error.clear();
        }
        (void)wait_sx1262_not_busy(500, error, cancel);
        error.clear();
    }

    return true;
}

void release_cap_power(bool preserve_ext5v) {
    g_held.rf_sw0.release();
    g_held.gdo0.release();
    g_held.busy.release();
    g_held.irq.release();
    g_held.rf_sw_held = false;
    g_held.busy_held = false;
    g_held.irq_held = false;
    // Keep GPIO26 / EXT5V for shared LCD SPI bus unless explicitly requested.
    if (!preserve_ext5v) {
        std::string e;
        if (g_held.gpio26_held) {
            (void)g_held.power26.set_value(false, e);
            g_held.power26.release();
            g_held.gpio26_held = false;
        }
        if (g_held.ext5v_owned) {
            (void)write_led_brightness(kExt5vLed, g_held.prev_ext5v > 0 ? 1 : 0, e);
            g_held.ext5v_owned = false;
        }
    }
}

CapPowerStatus query_cap_power_status() {
    CapPowerStatus st;
    st.spidev = default_cap_spidev_path();
    if (kDesktop) {
        st.lines.push_back("desktop: Cap / module not present");
        return st;
    }
    if (led_exists(kExt5vLed)) {
        st.ext5v_on = read_led_brightness(kExt5vLed) > 0;
        st.lines.push_back(std::string("ext_5v_out=") + (st.ext5v_on ? "on" : "off"));
    } else {
        st.lines.push_back("ext_5v_out=missing");
    }
    {
        GpioLine line("/dev/gpiochip0", kGpioPowerEn);
        std::string e;
        bool high = false;
        if (line.request_input(e) && line.get_value(high, e)) {
            st.gpio26_high = high;
            st.lines.push_back(std::string("GPIO26=") + (high ? "1" : "0"));
        } else {
            st.lines.push_back("GPIO26=unreadable");
        }
    }
    {
        uint8_t out = 0;
        std::string e;
        if (i2c_read_reg(kPi4ioeBus, kPi4ioeAddr, kPi4ioeOut, out, e)) {
            st.pi4ioe_ant_on = (out & 0x01) != 0;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "PI4IOE@0x43 out=0x%02X ant=%s", out, st.pi4ioe_ant_on ? "on" : "off");
            st.lines.push_back(buf);
        } else {
            st.lines.push_back("PI4IOE@0x43=missing");
        }
    }
    st.lines.push_back("spidev=" + st.spidev);
    const auto list = list_spidev();
    if (list.empty()) st.lines.push_back("spidev list: none");
    for (const auto& p : list) st.lines.push_back("spidev list: " + p);
    return st;
}

bool wait_sx1262_not_busy(int timeout_ms, std::string& error, std::atomic<bool>* cancel) {
    error.clear();
    if (kDesktop) {
        error = "Cap / module not present";
        return false;
    }
    GpioLine* busy = nullptr;
    GpioLine local;
    if (g_held.busy_held) {
        busy = &g_held.busy;
    } else {
        local = GpioLine("/dev/gpiochip0", kGpioBusy);
        if (!local.request_input(error)) return false;
        busy = &local;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(cancel)) {
            error = "cancelled";
            return false;
        }
        bool high = true;
        if (!busy->get_value(high, error)) return false;
        if (!high) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    error = "SX1262 BUSY timeout";
    return false;
}

} // namespace platform
