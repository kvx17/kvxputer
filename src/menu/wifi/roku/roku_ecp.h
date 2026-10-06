#pragma once

#include <Arduino.h>
#include <vector>

struct RokuDevice {
    String ip;
    String name;
    String model;
    String serial;
};

struct RokuApp {
    String id;
    String name;
};

enum RokuEcpStatus {
    ROKU_ECP_OK = 0,
    ROKU_ECP_UNREACHABLE,
    ROKU_ECP_FORBIDDEN, // Control by mobile apps disabled
    ROKU_ECP_WIFI_DOWN,
    ROKU_ECP_CANCELLED,
};

class RokuEcp {
public:
    void setBase(const String &ip);
    const String &baseUrl() const { return _base; }
    const String &ip() const { return _ip; }

    RokuEcpStatus postKey(const char *key);
    RokuEcpStatus launch(const String &appId);
    RokuEcpStatus queryDeviceInfo(RokuDevice &out, int timeoutMs = 1500);
    RokuEcpStatus queryApps(std::vector<RokuApp> &out, int timeoutMs = 2500);

    static std::vector<RokuDevice> ssdpDiscover(unsigned long timeoutMs = 2000);
    static String statusMessage(RokuEcpStatus st);

private:
    String _ip;
    String _base;

    RokuEcpStatus request(const char *method, const String &path, String *bodyOut, int timeoutMs);
    static String scrapeTag(const String &xml, const char *tag);
    static void scrapeApps(const String &xml, std::vector<RokuApp> &out);
    static String extractIpFromLocation(const String &loc);
    static String headerValue(const String &msg, const char *name);
};
