#include "camera_filter.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

int main() {
    using tools::camfilter::Filter;
    std::set<std::string> names;
    for (int i = 0; i < static_cast<int>(Filter::Count); ++i) {
        const auto f = static_cast<Filter>(i);
        const char* name = tools::camfilter::filter_name(f);
        if (!name || !name[0]) {
            std::fprintf(stderr, "empty name at %d\n", i);
            return 1;
        }
        if (!names.insert(name).second) {
            std::fprintf(stderr, "duplicate name %s\n", name);
            return 1;
        }
        constexpr int w = 8;
        constexpr int h = 8;
        std::vector<uint16_t> rgb565(w * h, 0x07E0);
        std::vector<uint8_t> rgb888(w * h * 3, 128);
        tools::camfilter::apply_rgb565(rgb565, w, h, f);
        tools::camfilter::apply_rgb888(rgb888, w, h, f);
        if (rgb565.size() != static_cast<size_t>(w * h) || rgb888.size() != static_cast<size_t>(w * h * 3)) {
            std::fprintf(stderr, "size change for %s\n", name);
            return 1;
        }
    }

    auto f = Filter::Original;
    for (int i = 0; i < static_cast<int>(Filter::Count); ++i) {
        f = tools::camfilter::step_filter(f, 1);
    }
    if (f != Filter::Original) {
        std::fprintf(stderr, "step wrap failed forward\n");
        return 1;
    }
    for (int i = 0; i < static_cast<int>(Filter::Count); ++i) {
        f = tools::camfilter::step_filter(f, -1);
    }
    if (f != Filter::Original) {
        std::fprintf(stderr, "step wrap failed backward\n");
        return 1;
    }

    std::printf("camera_filter_selftest ok (%zu filters)\n", names.size());
    return 0;
}
