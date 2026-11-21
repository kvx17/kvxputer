#include "camera_session.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

int main() {
    platform::CameraSession session;
    std::string err;
    if (!session.start(err)) {
        std::printf("start fail %s\n", err.c_str());
        return 1;
    }
    if (session.backend_name() != "simulation") {
        std::printf("backend %s\n", session.backend_name().c_str());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    session.set_filter(tools::camfilter::Filter::Noir);
    session.set_brightness(1.2f);

    std::string path;
    if (!session.capture_still(path, err)) {
        std::printf("still fail %s\n", err.c_str());
        return 1;
    }
    std::printf("still %s exists=%d\n", path.c_str(), static_cast<int>(std::filesystem::exists(path)));

    std::string vpath;
    if (!session.start_recording(vpath, err)) {
        std::printf("rec fail %s\n", err.c_str());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    session.stop_recording();
    const auto size = std::filesystem::exists(vpath) ? std::filesystem::file_size(vpath) : 0;
    std::printf("avi %s exists=%d size=%llu\n", vpath.c_str(), static_cast<int>(std::filesystem::exists(vpath)),
                static_cast<unsigned long long>(size));
    session.stop();

    std::vector<uint16_t> px;
    int w = 0;
    int h = 0;
    std::string e2;
    if (!platform::decode_image_rgb565(path, px, w, h, e2)) {
        std::printf("decode fail %s\n", e2.c_str());
        return 1;
    }
    std::printf("decode %dx%d pixels=%zu\n", w, h, px.size());
    return 0;
}
