#pragma once

// Shared cores for the HTTP recon tools (web_crawler, cctv). Pure network logic so
// the GUI worker (tool_screens) and the CLI runner (net_runners) stay in sync.

#include <functional>
#include <string>
#include <vector>

namespace platform {

struct ReconStats {
    int tried{0};
    int hits{0};
};

// Progress / result sink and a cancel predicate, supplied by each front-end.
using ReconEmit = std::function<void(const std::string&)>;
using ReconCancel = std::function<bool()>;

// GET base + each path; a path that answers 2xx/3xx is reported as a hit.
// `base` may be a bare host[:port] or a full URL; a trailing slash is trimmed.
ReconStats web_crawl(const std::string& base, const std::vector<std::string>& paths, const ReconEmit& emit,
                     const ReconCancel& cancel);

// Sweep the /24 that contains `seed_ip` (x.y.z.1 .. x.y.z.254) over HTTP. Each host that
// answers is reported; `cctv_paths` that also answer flag it as a likely camera.
ReconStats http_sweep(const std::string& seed_ip, const std::vector<std::string>& cctv_paths,
                      const ReconEmit& emit, const ReconCancel& cancel);

// Built-in fallbacks used when no wordlist / path file is present.
const std::vector<std::string>& default_crawler_paths();
const std::vector<std::string>& default_cctv_paths();

// Read non-empty, non-comment lines from a file; empty vector when it is missing.
std::vector<std::string> read_path_list(const std::string& file);

} // namespace platform
