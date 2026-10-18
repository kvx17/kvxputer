/**
 * @file wigle.cpp
 * @brief Wigle connector with archive / thin quarantine / optional gzip
 */

#include "wigle.h"
#include "root/input/mykeyboard.h"
#include "root/net/wifi_common.h"
#include "root/storage/sd_functions.h"
#include "root/ui/display.h"
#include <vector>

// ESP32 Arduino exposes zlib.h for inflate paths, but deflate is not linked by default.
// Keep uncompressed WiGLE upload; gzip can be re-enabled when a compression lib is added.
#define WIGLE_HAS_ZLIB 0

#define CBUFLEN 1024

Wigle::Wigle() {}

Wigle::~Wigle() {}

bool Wigle::_check_token() {
    if (kvxConfig.wigleBasicToken == "") {
        displayError("Wigle token not found", true);
        return false;
    }

    auth_header = "Basic " + kvxConfig.wigleBasicToken;

    if (!wifiConnected) wifiConnectMenu();

    return true;
}

bool Wigle::get_user() {
    if (!_check_token()) return false;

    display_banner();
    padprintln("Connecting to Wigle...");

    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host, 443)) return false;

    client.println("GET /api/v2/profile/user HTTP/1.0");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println("User-Agent: kvxputer.wardriving");
    client.print("Authorization: ");
    client.println(auth_header);
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, client);

    client.stop();

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return false;
    }

    String userid = doc["userid"];
    wigle_user = userid;

    return true;
}

void Wigle::display_banner() {
    drawMainBorderWithTitle("Wigle Upload");
    padprintln("\n");
}

void Wigle::dump_wigle_info() {
    display_banner();
    padprintln("Logged into Wigle as " + wigle_user);
    padprintln("");
}

void Wigle::send_upload_headers(
    WiFiClientSecure &client, const String &filename, int filesize, const String &boundary
) {
    int cd_header_len = 147 - 45 - 8 + filename.length() + 2 * (boundary.length() + 2);

    client.println("POST /api/v2/file/upload HTTP/1.0");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println("User-Agent: kvxputer.wardriving");
    client.print("Authorization: ");
    client.println(auth_header);
    client.print("Content-Type: multipart/form-data; boundary=");
    client.println(boundary);
    client.print("Content-Length: ");
    client.println(filesize + cd_header_len);
    client.println();

    client.println("--" + boundary);
    client.print("Content-Disposition: form-data; name=\"file\"; filename=\"");
    client.print(filename);
    client.println("\"");
    if (filename.endsWith(".gz")) client.println("Content-Type: application/gzip");
    else client.println("Content-Type: text/csv");
    client.println();
}

bool Wigle::upload(FS *fs, const String &filepath, bool auto_delete) {
    display_banner();

    if (!fs || !get_user()) return false;

    dump_wigle_info();

    File file = fs->open(filepath);
    if (!file) {
        displayError("Failed to open Wigle file", true);
        return false;
    }

    if (!_upload_file(file, "Uploading...")) {
        file.close();
        displayError("File upload error", true);
        return false;
    }

    file.close();
    if (auto_delete) fs->remove(filepath);

    displaySuccess("File upload success", true);
    return true;
}

bool Wigle::upload_all(FS *fs, const String &folder, bool auto_delete) {
    return upload_folder(fs, folder, auto_delete, false, 0);
}

int Wigle::countDistinctSweeps(FS *fs, const String &filepath) {
    File f = fs->open(filepath, FILE_READ);
    if (!f) return 0;
    std::vector<String> seen;
    seen.reserve(64);
    int lineNo = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        lineNo++;
        if (lineNo <= 2) continue; // skip headers
        // FirstSeen is field index 3 (0-based) in WiGLE CSV
        int commas = 0;
        int start = 0;
        String firstSeen;
        bool inQuotes = false;
        for (int i = 0; i <= (int)line.length(); i++) {
            char c = i < (int)line.length() ? line[i] : ',';
            if (c == '"') inQuotes = !inQuotes;
            if ((c == ',' && !inQuotes) || i == (int)line.length()) {
                if (commas == 3) {
                    firstSeen = line.substring(start, i);
                    firstSeen.trim();
                    break;
                }
                commas++;
                start = i + 1;
            }
        }
        if (firstSeen.isEmpty()) continue;
        bool found = false;
        for (const auto &s : seen) {
            if (s == firstSeen) {
                found = true;
                break;
            }
        }
        if (!found) seen.push_back(firstSeen);
    }
    f.close();
    return (int)seen.size();
}

bool Wigle::gzipFileToBuffer(FS *fs, const String &filepath, std::vector<uint8_t> &out) {
#if WIGLE_HAS_ZLIB
    File f = fs->open(filepath, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > 2 * 1024 * 1024) {
        f.close();
        return false;
    }
    std::vector<uint8_t> raw(sz);
    if (f.read(raw.data(), sz) != (int)sz) {
        f.close();
        return false;
    }
    f.close();

    z_stream strm = {};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;

    out.resize(sz + sz / 8 + 64);
    strm.next_in = raw.data();
    strm.avail_in = sz;
    strm.next_out = out.data();
    strm.avail_out = out.size();
    int ret = deflate(&strm, Z_FINISH);
    size_t produced = strm.total_out;
    deflateEnd(&strm);
    if (ret != Z_STREAM_END) return false;
    out.resize(produced);
    return true;
#else
    (void)fs;
    (void)filepath;
    (void)out;
    return false;
#endif
}

bool Wigle::_upload_bytes(
    const uint8_t *data, size_t len, const String &filename, const String &upload_message
) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host, 443)) {
        displayError("Wigle API connection failed", true);
        return false;
    }

    String boundary = "KVX";
    boundary.concat(esp_random());
    send_upload_headers(client, filename, (int)len, boundary);

    size_t sent = 0;
    while (sent < len) {
        size_t chunk = min((size_t)CBUFLEN, len - sent);
        client.write(data + sent, chunk);
        sent += chunk;
        progressHandler((float)sent / (float)len * 100.0f, 100, upload_message);
    }

    client.println();
    client.print("--");
    client.print(boundary);
    client.println("--");
    client.println();
    client.flush();

    String serverres = "";
    while (client.connected()) {
        if (client.available()) {
            char c = client.read();
            serverres.concat(c);
        }
        if (serverres.length() > 1024) break;
    }
    client.stop();
    return (serverres.indexOf("\"success\":true") > -1);
}

bool Wigle::upload_folder(
    FS *fs, const String &folder, bool delete_after, bool compress, int min_sweeps
) {
    Serial.println("Wigle upload folder: " + folder);
    display_banner();

    if (!fs || !get_user()) return false;
    dump_wigle_info();

    String uploadedDir = folder + "/uploaded";
    String thinDir = uploadedDir + "/thin";
    if (!fs->exists(uploadedDir)) fs->mkdir(uploadedDir);
    if (!fs->exists(thinDir)) fs->mkdir(thinDir);

    File root = fs->open(folder);
    if (!root || !root.isDirectory()) return false;

    int uploaded = 0;
    int thin = 0;
    int failed = 0;

    std::vector<String> files;
    while (true) {
        bool isDir = false;
        String fullPath = root.getNextFileName(&isDir);
        if (fullPath == "") break;
        if (isDir) continue;
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (!nameOnly.endsWith(".csv") && !nameOnly.endsWith(".CSV")) continue;
        if (nameOnly == "stats.csv") continue;
        files.push_back(fullPath);
    }
    root.close();

    for (size_t i = 0; i < files.size(); i++) {
        const String &fullPath = files[i];
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);

        if (min_sweeps > 0) {
            int sweeps = countDistinctSweeps(fs, fullPath);
            if (sweeps < min_sweeps) {
                String dest = thinDir + "/" + nameOnly;
                fs->rename(fullPath, dest);
                thin++;
                continue;
            }
        }

        bool ok = false;
        if (compress) {
            std::vector<uint8_t> gz;
            if (gzipFileToBuffer(fs, fullPath, gz)) {
                ok = _upload_bytes(gz.data(), gz.size(), nameOnly + ".gz", "Uploading " + String(i + 1));
                if (ok) {
                    if (delete_after) fs->remove(fullPath);
                    else {
                        String dest = uploadedDir + "/" + nameOnly + ".gz";
                        File out = fs->open(dest, FILE_WRITE);
                        if (out) {
                            out.write(gz.data(), gz.size());
                            out.close();
                            fs->remove(fullPath);
                        }
                    }
                }
            }
        }
        if (!ok) {
            File file = fs->open(fullPath);
            if (!file) {
                failed++;
                continue;
            }
            ok = _upload_file(file, "Uploading " + String(i + 1));
            file.close();
            if (ok) {
                if (delete_after) fs->remove(fullPath);
                else {
                    String dest = uploadedDir + "/" + nameOnly;
                    fs->rename(fullPath, dest);
                }
            }
        }
        if (ok) uploaded++;
        else failed++;
    }

    String msg = String(uploaded) + " up";
    if (thin) msg += ", " + String(thin) + " thin";
    if (failed) msg += ", " + String(failed) + " fail";
    displaySuccess(msg, true);
    return failed == 0;
}

bool Wigle::_upload_file(File file, const String &upload_message) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host, 443)) {
        displayError("Wigle API connection failed", true);
        return false;
    }

    String filename = file.name();
    int filesize = file.size();
    String boundary = "KVX";
    boundary.concat(esp_random());

    send_upload_headers(client, filename, filesize, boundary);

    byte cbuf[CBUFLEN];
    float percent = 0;
    while (file.available()) {
        long bytes_available = file.available();
        int toread = CBUFLEN;
        if (bytes_available < CBUFLEN) toread = bytes_available;
        file.read(cbuf, toread);
        client.write(cbuf, toread);
        percent = ((float)file.position() / (float)file.size()) * 100;
        progressHandler(percent, 100, upload_message);
    }

    client.println();
    client.print("--");
    client.print(boundary);
    client.println("--");
    client.println();
    client.flush();

    String serverres = "";
    while (client.connected()) {
        if (client.available()) {
            char c = client.read();
            serverres.concat(c);
        }
        if (serverres.length() > 1024) break;
    }

    client.stop();

    return (serverres.indexOf("\"success\":true") > -1);
}
