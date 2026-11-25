#pragma once

// File capabilities on this helper do not pass to programs it starts (iw, ip, nmcli).
// Copy the network capabilities into the ambient set so those children keep them.

#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

inline void pass_net_caps_to_children() {
    __user_cap_header_struct hdr{};
    __user_cap_data_struct data[2]{};
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;
    if (syscall(SYS_capget, &hdr, data) != 0) return;

    const __u32 need = (1u << CAP_NET_ADMIN) | (1u << CAP_NET_RAW);
    const __u32 have = data[0].permitted & need;
    if (have == 0) return;

    data[0].inheritable |= have;
    data[0].effective |= have;
    if (syscall(SYS_capset, &hdr, data) != 0) return;

    if (have & (1u << CAP_NET_ADMIN)) prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_ADMIN, 0, 0);
    if (have & (1u << CAP_NET_RAW)) prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_RAW, 0, 0);
}
