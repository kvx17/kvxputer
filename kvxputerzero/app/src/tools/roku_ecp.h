#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace tools::roku {

enum class Status {
    Ok = 0,
    Unreachable,
    Forbidden, // Control by mobile apps disabled
    Cancelled,
};

struct Device {
    std::string ip;
    std::string name;
    std::string model;
    std::string serial;
    bool supports_find_remote{false};
    bool headphones_connected{false};
};

struct App {
    std::string id;
    std::string name;
};

struct AudioInfo {
    bool muted{false};
    int volume{-1};
    bool headphones_connected{false};
    std::vector<std::string> destinations;
    std::string raw_note; // short human status when query fails or is sparse
};

const char* status_message(Status st);

// Synchronous helpers (blocking; call from a worker thread).
std::vector<Device> ssdp_discover(int timeout_ms, const std::atomic<bool>* cancel = nullptr);
Status query_device_info(const std::string& ip, Device& out, int timeout_sec = 2);
Status query_apps(const std::string& ip, std::vector<App>& out, int timeout_sec = 3);
Status query_audio_device(const std::string& ip, AudioInfo& out, int timeout_sec = 2);
Status launch_app(const std::string& ip, const std::string& app_id, int timeout_sec = 2);
Status keypress(const std::string& ip, const std::string& key, int timeout_sec = 2);
Status keydown(const std::string& ip, const std::string& key, int timeout_sec = 2);
Status keyup(const std::string& ip, const std::string& key, int timeout_sec = 2);

// Build Lit_ key for printable ASCII / URL-encoded UTF-8 byte.
std::string lit_key(char c);

// Ordered background queue for keypress / keydown / keyup.
class CommandQueue {
public:
    using ResultFn = std::function<void(const std::string& key, Status st)>;

    CommandQueue();
    ~CommandQueue();

    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    void set_ip(std::string ip);
    std::string ip() const;

    void enqueue_press(std::string key);
    void enqueue_down(std::string key);
    void enqueue_up(std::string key);

    // Release every key still held (called on destroy / device change).
    void release_held();

    void set_result_callback(ResultFn cb);

private:
    enum class Kind { Press, Down, Up };

    struct Job {
        Kind kind;
        std::string key;
    };

    void worker_loop();
    void push(Job job);

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<Job> q_;
    std::string ip_;
    std::vector<std::string> held_;
    ResultFn result_cb_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

} // namespace tools::roku
