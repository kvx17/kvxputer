#include "shell_util.h"

#include <cstdio>
#include <sys/wait.h>

namespace platform {

int run_shell(const std::string& cmd, std::string& output) {
    output.clear();
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return -1;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) output += buf;
    const int rc = pclose(fp);
    if (rc < 0) return -1;
    return WEXITSTATUS(rc);
}

} // namespace platform
