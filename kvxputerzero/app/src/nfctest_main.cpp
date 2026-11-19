// On-device Cap ST25 test harness — same St25Session code path as the UI.
//   sudo kvxputerzero-nfctest id
//   sudo kvxputerzero-nfctest read [seconds]
//   sudo kvxputerzero-nfctest emulate [url] [seconds]
// Sets KVX_NFC_TRACE=1 so st25r3916.cpp logs each step to stderr.

#include "nfc_dump.h"
#include "nfc_ndef.h"
#include "st25r3916.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

bool g_cancel = false;

void on_sigint(int) { g_cancel = true; }

int usage() {
    std::fprintf(stderr,
                 "usage: kvxputerzero-nfctest id | read [seconds] | emulate [url] [seconds]\n"
                 "Stop the kvxputerzero UI first (it holds the same SPI/GPIO lines).\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    ::setenv("KVX_NFC_TRACE", "1", 0);
    std::signal(SIGINT, on_sigint);
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    platform::St25Session s;
    std::string err;
    if (!s.open({}, err)) {
        std::fprintf(stderr, "open failed: %s\n", err.c_str());
        return 1;
    }

    int rc = 0;
    if (cmd == "id") {
        std::string line;
        if (s.identity(line, err)) std::printf("%s\n", line.c_str());
        else {
            std::fprintf(stderr, "identity failed: %s\n", err.c_str());
            rc = 1;
        }
    } else if (cmd == "read") {
        const int secs = argc > 2 ? std::atoi(argv[2]) : 15;
        std::printf("Reading for %ds — hold tag flat on Cap NFC coil (Ctrl-C to stop)\n", secs);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(secs);
        rc = 3;
        int attempt = 0;
        while (!g_cancel && std::chrono::steady_clock::now() < deadline) {
            tools::NfcDump d;
            err.clear();
            ++attempt;
            if (s.read_dump(d, err, &g_cancel)) {
                std::printf("attempt %d OK\n%s\n", attempt, tools::nfc_dump_summary(d).c_str());
                rc = 0;
                break;
            }
            std::printf("attempt %d: %s\n", attempt, err.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } else if (cmd == "emulate") {
        const std::string url = argc > 2 ? argv[2] : "https://github.com/kvx17/kvxputer";
        const int secs = argc > 3 ? std::atoi(argv[3]) : 30;
        std::printf("Emulating NTAG URL %s for %ds — hold phone / Flipper (NFC Read) on Cap coil\n", url.c_str(),
                    secs);
        if (s.emulate_ndef_tlv(tools::ndef_tlv_uri(url), err, &g_cancel, secs * 1000)) {
            std::printf("reader contacted\n");
        } else {
            std::printf("emulate: %s\n", err.c_str());
            rc = 3;
        }
    } else {
        rc = usage();
    }
    s.close();
    return rc;
}
