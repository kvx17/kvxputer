#include "ir_lirc.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace platform {
namespace {

// Some kernels expose scancode TX via LIRC_MODE_SCANCODE without a dedicated CAN_SEND_* macro.
#ifndef LIRC_CAN_SEND_SCANCODE
#define LIRC_CAN_SEND_SCANCODE LIRC_MODE2SEND(LIRC_MODE_SCANCODE)
#endif

constexpr unsigned kNecHdrMark = 9000;
constexpr unsigned kNecHdrSpace = 4500;
constexpr unsigned kNecBitMark = 560;
constexpr unsigned kNecZeroSpace = 560;
constexpr unsigned kNecOneSpace = 1690;
constexpr unsigned kNecCarrier = 38000;
constexpr unsigned kTolPct = 30;

bool approx(unsigned v, unsigned expected) {
    const unsigned lo = expected * (100 - kTolPct) / 100;
    const unsigned hi = expected * (100 + kTolPct) / 100;
    return v >= lo && v <= hi;
}

void push_nec_bit(std::vector<__u32>& buf, bool one) {
    buf.push_back(kNecBitMark);
    buf.push_back(one ? kNecOneSpace : kNecZeroSpace);
}

void push_nec_byte(std::vector<__u32>& buf, uint8_t value) {
    for (int i = 0; i < 8; ++i) {
        push_nec_bit(buf, (value >> i) & 1);
    }
}

std::vector<__u32> encode_nec_pulses(uint16_t address, uint16_t command) {
    std::vector<__u32> buf;
    buf.reserve(68);
    buf.push_back(kNecHdrMark);
    buf.push_back(kNecHdrSpace);
    if (address <= 0xFF && command <= 0xFF) {
        const uint8_t addr = static_cast<uint8_t>(address);
        const uint8_t cmd = static_cast<uint8_t>(command);
        push_nec_byte(buf, addr);
        push_nec_byte(buf, static_cast<uint8_t>(~addr));
        push_nec_byte(buf, cmd);
        push_nec_byte(buf, static_cast<uint8_t>(~cmd));
    } else {
        // Extended NEC: 16-bit address, 8-bit command + inverse.
        push_nec_byte(buf, static_cast<uint8_t>(address & 0xFF));
        push_nec_byte(buf, static_cast<uint8_t>((address >> 8) & 0xFF));
        const uint8_t cmd = static_cast<uint8_t>(command & 0xFF);
        push_nec_byte(buf, cmd);
        push_nec_byte(buf, static_cast<uint8_t>(~cmd));
    }
    buf.push_back(kNecBitMark); // trailing mark
    return buf;
}

bool decode_nec_from_mode2(const unsigned int* samples, size_t n, IrPacket& out) {
    out = {};
    if (n < 67) return false;

    size_t i = 0;
    // Skip leading space if present.
    if (i < n && samples[i] < 2000 && (i + 1 < n) && approx(samples[i + 1], kNecHdrMark)) {
        ++i;
    }
    if (i + 1 >= n || !approx(samples[i], kNecHdrMark) || !approx(samples[i + 1], kNecHdrSpace)) {
        return false;
    }
    i += 2;

    auto read_bit = [&](bool& bit) -> bool {
        if (i + 1 >= n) return false;
        if (!approx(samples[i], kNecBitMark)) return false;
        const unsigned space = samples[i + 1];
        if (approx(space, kNecOneSpace)) bit = true;
        else if (approx(space, kNecZeroSpace)) bit = false;
        else return false;
        i += 2;
        return true;
    };

    auto read_byte = [&](uint8_t& value) -> bool {
        value = 0;
        for (int b = 0; b < 8; ++b) {
            bool bit = false;
            if (!read_bit(bit)) return false;
            if (bit) value = static_cast<uint8_t>(value | (1u << b));
        }
        return true;
    };

    uint8_t a0 = 0, a1 = 0, c0 = 0, c1 = 0;
    if (!read_byte(a0) || !read_byte(a1) || !read_byte(c0) || !read_byte(c1)) return false;

    if (static_cast<uint8_t>(~a0) == a1 && static_cast<uint8_t>(~c0) == c1) {
        out.address = a0;
        out.command = c0;
        out.valid = true;
        return true;
    }
    // Extended: a0/a1 form 16-bit address; c1 should invert c0.
    if (static_cast<uint8_t>(~c0) == c1) {
        out.address = static_cast<uint16_t>(a0 | (static_cast<uint16_t>(a1) << 8));
        out.command = c0;
        out.valid = true;
        return true;
    }
    return false;
}

IrPacket packet_from_scancode(const lirc_scancode& sc) {
    IrPacket pkt;
    if (sc.rc_proto == RC_PROTO_NEC) {
        pkt.address = static_cast<uint16_t>((sc.scancode >> 8) & 0xFF);
        pkt.command = static_cast<uint16_t>(sc.scancode & 0xFF);
        pkt.valid = true;
    } else if (sc.rc_proto == RC_PROTO_NECX || sc.rc_proto == RC_PROTO_NEC32) {
        pkt.address = static_cast<uint16_t>((sc.scancode >> 8) & 0xFFFF);
        pkt.command = static_cast<uint16_t>(sc.scancode & 0xFF);
        pkt.valid = true;
    }
    return pkt;
}

std::string sysfs_rc_name(const std::string& lirc_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dev = fs::path("/sys/class/rc");
    if (!fs::exists(dev, ec)) return {};
    for (const auto& entry : fs::directory_iterator(dev, ec)) {
        if (ec) break;
        const fs::path lirc_link = entry.path() / "lirc";
        if (!fs::exists(lirc_link, ec)) continue;
        for (const auto& child : fs::directory_iterator(lirc_link, ec)) {
            if (ec) break;
            if (("/dev/" + child.path().filename().string()) == lirc_path) {
                std::ifstream in(entry.path() / "name");
                std::string name;
                if (in && std::getline(in, name)) return name;
                return entry.path().filename().string();
            }
        }
    }
    return {};
}

struct LircNode {
    std::string path;
    std::string name;
    __u32 features{0};
};

bool open_lirc(const std::string& path, int& fd, std::string& error) {
    fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        error = std::string("open failed: ") + path;
        return false;
    }
    return true;
}

bool get_features(int fd, __u32& features, std::string& error) {
    features = 0;
    if (ioctl(fd, LIRC_GET_FEATURES, &features) < 0) {
        error = "LIRC_GET_FEATURES failed";
        return false;
    }
    return true;
}

bool can_receive(const __u32 features) {
    return (features & (LIRC_CAN_REC_MODE2 | LIRC_CAN_REC_SCANCODE | LIRC_CAN_REC_PULSE)) != 0;
}

bool can_send(const __u32 features) {
    return (features & (LIRC_CAN_SEND_PULSE | LIRC_CAN_SEND_MODE2 | LIRC_CAN_SEND_SCANCODE)) != 0;
}

// Onboard IR is wired to GPIO12 (TX) and GPIO13 (RX). LIRC is used when the kernel
// exposes it. Otherwise the GPIO block is driven directly through /dev/gpiomem,
// which is the BCM2837 register window on the CM0.
constexpr unsigned kGpioTx = kOnboardIrTxGpio;
constexpr unsigned kGpioRx = kOnboardIrRxGpio;

struct GpioMap {
    int fd{-1};
    volatile uint32_t* base{nullptr};

    bool open_map(std::string& error) {
        fd = ::open("/dev/gpiomem", O_RDWR | O_SYNC);
        if (fd < 0) fd = ::open("/dev/gpiomem0", O_RDWR | O_SYNC);
        if (fd < 0) {
            error = "cannot open /dev/gpiomem for onboard IR (TX GPIO12 / RX GPIO13)";
            return false;
        }
        void* p = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            error = "cannot map onboard IR GPIO";
            ::close(fd);
            fd = -1;
            return false;
        }
        base = static_cast<volatile uint32_t*>(p);
        return true;
    }

    void close_map() {
        if (base) {
            ::munmap(const_cast<uint32_t*>(base), 4096);
            base = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    uint32_t fsel(unsigned gpio) const {
        const unsigned shift = (gpio % 10) * 3;
        return (base[gpio / 10] >> shift) & 7u;
    }

    void set_fsel(unsigned gpio, uint32_t function) {
        const unsigned shift = (gpio % 10) * 3;
        uint32_t v = base[gpio / 10];
        v &= ~(7u << shift);
        v |= (function & 7u) << shift;
        base[gpio / 10] = v;
    }

    void set_high(unsigned gpio) { base[0x1C / 4] = 1u << gpio; }
    void set_low(unsigned gpio) { base[0x28 / 4] = 1u << gpio; }
    bool level_high(unsigned gpio) const { return (base[0x34 / 4] & (1u << gpio)) != 0; }
};

bool gpio_mem_available() {
#if USE_DESKTOP
    return false;
#else
    GpioMap map;
    std::string err;
    if (!map.open_map(err)) return false;
    map.close_map();
    return true;
#endif
}

volatile uint32_t delay_sink = 0;

void delay_loops(uint32_t n) {
    while (n--) delay_sink += 1;
}

uint32_t loops_per_us() {
    static const uint32_t cached = []() {
        constexpr uint32_t kIters = 200000;
        timespec a{};
        timespec b{};
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (uint32_t i = 0; i < kIters; ++i) delay_sink += 1;
        clock_gettime(CLOCK_MONOTONIC, &b);
        const uint64_t dt =
            static_cast<uint64_t>(b.tv_sec - a.tv_sec) * 1000000000ull + static_cast<uint64_t>(b.tv_nsec - a.tv_nsec);
        const uint64_t safe = dt < 1000ull ? 1000ull : dt;
        uint64_t lpu = static_cast<uint64_t>(kIters) * 1000ull / safe;
        if (lpu < 1) lpu = 1;
        if (lpu > 100000) lpu = 100000;
        return static_cast<uint32_t>(lpu);
    }();
    return cached;
}

uint64_t mono_us() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ull + static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

void emit_carrier(GpioMap& map, unsigned gpio, uint32_t us, uint32_t carrier_hz) {
    if (carrier_hz < 1000) carrier_hz = kNecCarrier;
    const uint32_t lpu = loops_per_us();
    const uint32_t period_ns = 1000000000u / carrier_hz;
    const uint32_t high_ns = period_ns / 3;
    const uint32_t low_ns = period_ns - high_ns;
    const uint32_t high_loops = std::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(lpu) * high_ns / 1000ull));
    const uint32_t low_loops = std::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(lpu) * low_ns / 1000ull));
    uint32_t cycles = std::max(1u, static_cast<uint32_t>(static_cast<uint64_t>(us) * carrier_hz / 1000000ull));
    while (cycles--) {
        map.set_high(gpio);
        delay_loops(high_loops);
        map.set_low(gpio);
        delay_loops(low_loops);
    }
    map.set_low(gpio);
}

void emit_space(GpioMap& map, unsigned gpio, uint32_t us) {
    map.set_low(gpio);
    const uint32_t n = loops_per_us() * std::max(1u, us);
    delay_loops(n);
}

IrSendResult send_pulses_gpio(const uint32_t* pulses_us, size_t count, uint32_t carrier_hz, bool* cancel) {
    IrSendResult r;
    if (!pulses_us || count == 0) {
        r.error = "empty pulse buffer";
        return r;
    }
    GpioMap map;
    if (!map.open_map(r.error)) return r;
    const uint32_t saved = map.fsel(kGpioTx);
    map.set_low(kGpioTx);
    map.set_fsel(kGpioTx, 1);
    const uint32_t carrier = carrier_hz ? carrier_hz : kNecCarrier;
    for (size_t i = 0; i < count; ++i) {
        if (cancel && *cancel) break;
        const uint32_t us = pulses_us[i] == 0 ? 1u : std::min(pulses_us[i], 500000u);
        if ((i % 2) == 0) emit_carrier(map, kGpioTx, us, carrier);
        else emit_space(map, kGpioTx, us);
    }
    map.set_low(kGpioTx);
    map.set_fsel(kGpioTx, saved);
    map.close_map();
    r.ok = true;
    return r;
}

bool gpio_path(const std::string& path) {
    return path == "gpio12" || path == "gpio13";
}

std::vector<LircNode> enumerate_lirc() {
    std::vector<LircNode> nodes;
    DIR* dir = opendir("/dev");
    if (!dir) return nodes;
    while (dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "lirc", 4) != 0) continue;
        LircNode n;
        n.path = std::string("/dev/") + ent->d_name;
        int fd = -1;
        std::string err;
        if (!open_lirc(n.path, fd, err)) continue;
        if (!get_features(fd, n.features, err)) {
            ::close(fd);
            continue;
        }
        ::close(fd);
        n.name = sysfs_rc_name(n.path);
        if (n.name.empty()) n.name = ent->d_name;
        nodes.push_back(std::move(n));
    }
    closedir(dir);
    return nodes;
}

IrDeviceInfo pick_device(bool want_receive) {
    IrDeviceInfo info;
#if USE_DESKTOP
    (void)want_receive;
    info.present = false;
    info.error = "Onboard IR is device-only (TX GPIO12, RX GPIO13)";
    return info;
#else
    const auto nodes = enumerate_lirc();
    for (const auto& n : nodes) {
        if (want_receive ? can_receive(n.features) : can_send(n.features)) {
            info.present = true;
            info.path = n.path;
            info.name = n.name.empty() ? n.path : n.name;
            info.error.clear();
            return info;
        }
    }
    if (gpio_mem_available()) {
        info.present = true;
        info.path = want_receive ? "gpio13" : "gpio12";
        info.name = want_receive ? "IR RX GPIO13" : "IR TX GPIO12";
        info.error.clear();
        return info;
    }
    info.present = false;
    info.error = want_receive ? "Onboard IR RX GPIO13 unavailable" : "Onboard IR TX GPIO12 unavailable";
    return info;
#endif
}

bool write_all(int fd, const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

IrSendResult send_scancode(int fd, uint16_t address, uint16_t command) {
    IrSendResult r;
    __u32 mode = LIRC_MODE_SCANCODE;
    if (ioctl(fd, LIRC_SET_SEND_MODE, &mode) < 0) {
        r.error = "scancode mode unsupported";
        return r;
    }
    if (ioctl(fd, LIRC_SET_SEND_CARRIER, &kNecCarrier) < 0) {
        // Optional on some drivers.
    }
    lirc_scancode sc{};
    if (address <= 0xFF && command <= 0xFF) {
        sc.rc_proto = RC_PROTO_NEC;
        sc.scancode = (static_cast<__u64>(address & 0xFF) << 8) | (command & 0xFF);
    } else {
        sc.rc_proto = RC_PROTO_NECX;
        sc.scancode = (static_cast<__u64>(address) << 8) | (command & 0xFF);
    }
    if (!write_all(fd, &sc, sizeof(sc))) {
        r.error = "scancode write failed";
        return r;
    }
    r.ok = true;
    return r;
}

IrSendResult send_pulse(int fd, uint16_t address, uint16_t command) {
    IrSendResult r;
    __u32 mode = LIRC_MODE_PULSE;
    if (ioctl(fd, LIRC_SET_SEND_MODE, &mode) < 0) {
        r.error = "pulse mode unsupported";
        return r;
    }
    if (ioctl(fd, LIRC_SET_SEND_CARRIER, &kNecCarrier) < 0) {
        // Optional.
    }
    const auto pulses = encode_nec_pulses(address, command);
    if (!write_all(fd, pulses.data(), pulses.size() * sizeof(__u32))) {
        r.error = "pulse write failed";
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace

IrDeviceInfo read_receiver_info() {
    return pick_device(true);
}

IrDeviceInfo read_sender_info() {
    return pick_device(false);
}

IrSendResult send_nec_packet(uint16_t address, uint16_t command) {
    IrSendResult r;
#if USE_DESKTOP
    (void)address;
    (void)command;
    r.ok = false;
    r.error = "Onboard IR is device-only (TX GPIO12, RX GPIO13)";
    return r;
#else
    const IrDeviceInfo sender = read_sender_info();
    if (!sender.present || sender.path.empty()) {
        r.error = sender.error.empty() ? "Onboard IR TX GPIO12 unavailable" : sender.error;
        return r;
    }
    if (gpio_path(sender.path)) {
        const auto pulses = encode_nec_pulses(address, command);
        return send_pulses_gpio(pulses.data(), pulses.size(), kNecCarrier, nullptr);
    }
    int fd = -1;
    if (!open_lirc(sender.path, fd, r.error)) {
        if (r.error.empty()) r.error = "IR unavailable";
        return r;
    }
    __u32 features = 0;
    if (!get_features(fd, features, r.error)) {
        ::close(fd);
        if (r.error.empty()) r.error = "IR unavailable";
        return r;
    }

    if (features & LIRC_CAN_SEND_SCANCODE) {
        r = send_scancode(fd, address, command);
        if (r.ok) {
            ::close(fd);
            return r;
        }
    }
    if (features & (LIRC_CAN_SEND_PULSE | LIRC_CAN_SEND_MODE2)) {
        r = send_pulse(fd, address, command);
        ::close(fd);
        if (!r.ok && r.error.empty()) r.error = "IR unavailable";
        return r;
    }
    ::close(fd);
    r.ok = false;
    r.error = "IR unavailable";
    return r;
#endif
}

IrSendResult send_raw(const uint32_t* pulses_us, size_t count, uint32_t carrier_hz) {
    IrSendResult r;
#if USE_DESKTOP
    (void)pulses_us;
    (void)count;
    (void)carrier_hz;
    r.ok = false;
    r.error = "Onboard IR is device-only (TX GPIO12, RX GPIO13)";
    return r;
#else
    if (!pulses_us || count == 0) {
        r.error = "empty pulse buffer";
        return r;
    }
    const IrDeviceInfo sender = read_sender_info();
    if (!sender.present || sender.path.empty()) {
        r.error = sender.error.empty() ? "Onboard IR TX GPIO12 unavailable" : sender.error;
        return r;
    }
    if (gpio_path(sender.path)) return send_pulses_gpio(pulses_us, count, carrier_hz, nullptr);
    int fd = -1;
    if (!open_lirc(sender.path, fd, r.error)) {
        if (r.error.empty()) r.error = "IR unavailable";
        return r;
    }
    __u32 features = 0;
    if (!get_features(fd, features, r.error)) {
        ::close(fd);
        if (r.error.empty()) r.error = "IR unavailable";
        return r;
    }
    if (!(features & (LIRC_CAN_SEND_PULSE | LIRC_CAN_SEND_MODE2))) {
        ::close(fd);
        r.error = "pulse send unsupported";
        return r;
    }
    __u32 mode = LIRC_MODE_PULSE;
    if (ioctl(fd, LIRC_SET_SEND_MODE, &mode) < 0) {
        ::close(fd);
        r.error = "pulse mode unsupported";
        return r;
    }
    __u32 carrier = carrier_hz ? carrier_hz : kNecCarrier;
    ioctl(fd, LIRC_SET_SEND_CARRIER, &carrier);

    std::vector<__u32> buf;
    buf.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        __u32 v = pulses_us[i];
        if (v == 0) v = 1;
        if (v > 500000) v = 500000;
        buf.push_back(v);
    }
    // LIRC pulse buffers must end on a pulse (odd count). Drop trailing space if present.
    if ((buf.size() % 2) == 0) buf.pop_back();
    if (buf.empty()) {
        ::close(fd);
        r.error = "empty pulse buffer";
        return r;
    }
    if (!write_all(fd, buf.data(), buf.size() * sizeof(__u32))) {
        ::close(fd);
        r.error = "pulse write failed";
        return r;
    }
    ::close(fd);
    r.ok = true;
    return r;
#endif
}

IrSendResult send_carrier(uint32_t carrier_hz, int duration_ms, bool* cancel) {
    IrSendResult r;
#if USE_DESKTOP
    (void)carrier_hz;
    (void)duration_ms;
    (void)cancel;
    r.ok = false;
    r.error = "Onboard IR is device-only (TX GPIO12, RX GPIO13)";
    return r;
#else
    if (duration_ms <= 0) {
        r.error = "bad duration";
        return r;
    }
    const IrDeviceInfo sender = read_sender_info();
    if (sender.present && gpio_path(sender.path)) {
        if (cancel && *cancel) {
            r.ok = true;
            return r;
        }
        // Slice the carrier so Esc can stop a long test.
        int left = duration_ms;
        while (left > 0) {
            if (cancel && *cancel) {
                r.ok = true;
                return r;
            }
            const int chunk = left < 50 ? left : 50;
            const uint32_t slice[1] = {static_cast<uint32_t>(chunk) * 1000u};
            r = send_pulses_gpio(slice, 1, carrier_hz ? carrier_hz : kNecCarrier, cancel);
            if (!r.ok) return r;
            left -= chunk;
        }
        r.ok = true;
        return r;
    }
    const int slice_ms = 100;
    int left = duration_ms;
    while (left > 0) {
        if (cancel && *cancel) {
            r.ok = true;
            return r;
        }
        const int chunk = left < slice_ms ? left : slice_ms;
        const uint32_t mark = static_cast<uint32_t>(chunk) * 1000u;
        const uint32_t pulses[1] = {mark};
        r = send_raw(pulses, 1, carrier_hz ? carrier_hz : kNecCarrier);
        if (!r.ok) return r;
        left -= chunk;
    }
    r.ok = true;
    return r;
#endif
}

IrReceiverSession::~IrReceiverSession() {
    stop();
}

bool IrReceiverSession::start(uint16_t expected_address, bool filter_address) {
    stop();
    last_error_.clear();
    expected_address_ = expected_address;
    filter_address_ = filter_address;
    mode2_len_ = 0;
    use_scancode_ = false;

#if USE_DESKTOP
    last_error_ = "Onboard IR is device-only (TX GPIO12, RX GPIO13)";
    return false;
#else
    const IrDeviceInfo rx = read_receiver_info();
    if (!rx.present || rx.path.empty()) {
        last_error_ = rx.error.empty() ? "Onboard IR RX GPIO13 unavailable" : rx.error;
        return false;
    }
    if (gpio_path(rx.path)) {
        gpio_stop_.store(false);
        {
            std::lock_guard<std::mutex> lock(gpio_mu_);
            gpio_queue_.clear();
        }
        gpio_mode_ = true;
        try {
            gpio_thread_ = std::thread([this] { gpio_loop(); });
        } catch (...) {
            gpio_mode_ = false;
            last_error_ = "cannot start onboard IR RX";
            return false;
        }
        return true;
    }
    if (!open_lirc(rx.path, fd_, last_error_)) {
        fd_ = -1;
        if (last_error_.empty()) last_error_ = "IR unavailable";
        return false;
    }
    __u32 features = 0;
    if (!get_features(fd_, features, last_error_)) {
        stop();
        if (last_error_.empty()) last_error_ = "IR unavailable";
        return false;
    }

    if (features & LIRC_CAN_REC_SCANCODE) {
        __u32 mode = LIRC_MODE_SCANCODE;
        if (ioctl(fd_, LIRC_SET_REC_MODE, &mode) == 0) {
            use_scancode_ = true;
            return true;
        }
    }
    if (features & LIRC_CAN_REC_MODE2) {
        __u32 mode = LIRC_MODE_MODE2;
        if (ioctl(fd_, LIRC_SET_REC_MODE, &mode) == 0) {
            use_scancode_ = false;
            return true;
        }
    }
    last_error_ = "no supported receive mode";
    stop();
    return false;
#endif
}

void IrReceiverSession::stop() {
    gpio_stop_.store(true);
    if (gpio_thread_.joinable()) gpio_thread_.join();
    gpio_mode_ = false;
    gpio_stop_.store(false);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    mode2_len_ = 0;
}

bool IrReceiverSession::running() const {
    return fd_ >= 0 || gpio_mode_;
}

const std::string& IrReceiverSession::last_error() const {
    return last_error_;
}

void IrReceiverSession::gpio_loop() {
#if !USE_DESKTOP
    GpioMap map;
    std::string err;
    if (!map.open_map(err)) return;
    const uint32_t saved = map.fsel(kGpioRx);
    if (saved <= 1) map.set_fsel(kGpioRx, 0);

    std::vector<unsigned int> samples;
    samples.reserve(128);
    bool last = map.level_high(kGpioRx);
    uint64_t edge = mono_us();
    bool in_frame = false;

    auto push_packet = [this](IrPacket pkt) {
        std::lock_guard<std::mutex> lock(gpio_mu_);
        if (gpio_queue_.size() >= 32) gpio_queue_.erase(gpio_queue_.begin());
        gpio_queue_.push_back(std::move(pkt));
    };

    while (!gpio_stop_.load()) {
        const bool level = map.level_high(kGpioRx);
        const uint64_t now = mono_us();
        const uint64_t dur = now - edge;
        if (level == last) {
            // Demodulated RX idles high. A long high ends the frame.
            if (in_frame && last && dur > 12000) {
                if (samples.size() >= 6) {
                    IrPacket decoded;
                    if (decode_nec_from_mode2(samples.data(), samples.size(), decoded) && decoded.valid) {
                        if (!filter_address_ || decoded.address == expected_address_) push_packet(decoded);
                    } else {
                        IrPacket raw;
                        raw.activity = true;
                        raw.pulses = static_cast<unsigned>(samples.size());
                        push_packet(raw);
                    }
                }
                samples.clear();
                in_frame = false;
            }
            continue;
        }
        if (!last) {
            if (dur > 50 && dur < 100000) {
                samples.push_back(static_cast<unsigned>(dur));
                in_frame = true;
            }
        } else if (in_frame && dur > 50 && dur < 20000) {
            samples.push_back(static_cast<unsigned>(dur));
        }
        edge = now;
        last = level;
        if (samples.size() > 200) {
            samples.clear();
            in_frame = false;
        }
    }
    if (saved <= 1) map.set_fsel(kGpioRx, saved);
    map.close_map();
#else
    (void)0;
#endif
}

IrPacket IrReceiverSession::poll() {
    IrPacket pkt;
    if (gpio_mode_) {
        std::lock_guard<std::mutex> lock(gpio_mu_);
        if (gpio_queue_.empty()) return pkt;
        pkt = gpio_queue_.front();
        gpio_queue_.erase(gpio_queue_.begin());
        return pkt;
    }
    if (fd_ < 0) return pkt;

    if (use_scancode_) {
        lirc_scancode sc{};
        const ssize_t n = ::read(fd_, &sc, sizeof(sc));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                last_error_ = "read failed";
            }
            return pkt;
        }
        if (static_cast<size_t>(n) < sizeof(sc)) return pkt;
        if (sc.flags & LIRC_SCANCODE_FLAG_REPEAT) return pkt;
        pkt = packet_from_scancode(sc);
        if (!pkt.valid) return {};
        if (filter_address_ && pkt.address != expected_address_) return {};
        return pkt;
    }

    // MODE2: read available __u32 samples non-blocking, decode when possible.
    __u32 sample = 0;
    for (;;) {
        const ssize_t n = ::read(fd_, &sample, sizeof(sample));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) last_error_ = "read failed";
            break;
        }
        if (static_cast<size_t>(n) < sizeof(sample)) break;
        if (LIRC_IS_OVERFLOW(sample) || LIRC_IS_TIMEOUT(sample)) {
            mode2_len_ = 0;
            continue;
        }
        if (!(LIRC_IS_PULSE(sample) || LIRC_IS_SPACE(sample))) continue;
        if (mode2_len_ >= (sizeof(mode2_buf_) / sizeof(mode2_buf_[0]))) {
            // Drop oldest half to keep receiving.
            const size_t keep = mode2_len_ / 2;
            std::memmove(mode2_buf_, mode2_buf_ + (mode2_len_ - keep), keep * sizeof(unsigned int));
            mode2_len_ = keep;
        }
        mode2_buf_[mode2_len_++] = LIRC_VALUE(sample);

        IrPacket decoded;
        if (decode_nec_from_mode2(mode2_buf_, mode2_len_, decoded) && decoded.valid) {
            mode2_len_ = 0;
            if (filter_address_ && decoded.address != expected_address_) continue;
            return decoded;
        }
    }
    return pkt;
}

} // namespace platform
