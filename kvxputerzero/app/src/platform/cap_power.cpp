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
constexpr char kGroveFunLed[] = "grove_fun";      // 0=UART, 1=I2C
constexpr char kGrove5vLed[] = "grove_5v_out";
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
    bool grove5v_owned{false};
    int prev_grove5v{0};
    bool grove_fun_owned{false};
    int prev_grove_fun{0};
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

bool led_value_matches(const char* name, int want_on) {
    const int cur = read_led_brightness(name);
    return want_on ? (cur > 0) : (cur <= 0);
}

bool write_led_direct(const char* name, int value, std::string& error) {
    const std::string path = led_brightness_path(name);
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + path + " failed";
        return false;
    }
    // Prefer max_brightness when turning on — matches vendor tee behavior (reads back 255).
    std::string payload = value > 0 ? "1" : "0";
    if (value > 0) {
        std::ifstream mx(std::string("/sys/class/leds/") + name + "/max_brightness");
        int max_v = 0;
        if (mx >> max_v && max_v > 0) payload = std::to_string(max_v);
    }
    const ssize_t n = ::write(fd, payload.data(), payload.size());
    ::close(fd);
    if (n != static_cast<ssize_t>(payload.size())) {
        error = "write " + path + " failed";
        return false;
    }
    return true;
}

bool run_sysfs_led_helper(const char* name, int value, bool use_sudo, std::string& error) {
    const char* helper = "/usr/lib/kvxputerzero/sysfs-led";
    if (::access(helper, X_OK) != 0) {
        error = "sysfs-led helper missing";
        return false;
    }
    const std::string val = value > 0 ? "1" : "0";
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork sysfs-led failed";
        return false;
    }
    if (pid == 0) {
        if (use_sudo) {
            ::execl("/usr/bin/sudo", "sudo", "-n", helper, name, val.c_str(), static_cast<char*>(nullptr));
            ::execlp("sudo", "sudo", "-n", helper, name, val.c_str(), static_cast<char*>(nullptr));
        } else {
            // Setuid binary elevates itself — works for APPLaunch and systemd users.
            ::execl(helper, helper, name, val.c_str(), static_cast<char*>(nullptr));
        }
        _exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        error = "waitpid sysfs-led failed";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        error = std::string(use_sudo ? "sudo " : "") + "sysfs-led " + name + " exit=" + std::to_string(code);
        return false;
    }
    return true;
}

bool write_led_via_sudo(const char* name, int value, std::string& error) {
    // Prefer setuid helper (no sudoers). Fall back to sudo -n for older packages.
    if (run_sysfs_led_helper(name, value, false, error)) return true;
    std::string sudo_err;
    if (run_sysfs_led_helper(name, value, true, sudo_err)) {
        error.clear();
        return true;
    }
    if (!error.empty()) error += "; ";
    error += sudo_err;
    return false;
}

bool write_led_brightness(const char* name, int value, std::string& error) {
    // Cap/Grove power LEDs often accept a non-root open/write that does not stick.
    // Always prefer sudo for those, then verify the sysfs value.
    const bool power_led = std::strcmp(name, "grove_fun") == 0 || std::strcmp(name, "grove_5v_out") == 0 ||
                           std::strcmp(name, "ext_5v_out") == 0 || std::strcmp(name, "ext_usb_gpio_fun") == 0;

    auto try_verify = [&](const char* how) -> bool {
        // Give the M5IOE LED class a tick to settle.
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (led_value_matches(name, value > 0)) {
            error.clear();
            return true;
        }
        error = std::string(name) + " still " + (value > 0 ? "off" : "on") + " after " + how;
        return false;
    };

    if (power_led) {
        // Prefer direct write (udev 99-kvxputerzero-leds.rules makes brightness world-writable).
        std::string direct_err;
        if (write_led_direct(name, value, direct_err)) {
            if (try_verify("direct")) return true;
            error = direct_err.empty() ? std::string(name) + " direct write did not stick" : direct_err;
        } else {
            error = direct_err;
        }
        std::string helper_err;
        if (write_led_via_sudo(name, value, helper_err)) {
            if (try_verify("helper")) return true;
            if (!error.empty()) error += "; ";
            error += helper_err;
        } else {
            if (!error.empty()) error += "; ";
            error += helper_err;
        }
        return false;
    }

    if (write_led_direct(name, value, error)) return true;
    std::string sudo_err;
    if (write_led_via_sudo(name, value, sudo_err)) {
        error.clear();
        return true;
    }
    if (!sudo_err.empty()) error = error + "; " + sudo_err;
    return false;
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
    // CapKindHint::Gps: no GPIO14/15 remux — stock Cap-LoRa-1262-GPS leaves UART to
    // boot (enable_uart / serial0→ttyS0). Cc1101 remux of 14/15 can steal NMEA; caller
    // should restore UART via device troubleshooting if SubG was used first.
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
            if (!write_led_brightness(kExt5vLed, 1, error)) {
                // Store builds cannot ship setuid/sudoers; Cap may still be powered externally.
                error.clear();
            } else {
                g_held.ext5v_owned = true;
            }
        }
    } else {
        // Soft-fail: some images expose Cap power differently.
        error.clear();
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
        // Default RF_SW0 high (868/915). Callers retune via set_cc1101_rf_path().
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
        g_held.gdo0 = GpioLine("/dev/gpiochip0", kGpioGdo0);
        std::string e;
        (void)g_held.gdo0.request_input(e);
    }

    if (kind == CapKindHint::Gps) {
        // Cap LoRa-1262 GNSS: EXT5V (+ POWER_EN) already applied. No SPI / RST / Grove.
        return true;
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

bool set_cc1101_rf_path(uint32_t hz, std::string& error) {
    error.clear();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    // Low path for sub-500 MHz; high for 779–928 (RF_SW1 is hardwired high on Cap).
    const bool high = hz >= 500000000u;
    if (!g_held.rf_sw_held) {
        g_held.rf_sw0 = GpioLine("/dev/gpiochip0", kGpioRfSw0);
        if (!g_held.rf_sw0.request_output(high, error)) return false;
        g_held.rf_sw_held = true;
        return true;
    }
    return g_held.rf_sw0.set_value(high, error);
}

bool read_cc1101_gdo0(bool& high, std::string& error) {
    error.clear();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    if (!g_held.gdo0.ok()) {
        g_held.gdo0 = GpioLine("/dev/gpiochip0", kGpioGdo0);
        if (!g_held.gdo0.request_input(error)) return false;
    }
    return g_held.gdo0.get_value(high, error);
}

bool request_cc1101_gdo0_edges(std::string& error) {
    error.clear();
    if (kDesktop) {
        error = "Cap not present";
        return false;
    }
    g_held.gdo0 = GpioLine("/dev/gpiochip0", kGpioGdo0);
    return g_held.gdo0.request_edge_events(error);
}

int cc1101_gdo0_fd() {
    return g_held.gdo0.fd();
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


bool enable_grove_uart_power(std::string& error) {
    error.clear();
    if (kDesktop) {
        error = "Grove not present";
        return false;
    }
    // grove_fun: 0 = UART (Unit GPS), 1 = I2C (Unit Scroll / scrolld).
    // Write only when the rail/mode is wrong so we do not fork sudo every poll
    // (that was starving the UI and making the battery meter flicker off).
    // Best-effort only: Cardputer store forbids setuid/sudoers helpers for these LEDs.
    if (led_exists(kGroveFunLed)) {
        const int cur = read_led_brightness(kGroveFunLed);
        if (!g_held.grove_fun_owned) {
            g_held.prev_grove_fun = cur;
            g_held.grove_fun_owned = true;
        }
        if (cur != 0) {
            std::string ign;
            (void)write_led_brightness(kGroveFunLed, 0, ign);
        }
    }
    if (led_exists(kGrove5vLed)) {
        const int cur = read_led_brightness(kGrove5vLed);
        if (!g_held.grove5v_owned) {
            g_held.prev_grove5v = cur;
            g_held.grove5v_owned = true;
        }
        if (cur <= 0) {
            std::string ign;
            (void)write_led_brightness(kGrove5vLed, 1, ign);
        }
    }
    error.clear();
    return true;
}

void release_grove_uart_power(bool preserve_5v) {
    if (kDesktop) return;
    std::string e;
    if (g_held.grove_fun_owned) {
        (void)write_led_brightness(kGroveFunLed, g_held.prev_grove_fun > 0 ? 1 : 0, e);
        g_held.grove_fun_owned = false;
    }
    if (g_held.grove5v_owned && !preserve_5v) {
        (void)write_led_brightness(kGrove5vLed, g_held.prev_grove5v > 0 ? 1 : 0, e);
        g_held.grove5v_owned = false;
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
