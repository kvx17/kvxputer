#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tools::file_share {

struct Auth {
    std::string user;
    std::string password;
};

struct ListenResult {
    int fd{-1};
    uint16_t port{0};
    std::string error;
    std::string occupant; // process that held a skipped port, when known
};

// Bind 0.0.0.0 on the first free port in [first, last]. Caller owns the fd.
ListenResult listen_first_free(uint16_t first = 8080, uint16_t last = 8099);

// Best-effort: name the process listening on port via /proc/net/tcp + /proc/*/fd.
std::string port_occupant(uint16_t port);

Auth load_or_create_auth();
std::string lan_url(uint16_t port, std::string& error);

struct Snapshot {
    bool running{false};
    uint16_t port{0};
    std::string url;
    std::string error;
    std::string occupant;
    Auth auth;
};

// Authenticated (optional) HTTP file browser over root. One accept-at-a-time worker.
class Server {
public:
    Server(std::string root, bool writable, bool require_auth);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();
    void stop();
    Snapshot snapshot() const;

private:
    void run();
    void handle(int fd);

    std::string root_;
    bool writable_{false};
    bool require_auth_{false};
    Auth auth_;

    mutable std::mutex mu_;
    Snapshot snap_;
    std::vector<std::string> sessions_;

    std::atomic<bool> cancel_{false};
    std::thread thread_;
    bool started_{false};
};

} // namespace tools::file_share
