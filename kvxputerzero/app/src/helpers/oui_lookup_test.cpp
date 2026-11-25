#include "oui_lib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && argv[1] && argv[1][0]) {
        tools::oui::set_table_path(argv[1]);
    }
    if (!tools::oui::ensure_loaded()) {
        return fail("could not load oui_table.bin");
    }

    // 24-bit Cisco
    {
        const auto m = tools::oui::lookup("00:00:0C:11:22:33");
        if (!m.found) return fail("Cisco 00:00:0C not found");
        if (m.bit_length != 24) return fail("Cisco should be 24-bit");
        if (m.vendor.find("Cisco") == std::string::npos) return fail("Cisco vendor name");
    }

    // 36-bit IAB must beat a shorter MA-L covering the same start
    {
        const auto m = tools::oui::lookup("00:50:C2:00:00:11");
        if (!m.found) return fail("IAB 00:50:C2:00:0 not found");
        if (m.bit_length < 28) return fail("IAB should prefer longer than 24-bit");
        if (m.vendor.find("T.L.S") == std::string::npos && m.vendor.find("TLS") == std::string::npos &&
            m.vendor.empty()) {
            // Named IAB for 00:50:C2:00:0 is T.L.S. Corp.
            return fail("expected T.L.S. Corp. for 00:50:C2:00:0");
        }
        if (m.vendor.find("T.L.S") == std::string::npos) {
            return fail(("unexpected vendor: " + m.vendor).c_str());
        }
    }

    // Private /3:6 block
    {
        const auto m = tools::oui::lookup("00:1B:C5:00:10:AB");
        if (!m.found) return fail("private 00:1B:C5:00:10 not found");
        if (!m.is_private) return fail("expected private registration");
        if (m.bit_length != 36) return fail("private /3:6 should be 36-bit");
    }

    // Locally administered: no vendor required; lookup may miss
    {
        const auto m = tools::oui::lookup("02:00:00:00:00:01");
        // May or may not match a private prefix; just ensure no crash.
        (void)m;
    }

    // OUI-only must not claim a 36-bit match
    {
        const auto m = tools::oui::lookup("00:50:C2");
        if (!m.found) return fail("00:50:C2 OUI missing");
        if (m.bit_length > 24) return fail("OUI-only must stay at 24-bit or less");
    }

    std::printf("oui_lookup_test: ok\n");
    return 0;
}
