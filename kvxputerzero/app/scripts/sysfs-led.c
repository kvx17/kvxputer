/*
 * Setuid helper: write Cap/Grove LED-class brightness without sudo.
 * Allowed names only. "1" maps to max_brightness when present.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int allowed(const char* name) {
    return strcmp(name, "grove_fun") == 0 || strcmp(name, "grove_5v_out") == 0 ||
           strcmp(name, "ext_5v_out") == 0 || strcmp(name, "ext_usb_gpio_fun") == 0;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: sysfs-led <grove_fun|grove_5v_out|ext_5v_out|ext_usb_gpio_fun> <0|1>\n");
        return 2;
    }
    const char* name = argv[1];
    const char* value = argv[2];
    if (!allowed(name) || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) {
        fprintf(stderr, "sysfs-led: refused\n");
        return 2;
    }

    /* Drop privileges only after becoming root via setuid bit. */
    if (seteuid(0) != 0) {
        perror("sysfs-led: seteuid(0)");
        return 1;
    }

    char payload[16] = "0";
    if (strcmp(value, "1") == 0) {
        char max_path[256];
        snprintf(max_path, sizeof(max_path), "/sys/class/leds/%s/max_brightness", name);
        FILE* mf = fopen(max_path, "r");
        int max_v = 0;
        if (mf && fscanf(mf, "%d", &max_v) == 1 && max_v > 0) {
            snprintf(payload, sizeof(payload), "%d", max_v);
        } else {
            snprintf(payload, sizeof(payload), "255");
        }
        if (mf) fclose(mf);
    }

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/brightness", name);
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("sysfs-led: open");
        return 1;
    }
    const ssize_t n = write(fd, payload, strlen(payload));
    close(fd);
    if (n != (ssize_t)strlen(payload)) {
        fprintf(stderr, "sysfs-led: short write\n");
        return 1;
    }
    return 0;
}
