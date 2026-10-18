/**
 * @file wigle.h
 * @brief Wigle connector
 */

#ifndef __WIGLE_H__
#define __WIGLE_H__

#include <WiFiClientSecure.h>
#include <globals.h>
#include <vector>

class Wigle {
public:
    Wigle();
    ~Wigle();

    bool get_user(void);
    bool upload(FS *fs, const String &filepath, bool auto_delete = true);
    bool upload_all(FS *fs, const String &folder, bool auto_delete = true);
    // Extended upload: optional gzip, archive to uploaded/, thin quarantine by distinct FirstSeen sweeps.
    bool upload_folder(
        FS *fs,
        const String &folder,
        bool delete_after,
        bool compress,
        int min_sweeps
    );
    void send_upload_headers(
        WiFiClientSecure &client, const String &filename, int filesize, const String &boundary
    );
    void display_banner(void);
    void dump_wigle_info(void);

private:
    String wigle_user;
    String auth_header;
    const char *host = "api.wigle.net";

    bool _check_token(void);
    bool _upload_file(File file, const String &upload_message);
    bool _upload_bytes(
        const uint8_t *data, size_t len, const String &filename, const String &upload_message
    );
    int countDistinctSweeps(FS *fs, const String &filepath);
    bool gzipFileToBuffer(FS *fs, const String &filepath, std::vector<uint8_t> &out);
};

#endif
