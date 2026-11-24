// kvxputerzero-hidgadget — configfs USB HID gadget for kvxkeyboard HID.
//
// Installed setuid root by the package postinst (configfs is root-only). It
// takes one fixed verb and no other input:
//   up      build the gadget, bind it to the first UDC, give /dev/hidgN to the
//           invoking user. Any other gadget bound to that UDC (ADB, serial…)
//           is unbound and remembered so `down` can restore it.
//   down    unbind and remove the gadget, rebind what `up` displaced.
//   status  print the UDC state.
//   ble-prep  unblock the Bluetooth radio, start bluetoothd, and allow the
//           invoking user to reach org.bluez on the system bus.
// Output is one line: "OK <udc> <devnode>" / "OK <state>" / "ERR <reason>".
//
// The Cardputer Zero Host/Slave switch must be on Slave for a UDC to appear.

#include "hid_lib.h"

#include <cctype>
#include <cerrno>
#include <iterator>
#include <pwd.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kConfigfs = "/sys/kernel/config";
constexpr const char* kGadgetRoot = "/sys/kernel/config/usb_gadget";
constexpr const char* kGadget = "/sys/kernel/config/usb_gadget/kvxkeyboard";
constexpr const char* kPrevFile = "/run/kvxputerzero-hidgadget.prev";

int fail(const std::string& why) {
    std::printf("ERR %s\n", why.c_str());
    return 1;
}

bool write_file(const std::string& path, const void* data, size_t len) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t n = ::write(fd, data, len);
    const int err = errno;
    ::close(fd);
    errno = err;
    return n == static_cast<ssize_t>(len);
}

bool write_str(const std::string& path, const std::string& s) {
    return write_file(path, s.data(), s.size());
}

std::string read_str(const std::string& path) {
    std::ifstream in(path);
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool mkdir_p(const std::string& path) {
    if (::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) return true;
    return false;
}

std::vector<std::string> list_dir(const char* path) {
    std::vector<std::string> out;
    DIR* d = ::opendir(path);
    if (!d) return out;
    while (auto* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        out.emplace_back(e->d_name);
    }
    ::closedir(d);
    return out;
}

// Run a fixed system tool with a scrubbed environment. Never goes through a
// shell; `name` is looked up only in the fixed system directories below.
int run_tool(const char* name, std::vector<const char*> args) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(name));
        for (const char* a : args) argv.push_back(const_cast<char*>(a));
        argv.push_back(nullptr);
        char* const envp[] = {const_cast<char*>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"), nullptr};
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
        }
        for (const char* dir : {"/usr/sbin/", "/usr/bin/", "/sbin/", "/bin/"}) {
            const std::string path = std::string(dir) + name;
            ::execve(path.c_str(), argv.data(), envp);
        }
        ::_exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void modprobe(const char* module) {
    run_tool("modprobe", {module});
}

// The Host/Slave switch is physical. On Host the CM0 talks to the internal hub,
// so non-root USB devices (hub, USB-Ethernet …) appear on its bus.
bool switch_on_host() {
    for (const auto& name : list_dir("/sys/bus/usb/devices")) {
        if (name.find('-') != std::string::npos && name.find(':') == std::string::npos) return true;
    }
    return false;
}

bool ensure_configfs() {
    if (::access(kGadgetRoot, F_OK) == 0) return true;
    modprobe("libcomposite");
    if (::access(kGadgetRoot, F_OK) == 0) return true;
    ::mount("none", kConfigfs, "configfs", 0, nullptr);
    return ::access(kGadgetRoot, F_OK) == 0;
}

std::string first_udc() {
    auto udcs = list_dir("/sys/class/udc");
    return udcs.empty() ? std::string() : udcs.front();
}

void teardown_gadget() {
    if (::access(kGadget, F_OK) != 0) return;
    const std::string g = kGadget;
    write_str(g + "/UDC", "\n");
    ::unlink((g + "/configs/c.1/hid.usb0").c_str());
    ::rmdir((g + "/configs/c.1/strings/0x409").c_str());
    ::rmdir((g + "/configs/c.1").c_str());
    ::rmdir((g + "/functions/hid.usb0").c_str());
    ::rmdir((g + "/strings/0x409").c_str());
    ::rmdir(g.c_str());
}

// Unbind every other gadget holding `udc`; remember them for `down`.
void displace_other_gadgets(const std::string& udc) {
    std::string prev;
    for (const auto& name : list_dir(kGadgetRoot)) {
        if (name == "kvxkeyboard") continue;
        const std::string path = std::string(kGadgetRoot) + "/" + name + "/UDC";
        if (read_str(path) == udc && write_str(path, "\n")) prev += name + "\n";
    }
    if (prev.empty()) return;
    // Keep earlier entries if `up` runs twice before `down`.
    std::ifstream old(kPrevFile);
    std::string line;
    while (std::getline(old, line)) {
        if (!line.empty() && prev.find(line + "\n") == std::string::npos) prev += line + "\n";
    }
    const int fd = ::open(kPrevFile, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        (void)!::write(fd, prev.data(), prev.size());
        ::close(fd);
    }
}

void restore_other_gadgets(const std::string& udc) {
    std::ifstream in(kPrevFile);
    std::string name;
    while (std::getline(in, name)) {
        if (name.empty() || name.find('/') != std::string::npos || name == "." || name == "..") continue;
        const std::string path = std::string(kGadgetRoot) + "/" + name + "/UDC";
        if (::access(path.c_str(), W_OK) == 0 && !udc.empty()) write_str(path, udc + "\n");
    }
    ::unlink(kPrevFile);
}

std::string find_hidg_node(const std::string& devnum) {
    unsigned maj = 0, min = 0;
    if (std::sscanf(devnum.c_str(), "%u:%u", &maj, &min) != 2) return {};
    const dev_t want = makedev(maj, min);
    // udev may still be creating the node.
    for (int attempt = 0; attempt < 40; ++attempt) {
        for (const auto& name : list_dir("/dev")) {
            if (name.compare(0, 4, "hidg") != 0) continue;
            const std::string path = "/dev/" + name;
            struct stat st {};
            if (::stat(path.c_str(), &st) == 0 && S_ISCHR(st.st_mode) && st.st_rdev == want) return path;
        }
        ::usleep(50 * 1000);
    }
    return {};
}

int cmd_up(uid_t ruid, gid_t rgid) {
    if (!ensure_configfs()) return fail("configfs/libcomposite unavailable");
    std::string udc = first_udc();
    if (udc.empty()) {
        modprobe("dwc2"); // OTG controller driver; harmless when already loaded
        for (int i = 0; i < 20 && udc.empty(); ++i) {
            ::usleep(50 * 1000);
            udc = first_udc();
        }
    }
    if (udc.empty()) {
        if (switch_on_host()) return fail("SWITCH_HOST");
        return fail("no USB device controller on this image");
    }

    teardown_gadget(); // stale gadget from a crash
    displace_other_gadgets(udc);

    const std::string g = kGadget;
    if (!mkdir_p(g)) return fail(std::string("mkdir gadget: ") + std::strerror(errno));
    bool ok = true;
    // pid.codes generic HID (same identity as the ESP32 build).
    ok &= write_str(g + "/idVendor", "0x1209");
    ok &= write_str(g + "/idProduct", "0x0001");
    ok &= write_str(g + "/bcdDevice", "0x0100");
    ok &= write_str(g + "/bcdUSB", "0x0200");
    ok &= mkdir_p(g + "/strings/0x409");
    ok &= write_str(g + "/strings/0x409/manufacturer", "Generic");
    ok &= write_str(g + "/strings/0x409/product", "HID Keyboard");
    ok &= write_str(g + "/strings/0x409/serialnumber", "1");
    ok &= mkdir_p(g + "/configs/c.1");
    ok &= mkdir_p(g + "/configs/c.1/strings/0x409");
    ok &= write_str(g + "/configs/c.1/strings/0x409/configuration", "HID");
    ok &= write_str(g + "/configs/c.1/MaxPower", "100");
    ok &= mkdir_p(g + "/functions/hid.usb0");
    // Report IDs in use, so no boot-protocol subclass.
    ok &= write_str(g + "/functions/hid.usb0/protocol", "0");
    ok &= write_str(g + "/functions/hid.usb0/subclass", "0");
    ok &= write_str(g + "/functions/hid.usb0/report_length", std::to_string(tools::hid::kMaxReportLen));
    const auto& desc = tools::hid::report_descriptor();
    ok &= write_file(g + "/functions/hid.usb0/report_desc", desc.data(), desc.size());
    if (!ok) {
        const std::string why = std::strerror(errno);
        teardown_gadget();
        restore_other_gadgets(udc);
        return fail("configure gadget: " + why);
    }
    if (::symlink((g + "/functions/hid.usb0").c_str(), (g + "/configs/c.1/hid.usb0").c_str()) != 0 &&
        errno != EEXIST) {
        const std::string why = std::strerror(errno);
        teardown_gadget();
        restore_other_gadgets(udc);
        return fail("link function: " + why);
    }
    if (!write_str(g + "/UDC", udc + "\n")) {
        const std::string why = std::strerror(errno);
        teardown_gadget();
        restore_other_gadgets(udc);
        return fail("bind " + udc + ": " + why);
    }

    const std::string node = find_hidg_node(read_str(g + "/functions/hid.usb0/dev"));
    if (node.empty()) {
        teardown_gadget();
        restore_other_gadgets(udc);
        return fail("no /dev/hidg node appeared");
    }
    if (::chown(node.c_str(), ruid, rgid) != 0 || ::chmod(node.c_str(), 0600) != 0) {
        const std::string why = std::strerror(errno);
        teardown_gadget();
        restore_other_gadgets(udc);
        return fail("chown " + node + ": " + why);
    }
    std::printf("OK %s %s\n", udc.c_str(), node.c_str());
    return 0;
}

int cmd_down() {
    std::string udc = ::access(kGadget, F_OK) == 0 ? read_str(std::string(kGadget) + "/UDC") : "";
    if (udc.empty()) udc = first_udc();
    teardown_gadget();
    restore_other_gadgets(udc);
    std::printf("OK down\n");
    return 0;
}

// Bluetooth prerequisites for kvxkeyboard HID, so the user never needs a shell:
// unblock the radio, start bluetoothd, and let the invoking user talk to BlueZ.
int cmd_ble_prep(uid_t ruid) {
    std::string notes;
    // 1. rfkill soft block on any Bluetooth radio.
    for (const auto& name : list_dir("/sys/class/rfkill")) {
        const std::string base = std::string("/sys/class/rfkill/") + name;
        if (read_str(base + "/type") != "bluetooth") continue;
        if (read_str(base + "/soft") == "1" && write_str(base + "/soft", "0")) notes += "unblocked ";
        if (read_str(base + "/hard") == "1") return fail("Bluetooth radio is hard-blocked");
    }
    // 2. bluetoothd.
    if (run_tool("systemctl", {"is-active", "--quiet", "bluetooth"}) != 0) {
        run_tool("systemctl", {"start", "bluetooth"});
        if (run_tool("systemctl", {"is-active", "--quiet", "bluetooth"}) != 0) {
            return fail("bluetooth service will not start");
        }
        notes += "started ";
    }
    // 3. D-Bus policy so this user may call org.bluez even outside the
    //    `bluetooth` group (group changes would need a new login).
    struct passwd* pw = ::getpwuid(ruid);
    if (pw && pw->pw_name && ruid != 0) {
        const std::string user = pw->pw_name;
        bool safe = !user.empty();
        for (char c : user) safe = safe && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.');
        const std::string path = "/etc/dbus-1/system.d/kvxputerzero-hid.conf";
        const std::string want =
            "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\"\n"
            " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
            "<!-- Written by kvxputerzero-hidgadget: kvxkeyboard HID needs BlueZ. -->\n"
            "<busconfig>\n  <policy user=\"" + user + "\">\n"
            "    <allow send_destination=\"org.bluez\"/>\n"
            "  </policy>\n</busconfig>\n";
        std::ifstream in(path);
        const std::string have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (safe && have.find("user=\"" + user + "\"") == std::string::npos) {
            mkdir_p("/etc/dbus-1");
            mkdir_p("/etc/dbus-1/system.d");
            const std::string tmp = path + ".tmp";
            const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd >= 0) {
                const bool ok = ::write(fd, want.data(), want.size()) == static_cast<ssize_t>(want.size());
                ::close(fd);
                if (ok && ::rename(tmp.c_str(), path.c_str()) == 0) {
                    run_tool("busctl", {"call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                        "org.freedesktop.DBus", "ReloadConfig"});
                    notes += "dbus ";
                } else {
                    ::unlink(tmp.c_str());
                }
            }
        }
    }
    std::printf("OK %s\n", notes.empty() ? "ready" : notes.c_str());
    return 0;
}

int cmd_status() {
    const std::string udc = first_udc();
    if (udc.empty()) return fail("no UDC");
    const std::string state = read_str("/sys/class/udc/" + udc + "/state");
    std::printf("OK %s %s\n", udc.c_str(), state.empty() ? "unknown" : state.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Unbuffered single-line protocol; the caller reads it through a pipe.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const uid_t ruid = ::getuid();
    const gid_t rgid = ::getgid();
    if (argc != 2) return fail("usage: kvxputerzero-hidgadget up|down|status|ble-prep");
    const std::string verb = argv[1];
    if (verb == "status") return cmd_status();

    if (::geteuid() != 0) return fail("Reinstall kvxputerzero: its USB/Bluetooth helper lost its permissions");
    if (::setresgid(0, 0, 0) != 0 || ::setresuid(0, 0, 0) != 0) return fail("cannot become root");
    ::umask(022);
    if (verb == "up") return cmd_up(ruid, rgid);
    if (verb == "ble-prep") return cmd_ble_prep(ruid);
    if (verb == "down") return cmd_down();
    return fail("unknown verb");
}
