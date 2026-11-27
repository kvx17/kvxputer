#include "weather.h"

#include "http_get.h"
#include "user_paths.h"

#define JSMN_STATIC
#include "../tools/third_party/jsmn/jsmn.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace platform {
namespace {

// ---- jsmn navigation helpers (flat token array, no parent links) ------------

bool tok_eq(const char* js, const jsmntok_t& t, const char* s) {
    const int len = t.end - t.start;
    return t.type == JSMN_STRING && static_cast<int>(std::strlen(s)) == len &&
           std::strncmp(js + t.start, s, static_cast<size_t>(len)) == 0;
}

// Index one past the subtree rooted at token i.
int tok_end(const jsmntok_t* t, int i) {
    if (t[i].type == JSMN_OBJECT) {
        int j = i + 1;
        for (int m = 0; m < t[i].size; ++m) {
            j = tok_end(t, j);  // key
            j = tok_end(t, j);  // value
        }
        return j;
    }
    if (t[i].type == JSMN_ARRAY) {
        int j = i + 1;
        for (int m = 0; m < t[i].size; ++m) j = tok_end(t, j);
        return j;
    }
    return i + 1;
}

// Value token index for `key` inside object token obj_i, or -1.
int obj_get(const char* js, const jsmntok_t* t, int obj_i, const char* key) {
    if (obj_i < 0 || t[obj_i].type != JSMN_OBJECT) return -1;
    int j = obj_i + 1;
    for (int m = 0; m < t[obj_i].size; ++m) {
        const int k = j;
        const int v = tok_end(t, k);
        if (tok_eq(js, t[k], key)) return v;
        j = tok_end(t, v);
    }
    return -1;
}

std::string tok_str(const char* js, const jsmntok_t* t, int i) {
    if (i < 0 || t[i].type != JSMN_STRING) return {};
    return std::string(js + t[i].start, static_cast<size_t>(t[i].end - t[i].start));
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out += "%20";
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

double tok_num(const char* js, const jsmntok_t* t, int i) {
    if (i < 0 || t[i].type != JSMN_PRIMITIVE) return 0.0;
    // "null"/"true"/"false" -> 0; numbers -> strtod.
    const char c = js[t[i].start];
    if (c == 'n' || c == 't' || c == 'f') return 0.0;
    return std::strtod(js + t[i].start, nullptr);
}

// Read a numeric array (value token arr_i) into out, up to `cap` entries.
void read_num_array(const char* js, const jsmntok_t* t, int arr_i, std::vector<double>& out,
                    size_t cap) {
    out.clear();
    if (arr_i < 0 || t[arr_i].type != JSMN_ARRAY) return;
    int j = arr_i + 1;
    for (int m = 0; m < t[arr_i].size && out.size() < cap; ++m) {
        out.push_back(tok_num(js, t, j));
        j = tok_end(t, j);
    }
}

// Decode an HTTP/1.1 chunked body (the raw-socket client does not). Returns the
// body unchanged when the response is not chunked.
std::string dechunk(const std::string& headers, const std::string& body) {
    std::string lower;
    lower.reserve(headers.size());
    for (char c : headers) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("transfer-encoding: chunked") == std::string::npos) return body;

    std::string out;
    out.reserve(body.size());
    size_t pos = 0;
    while (pos < body.size()) {
        const size_t eol = body.find("\r\n", pos);
        if (eol == std::string::npos) break;
        const long size = std::strtol(body.c_str() + pos, nullptr, 16);  // chunk size (hex)
        if (size <= 0) break;  // 0 = last chunk, negative = malformed
        size_t data = eol + 2;
        if (data >= body.size()) break;
        const size_t take = std::min(static_cast<size_t>(size), body.size() - data);
        out.append(body, data, take);
        pos = data + take + 2;  // skip chunk + trailing CRLF
    }
    return out;
}

} // namespace

Weather wx_parse(const std::string& body, std::string& error) {
    Weather w;
    if (body.empty()) {
        error = "empty";
        return w;
    }
    const char* js = body.c_str();
    const size_t len = body.size();

    // Size the token pool from the body (every token spans >= 1 byte; be generous).
    const unsigned int num_tokens = static_cast<unsigned int>(len / 2 + 64);
    std::vector<jsmntok_t> toks(num_tokens);
    jsmn_parser p;
    jsmn_init(&p);
    const int n = jsmn_parse(&p, js, len, toks.data(), num_tokens);
    if (n < 1 || toks[0].type != JSMN_OBJECT) {
        error = "parse failed";
        return w;
    }
    const jsmntok_t* t = toks.data();

    // utc offset for local-time rendering.
    w.utc_offset = static_cast<int>(tok_num(js, t, obj_get(js, t, 0, "utc_offset_seconds")));

    // Current conditions.
    const int cur = obj_get(js, t, 0, "current");
    if (cur >= 0) {
        w.temp = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "temperature_2m")));
        w.apparent = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "apparent_temperature")));
        w.humidity = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "relative_humidity_2m")));
        w.wind_speed = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "wind_speed_10m")));
        w.wind_dir = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "wind_direction_10m")));
        w.precip = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "precipitation")));
        w.pressure = static_cast<float>(tok_num(js, t, obj_get(js, t, cur, "pressure_msl")));
        w.weather_code = static_cast<int>(tok_num(js, t, obj_get(js, t, cur, "weather_code")));
        w.is_day = tok_num(js, t, obj_get(js, t, cur, "is_day")) > 0.5;
    }

    // Hourly series.
    const int hr = obj_get(js, t, 0, "hourly");
    if (hr >= 0) {
        std::vector<double> htime, htemp, hprob, hprecip, hcode, hwind, hwdir, hpress, hhum, huv;
        read_num_array(js, t, obj_get(js, t, hr, "time"), htime, 168);
        read_num_array(js, t, obj_get(js, t, hr, "temperature_2m"), htemp, 168);
        read_num_array(js, t, obj_get(js, t, hr, "precipitation_probability"), hprob, 168);
        read_num_array(js, t, obj_get(js, t, hr, "precipitation"), hprecip, 168);
        read_num_array(js, t, obj_get(js, t, hr, "weather_code"), hcode, 168);
        read_num_array(js, t, obj_get(js, t, hr, "wind_speed_10m"), hwind, 168);
        read_num_array(js, t, obj_get(js, t, hr, "wind_direction_10m"), hwdir, 168);
        read_num_array(js, t, obj_get(js, t, hr, "pressure_msl"), hpress, 168);
        read_num_array(js, t, obj_get(js, t, hr, "relative_humidity_2m"), hhum, 168);
        read_num_array(js, t, obj_get(js, t, hr, "uv_index"), huv, 168);
        const size_t hn = htime.size();
        auto at = [](const std::vector<double>& v, size_t i) {
            return i < v.size() ? static_cast<float>(v[i]) : 0.f;
        };
        w.hourly.reserve(hn);
        for (size_t i = 0; i < hn; ++i) {
            WxHour h;
            h.time = static_cast<int64_t>(htime[i]);
            h.temp = at(htemp, i);
            h.precip_prob = at(hprob, i);
            h.precip = at(hprecip, i);
            h.wind_speed = at(hwind, i);
            h.wind_dir = at(hwdir, i);
            h.pressure = at(hpress, i);
            h.humidity = at(hhum, i);
            h.uv = at(huv, i);
            h.weather_code = i < hcode.size() ? static_cast<int>(hcode[i]) : -1;
            w.hourly.push_back(h);
        }
    }

    // Daily series.
    const int dy = obj_get(js, t, 0, "daily");
    if (dy >= 0) {
        std::vector<double> dtime, dcode, dmax, dmin, dprob, dpsum, dsr, dss, dwind, dwdir, duv;
        read_num_array(js, t, obj_get(js, t, dy, "time"), dtime, 16);
        read_num_array(js, t, obj_get(js, t, dy, "weather_code"), dcode, 16);
        read_num_array(js, t, obj_get(js, t, dy, "temperature_2m_max"), dmax, 16);
        read_num_array(js, t, obj_get(js, t, dy, "temperature_2m_min"), dmin, 16);
        read_num_array(js, t, obj_get(js, t, dy, "precipitation_probability_max"), dprob, 16);
        read_num_array(js, t, obj_get(js, t, dy, "precipitation_sum"), dpsum, 16);
        read_num_array(js, t, obj_get(js, t, dy, "sunrise"), dsr, 16);
        read_num_array(js, t, obj_get(js, t, dy, "sunset"), dss, 16);
        read_num_array(js, t, obj_get(js, t, dy, "wind_speed_10m_max"), dwind, 16);
        read_num_array(js, t, obj_get(js, t, dy, "wind_direction_10m_dominant"), dwdir, 16);
        read_num_array(js, t, obj_get(js, t, dy, "uv_index_max"), duv, 16);
        const size_t dn = dtime.size();
        auto atd = [](const std::vector<double>& v, size_t i) {
            return i < v.size() ? static_cast<float>(v[i]) : 0.f;
        };
        w.daily.reserve(dn);
        for (size_t i = 0; i < dn; ++i) {
            WxDay d;
            d.time = static_cast<int64_t>(dtime[i]);
            d.weather_code = i < dcode.size() ? static_cast<int>(dcode[i]) : -1;
            d.t_max = atd(dmax, i);
            d.t_min = atd(dmin, i);
            d.precip_prob = atd(dprob, i);
            d.precip_sum = atd(dpsum, i);
            d.wind_max = atd(dwind, i);
            d.wind_dir = atd(dwdir, i);
            d.uv_max = atd(duv, i);
            d.sunrise = i < dsr.size() ? static_cast<int64_t>(dsr[i]) : 0;
            d.sunset = i < dss.size() ? static_cast<int64_t>(dss[i]) : 0;
            w.daily.push_back(d);
        }
    }

    w.valid = cur >= 0 || !w.hourly.empty() || !w.daily.empty();
    if (!w.valid) error = "no fields";
    w.fetched_at = static_cast<int64_t>(std::time(nullptr));
    return w;
}

Weather fetch_weather(float lat, float lon, bool celsius, std::string& error) {
    char url[900];
    std::snprintf(
        url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,"
        "wind_speed_10m,wind_direction_10m,weather_code,is_day,pressure_msl"
        "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,"
        "wind_speed_10m,wind_direction_10m,pressure_msl,relative_humidity_2m,uv_index"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
        "precipitation_sum,sunrise,sunset,wind_speed_10m_max,wind_direction_10m_dominant,uv_index_max"
        "&timezone=auto&forecast_days=7&timeformat=unixtime"
        "&temperature_unit=%s&wind_speed_unit=%s",
        static_cast<double>(lat), static_cast<double>(lon), celsius ? "celsius" : "fahrenheit",
        celsius ? "kmh" : "mph");

    const HttpResult res = http_request("GET", url, {}, 12, 128 * 1024);
    if (!res.error.empty()) {
        error = res.error;
        return {};
    }
    if (res.status != 200) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "HTTP %d", res.status);
        error = buf;
        return {};
    }
    const std::string clean = dechunk(res.headers, res.body);
    Weather w = wx_parse(clean, error);
    if (w.valid) wx_save_cache(lat, lon, clean);  // cache the clean JSON
    return w;
}

std::vector<WxGeoResult> wx_geocode(const std::string& query, std::string& error) {
    std::vector<WxGeoResult> out;
    std::string q = query;
    // Trim surrounding whitespace.
    while (!q.empty() && std::isspace(static_cast<unsigned char>(q.front()))) q.erase(q.begin());
    while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
    if (q.empty()) {
        error = "empty query";
        return out;
    }
    std::string url = "http://geocoding-api.open-meteo.com/v1/search?name=" + url_encode(q) +
                      "&count=10&language=en&format=json";
    const HttpResult res = http_request("GET", url, {}, 12, 64 * 1024);
    if (!res.error.empty()) {
        error = res.error;
        return out;
    }
    if (res.status != 200) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "HTTP %d", res.status);
        error = buf;
        return out;
    }
    const std::string body = dechunk(res.headers, res.body);
    const char* js = body.c_str();
    const size_t len = body.size();
    const unsigned int num_tokens = static_cast<unsigned int>(len / 2 + 64);
    std::vector<jsmntok_t> toks(num_tokens);
    jsmn_parser p;
    jsmn_init(&p);
    const int n = jsmn_parse(&p, js, len, toks.data(), num_tokens);
    if (n < 1 || toks[0].type != JSMN_OBJECT) {
        error = "parse failed";
        return out;
    }
    const jsmntok_t* t = toks.data();
    const int arr = obj_get(js, t, 0, "results");
    if (arr < 0 || t[arr].type != JSMN_ARRAY) {
        error = "no match";  // valid response, just nothing found
        return out;
    }
    int j = arr + 1;
    for (int m = 0; m < t[arr].size && out.size() < 10; ++m) {
        if (t[j].type == JSMN_OBJECT) {
            WxGeoResult r;
            const std::string name = tok_str(js, t, obj_get(js, t, j, "name"));
            const std::string country = tok_str(js, t, obj_get(js, t, j, "country"));
            const std::string admin1 = tok_str(js, t, obj_get(js, t, j, "admin1"));
            r.lat = static_cast<float>(tok_num(js, t, obj_get(js, t, j, "latitude")));
            r.lon = static_cast<float>(tok_num(js, t, obj_get(js, t, j, "longitude")));
            r.name = name;
            if (!admin1.empty() && admin1 != name) r.name += ", " + admin1;
            else if (!country.empty()) r.name += ", " + country;
            if (!name.empty()) out.push_back(r);
        }
        j = tok_end(t, j);
    }
    if (out.empty() && error.empty()) error = "no match";
    return out;
}

std::vector<WxGeoResult> wx_offline_search(const std::string& dataset_path, const std::string& query,
                                           std::string& error) {
    std::vector<WxGeoResult> out;
    std::string q = query;
    while (!q.empty() && std::isspace(static_cast<unsigned char>(q.front()))) q.erase(q.begin());
    while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
    for (char& c : q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (q.empty()) {
        error = "empty query";
        return out;
    }
    std::ifstream in(dataset_path);
    if (!in) {
        error = "no dataset";
        return out;
    }
    // The file is sorted most-populous first, so the first 10 name matches are
    // the 10 largest matching cities.
    std::string line;
    while (std::getline(in, line) && out.size() < 10) {
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const size_t t2 = line.find('\t', t1 + 1);
        const size_t t3 = line.find('\t', t2 + 1);
        const size_t t4 = line.find('\t', t3 + 1);
        if (t2 == std::string::npos || t3 == std::string::npos || t4 == std::string::npos) continue;
        const std::string name = line.substr(0, t1);
        std::string lname = name;
        for (char& c : lname) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lname.find(q) == std::string::npos) continue;
        WxGeoResult r;
        r.lat = std::strtof(line.c_str() + t1 + 1, nullptr);
        r.lon = std::strtof(line.c_str() + t2 + 1, nullptr);
        const std::string country = line.substr(t3 + 1, t4 - t3 - 1);
        r.name = country.empty() ? name : name + ", " + country;
        out.push_back(r);
    }
    if (out.empty()) error = "no match";
    return out;
}

// ---- WMO code mapping -------------------------------------------------------

const char* wx_condition_text(int code) {
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61:
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71:
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80:
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85:
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunder + hail";
    default: return "--";
    }
}

const char* wx_condition_icon(int code, bool is_day) {
    // Phosphor-Fill codepoints (verified via GSUB name ligatures). Phase 1 maps
    // snow/fog/storm onto cloud / cloud-rain; dedicated glyphs come later.
    static const char kSun[] = "";       // sun-fill
    static const char kMoon[] = "";      // moon-fill
    static const char kPartly[] = "";    // cloud-sun-fill
    static const char kCloud[] = "";     // cloud-fill
    static const char kRain[] = "";      // cloud-rain-fill
    switch (code) {
    case 0:
    case 1: return is_day ? kSun : kMoon;
    case 2: return kPartly;
    case 3:
    case 45:
    case 48: return kCloud;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 71:
    case 73:
    case 75:
    case 77:
    case 80:
    case 81:
    case 82:
    case 85:
    case 86:
    case 95:
    case 96:
    case 99: return kRain;
    default: return kCloud;
    }
}

// ---- Config + cache persistence --------------------------------------------

WxConfig wx_load_config() {
    WxConfig cfg;
    std::ifstream in(tools::config_file("weather.txt"));
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            if (line.rfind("unit ", 0) == 0) {
                cfg.celsius = line.substr(5) != "F";
            } else if (line.rfind("active ", 0) == 0) {
                cfg.active = std::atoi(line.c_str() + 7);
            } else if (line.rfind("refresh ", 0) == 0) {
                cfg.refresh_min = std::atoi(line.c_str() + 8);
            } else if (line.rfind("city ", 0) == 0) {
                // city <lat> <lon> <name...>
                WxCity c;
                const char* s = line.c_str() + 5;
                char* e = nullptr;
                c.lat = std::strtof(s, &e);
                c.lon = std::strtof(e, &e);
                while (*e == ' ') ++e;
                c.name = e;
                if (!c.name.empty()) cfg.cities.push_back(c);
            }
        }
    }
    if (cfg.cities.empty()) {
        cfg.cities.push_back({"Buenos Aires", -34.6037f, -58.3816f});
    }
    if (cfg.active < 0 || cfg.active >= static_cast<int>(cfg.cities.size())) cfg.active = 0;
    return cfg;
}

void wx_save_config(const WxConfig& cfg) {
    std::ofstream out(tools::config_file("weather.txt"), std::ios::trunc);
    if (!out) return;
    out << "unit " << (cfg.celsius ? "C" : "F") << "\n";
    out << "active " << cfg.active << "\n";
    out << "refresh " << cfg.refresh_min << "\n";
    for (const auto& c : cfg.cities) {
        out << "city " << c.lat << " " << c.lon << " " << c.name << "\n";
    }
}

namespace {
std::string cache_name(float lat, float lon) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "weather_cache_%+.2f_%+.2f.json", static_cast<double>(lat),
                  static_cast<double>(lon));
    return buf;
}
} // namespace

void wx_save_cache(float lat, float lon, const std::string& raw_json) {
    std::ofstream out(tools::config_file(cache_name(lat, lon).c_str()), std::ios::trunc);
    if (out) out << raw_json;
}

std::string wx_load_cache(float lat, float lon) {
    std::ifstream in(tools::config_file(cache_name(lat, lon).c_str()));
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace platform
