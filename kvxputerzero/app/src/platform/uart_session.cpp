#include "uart_session.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace platform {
namespace {

bool looks_like_console(const std::string& path) {
    // Refuse common login consoles.
    if (path.find("ttyAMA") != std::string::npos) return true;
    if (path.find("console") != std::string::npos) return true;
    if (path == "/dev/ttyS0" || path == "/dev/ttyS1") return true;
    if (path.find("pts/") != std::string::npos) return true;
    return false;
}

speed_t baud_const(int baud) {
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: return B115200;
    }
}

} // namespace

UartSession::~UartSession() {
    close();
}

std::string UartSession::default_grove_uart() {
    // Cardputer Zero Grove often shows as ttyS* or a USB-serial; prefer ttyUSB0 then ttyS2.
    if (::access("/dev/ttyUSB0", F_OK) == 0) return "/dev/ttyUSB0";
    if (::access("/dev/ttyS2", F_OK) == 0) return "/dev/ttyS2";
    if (::access("/dev/serial0", F_OK) == 0) return "/dev/serial0";
    return "/dev/ttyUSB0";
}

bool UartSession::open(const std::string& path, int baud, std::string& error) {
    error.clear();
    close();
    if (looks_like_console(path)) {
        error = "refusing login console";
        return false;
    }
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        error = std::string("open failed: ") + path;
        return false;
    }
    termios tio{};
    if (tcgetattr(fd_, &tio) != 0) {
        error = "tcgetattr failed";
        close();
        return false;
    }
    cfmakeraw(&tio);
    const speed_t sp = baud_const(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
        error = "tcsetattr failed";
        close();
        return false;
    }
    return true;
}

void UartSession::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool UartSession::is_open() const {
    return fd_ >= 0;
}

bool UartSession::write_bytes(const uint8_t* data, size_t len, std::string& error) {
    error.clear();
    if (fd_ < 0) {
        error = "not open";
        return false;
    }
    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd_, data + off, len - off);
        if (n < 0) {
            if (errno == EAGAIN) continue;
            error = "write failed";
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

bool UartSession::read_available(std::string& out, std::string& error) {
    error.clear();
    if (fd_ < 0) {
        error = "not open";
        return false;
    }
    char buf[256];
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n > 0) {
        out.append(buf, static_cast<size_t>(n));
        return true;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        error = "read failed";
        return false;
    }
    return true;
}

} // namespace platform
