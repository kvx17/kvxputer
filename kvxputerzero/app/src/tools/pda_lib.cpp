#include "pda_lib.h"

#include "user_paths.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace tools::pda {
namespace {

namespace fs = std::filesystem;

std::string pda_root() {
    return (fs::path(tools::config_dir()) / "pda").string();
}

std::string subdir(const char* name) {
    return (fs::path(pda_root()) / name).string();
}

void chmod_file(const std::string& path) {
#if !defined(_WIN32)
    ::chmod(path.c_str(), 0600);
#endif
}

bool write_lines(const std::string& path, const std::vector<std::string>& lines, std::string& error) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "Save failed";
        return false;
    }
    for (const auto& line : lines) {
        out << line << '\n';
    }
    out.close();
    chmod_file(path);
    return true;
}

bool read_lines(const std::string& path, std::vector<std::string>& lines, std::string& error) {
    lines.clear();
    std::ifstream in(path);
    if (!in) {
        // Missing file is empty, not an error.
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    (void)error;
    return true;
}

std::string pad2(int v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02d", v);
    return b;
}

std::string note_title_from_body(const std::string& content) {
    const auto nl = content.find('\n');
    std::string t = (nl == std::string::npos) ? content : content.substr(0, nl);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back();
    size_t i = 0;
    while (i < t.size() && t[i] == ' ') ++i;
    t.erase(0, i);
    if (t.size() > 40) t.resize(40);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) t = "note";
    return sanitize_name(t);
}

std::string unique_note_path(const std::string& base, const std::string& keep_path) {
    const fs::path dir = subdir("notes");
    std::string candidate = (dir / (base + ".txt")).string();
    if (candidate == keep_path || !fs::exists(candidate)) return candidate;
    for (int n = 2; n < 1000; ++n) {
        candidate = (dir / (base + "-" + std::to_string(n) + ".txt")).string();
        if (candidate == keep_path || !fs::exists(candidate)) return candidate;
    }
    return candidate;
}

Task parse_task(const std::string& line) {
    Task t;
    const auto p1 = line.find('|');
    const auto p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) {
        t.text = line;
        return t;
    }
    t.done = line.substr(0, p1) == "1";
    t.due = line.substr(p1 + 1, p2 - p1 - 1);
    t.text = line.substr(p2 + 1);
    return t;
}

std::string serialize_task(const Task& t) {
    return std::string(t.done ? "1" : "0") + "|" + t.due + "|" + strip_pipes(t.text);
}

Contact parse_contact(const std::string& line) {
    Contact c;
    const auto p1 = line.find('|');
    const auto p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
    if (p1 == std::string::npos) {
        c.name = line;
        return c;
    }
    c.name = line.substr(0, p1);
    if (p2 == std::string::npos) {
        c.phone = line.substr(p1 + 1);
        return c;
    }
    c.phone = line.substr(p1 + 1, p2 - p1 - 1);
    c.address = line.substr(p2 + 1);
    return c;
}

std::string serialize_contact(const Contact& c) {
    return strip_pipes(c.name) + "|" + strip_pipes(c.phone) + "|" + strip_pipes(c.address);
}

bool parse_alarm(const std::string& line, Alarm& a) {
    const auto p1 = line.find('|');
    const auto p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
    const auto p3 = (p2 == std::string::npos) ? std::string::npos : line.find('|', p2 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) return false;
    a.triggered = line.substr(0, p1) == "1";
    const std::string date = line.substr(p1 + 1, p2 - p1 - 1);
    const std::string time = line.substr(p2 + 1, p3 - p2 - 1);
    a.label = line.substr(p3 + 1);
    return parse_date(date, a.year, a.month, a.day) && parse_time(time, a.hour, a.minute);
}

std::string serialize_alarm(const Alarm& a) {
    return std::string(a.triggered ? "1" : "0") + "|" + format_date(a.year, a.month, a.day) + "|" +
           format_time(a.hour, a.minute) + "|" + strip_pipes(a.label);
}

Event parse_event_line(const std::string& line, size_t index) {
    Event e;
    e.line_index = index;
    const auto p1 = line.find('|');
    const auto p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
    if (p1 == std::string::npos) {
        e.date = line;
        return e;
    }
    e.date = line.substr(0, p1);
    if (p2 == std::string::npos) {
        e.time = line.substr(p1 + 1);
        return e;
    }
    e.time = line.substr(p1 + 1, p2 - p1 - 1);
    e.title = line.substr(p2 + 1);
    return e;
}

std::string serialize_event(const Event& e) {
    return e.date + "|" + e.time + "|" + strip_pipes(e.title);
}

std::string due_sort_key(const Task& t) {
    if (t.due.empty()) return "\x7F";
    if (t.due.size() >= 10 && t.due[4] == '-' && t.due[7] == '-') return t.due;
    return std::string("~") + t.due;
}

const City kCities[] = {
    {"Honolulu", -10.0f},
    {"LA", -8.0f},
    {"Chicago", -6.0f},
    {"New York", -5.0f},
    {"Sao Paulo", -3.0f},
    {"London", 0.0f},
    {"Paris", 1.0f},
    {"Cairo", 2.0f},
    {"Moscow", 3.0f},
    {"Dubai", 4.0f},
    {"Delhi", 5.5f},
    {"Bangkok", 7.0f},
    {"Beijing", 8.0f},
    {"Tokyo", 9.0f},
    {"Sydney", 10.0f},
    {"Auckland", 12.0f},
};

} // namespace

const char* channel_label(Channel ch) {
    switch (ch) {
    case Channel::Notes:
        return "Notes";
    case Channel::Memos:
        return "Memos";
    case Channel::Todo:
        return "To-Do";
    case Channel::Calendar:
        return "Calendar";
    case Channel::Contacts:
        return "Contacts";
    case Channel::Alarms:
        return "Alarms";
    case Channel::WorldClock:
        return "World Clock";
    case Channel::Calculator:
        return "Calculator";
    default:
        return "PDA";
    }
}

bool ensure_dirs(std::string& error) {
    std::error_code ec;
    const char* dirs[] = {"notes", "memos", "todo", "calendar", "contacts", "alarms"};
    for (const char* d : dirs) {
        fs::create_directories(subdir(d), ec);
        if (ec) {
            error = "Cannot create PDA dirs";
            return false;
        }
#if !defined(_WIN32)
        ::chmod(subdir(d).c_str(), 0700);
#endif
    }
    return true;
}

std::string strip_pipes(std::string s) {
    for (char& c : s) {
        if (c == '|') c = '/';
        if (c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::string sanitize_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == ' ') {
            out.push_back(static_cast<char>(c == ' ' ? '-' : c));
        }
    }
    while (!out.empty() && (out.front() == '-' || out.front() == '_')) out.erase(out.begin());
    while (!out.empty() && (out.back() == '-' || out.back() == '_')) out.pop_back();
    if (out.empty()) out = "note";
    if (out.size() > 40) out.resize(40);
    return out;
}

int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

int day_of_week(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

void add_days(int& year, int& month, int& day, int delta) {
    day += delta;
    while (day < 1) {
        month--;
        if (month < 1) {
            month = 12;
            year--;
        }
        day += days_in_month(year, month);
    }
    while (day > days_in_month(year, month)) {
        day -= days_in_month(year, month);
        month++;
        if (month > 12) {
            month = 1;
            year++;
        }
    }
}

void add_months(int& year, int& month, int& day, int delta) {
    month += delta;
    while (month < 1) {
        month += 12;
        year--;
    }
    while (month > 12) {
        month -= 12;
        year++;
    }
    const int dim = days_in_month(year, month);
    if (day > dim) day = dim;
}

std::string format_date(int year, int month, int day) {
    return std::to_string(year) + "-" + pad2(month) + "-" + pad2(day);
}

std::string format_time(int hour, int minute) {
    return pad2(hour) + ":" + pad2(minute);
}

bool parse_date(const std::string& s, int& year, int& month, int& day) {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-') return false;
    try {
        year = std::stoi(s.substr(0, 4));
        month = std::stoi(s.substr(5, 2));
        day = std::stoi(s.substr(8, 2));
    } catch (...) {
        return false;
    }
    return month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month);
}

bool parse_time(const std::string& s, int& hour, int& minute) {
    if (s.size() < 5 || s[2] != ':') return false;
    try {
        hour = std::stoi(s.substr(0, 2));
        minute = std::stoi(s.substr(3, 2));
    } catch (...) {
        return false;
    }
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

bool parse_yyyymmdd(const std::string& s, int& year, int& month, int& day) {
    if (s.size() != 8) return false;
    try {
        year = std::stoi(s.substr(0, 4));
        month = std::stoi(s.substr(4, 2));
        day = std::stoi(s.substr(6, 2));
    } catch (...) {
        return false;
    }
    if (year < 2000 || year > 2099 || month < 1 || month > 12) return false;
    if (day < 1 || day > days_in_month(year, month)) return false;
    return true;
}

bool parse_hhmm(const std::string& s, int& hour, int& minute) {
    if (s.size() != 4) return false;
    try {
        hour = std::stoi(s.substr(0, 2));
        minute = std::stoi(s.substr(2, 2));
    } catch (...) {
        return false;
    }
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

int compare_datetime(int y1, int mo1, int d1, int h1, int mi1, int y2, int mo2, int d2, int h2, int mi2) {
    if (y1 != y2) return y1 < y2 ? -1 : 1;
    if (mo1 != mo2) return mo1 < mo2 ? -1 : 1;
    if (d1 != d2) return d1 < d2 ? -1 : 1;
    if (h1 != h2) return h1 < h2 ? -1 : 1;
    if (mi1 != mi2) return mi1 < mi2 ? -1 : 1;
    return 0;
}

std::tm local_now() {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
#if defined(_WIN32)
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    return t;
}

bool clock_looks_set() {
    const auto t = local_now();
    return (t.tm_year + 1900) >= 2020;
}

bool list_notes(std::vector<NoteRef>& out, std::string& error) {
    out.clear();
    std::string err;
    if (!ensure_dirs(err)) {
        error = err;
        return false;
    }
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(subdir("notes"), ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;
        NoteRef n;
        n.path = entry.path().string();
        std::string body;
        if (read_note(n.path, body, error)) {
            n.title = note_title_from_body(body);
        } else {
            n.title = entry.path().stem().string();
        }
        out.push_back(std::move(n));
    }
    std::sort(out.begin(), out.end(), [](const NoteRef& a, const NoteRef& b) {
        return a.title < b.title;
    });
    return true;
}

bool read_note(const std::string& path, std::string& body, std::string& error) {
    body.clear();
    std::ifstream in(path);
    if (!in) {
        error = "Open failed";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    body = ss.str();
    return true;
}

bool save_note(std::string& path, const std::string& body, std::string& error) {
    std::string err;
    if (!ensure_dirs(err)) {
        error = err;
        return false;
    }
    const std::string base = note_title_from_body(body);
    const std::string new_path = unique_note_path(base, path);
    if (!path.empty() && new_path != path) {
        std::error_code ec;
        fs::rename(path, new_path, ec);
        if (ec) {
            // Fall back to writing at old path name uniqueness failed.
        } else {
            path = new_path;
        }
    }
    if (path.empty()) path = new_path;
    else if (path != new_path && !fs::exists(path)) path = new_path;

    // If rename did not happen but unique name differs, write to unique and drop old.
    if (!path.empty() && path != new_path && fs::exists(path)) {
        std::ofstream out(new_path, std::ios::trunc);
        if (!out) {
            error = "Save failed";
            return false;
        }
        out << body;
        out.close();
        chmod_file(new_path);
        std::error_code ec;
        fs::remove(path, ec);
        path = new_path;
        return true;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "Save failed";
        return false;
    }
    out << body;
    out.close();
    chmod_file(path);
    return true;
}

bool delete_note(const std::string& path, std::string& error) {
    std::error_code ec;
    if (!fs::remove(path, ec)) {
        error = "Delete failed";
        return false;
    }
    return true;
}

bool load_memos(std::vector<std::string>& lines, std::string& error) {
    return read_lines((fs::path(subdir("memos")) / "memos.txt").string(), lines, error);
}

bool save_memos(const std::vector<std::string>& lines, std::string& error) {
    std::vector<std::string> cleaned;
    cleaned.reserve(lines.size());
    for (const auto& l : lines) cleaned.push_back(strip_pipes(l));
    return write_lines((fs::path(subdir("memos")) / "memos.txt").string(), cleaned, error);
}

bool load_tasks(std::vector<Task>& tasks, std::string& error) {
    tasks.clear();
    std::vector<std::string> lines;
    if (!read_lines((fs::path(subdir("todo")) / "todo.txt").string(), lines, error)) return false;
    for (const auto& l : lines) {
        if (!l.empty()) tasks.push_back(parse_task(l));
    }
    sort_tasks(tasks);
    return true;
}

bool save_tasks(const std::vector<Task>& tasks, std::string& error) {
    std::vector<std::string> lines;
    lines.reserve(tasks.size());
    for (const auto& t : tasks) lines.push_back(serialize_task(t));
    return write_lines((fs::path(subdir("todo")) / "todo.txt").string(), lines, error);
}

void sort_tasks(std::vector<Task>& tasks) {
    std::sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        if (a.done != b.done) return !a.done;
        return due_sort_key(a) < due_sort_key(b);
    });
}

bool load_contacts(std::vector<Contact>& contacts, std::string& error) {
    contacts.clear();
    std::vector<std::string> lines;
    if (!read_lines((fs::path(subdir("contacts")) / "contacts.txt").string(), lines, error)) return false;
    for (const auto& l : lines) {
        if (!l.empty()) contacts.push_back(parse_contact(l));
    }
    std::sort(contacts.begin(), contacts.end(), [](const Contact& a, const Contact& b) {
        std::string ua = a.name, ub = b.name;
        for (char& c : ua) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (char& c : ub) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return ua < ub;
    });
    return true;
}

bool save_contacts(const std::vector<Contact>& contacts, std::string& error) {
    std::vector<std::string> lines;
    lines.reserve(contacts.size());
    for (const auto& c : contacts) lines.push_back(serialize_contact(c));
    return write_lines((fs::path(subdir("contacts")) / "contacts.txt").string(), lines, error);
}

bool load_alarms(std::vector<Alarm>& alarms, std::string& error) {
    alarms.clear();
    std::vector<std::string> lines;
    if (!read_lines((fs::path(subdir("alarms")) / "alarms.txt").string(), lines, error)) return false;
    for (const auto& l : lines) {
        Alarm a;
        if (!l.empty() && parse_alarm(l, a)) alarms.push_back(a);
    }
    return true;
}

bool save_alarms(const std::vector<Alarm>& alarms, std::string& error) {
    std::vector<std::string> lines;
    lines.reserve(alarms.size());
    for (const auto& a : alarms) lines.push_back(serialize_alarm(a));
    return write_lines((fs::path(subdir("alarms")) / "alarms.txt").string(), lines, error);
}

std::string alarm_label(const Alarm& a) {
    std::string s = format_date(a.year, a.month, a.day) + " " + format_time(a.hour, a.minute);
    if (!a.label.empty()) s += " " + a.label;
    if (a.triggered) s += " [done]";
    return s;
}

bool find_due_alarm(Alarm& out, size_t& index, std::string& error) {
    std::vector<Alarm> alarms;
    if (!load_alarms(alarms, error)) return false;
    const auto now = local_now();
    const int y = now.tm_year + 1900;
    const int mo = now.tm_mon + 1;
    const int d = now.tm_mday;
    const int h = now.tm_hour;
    const int mi = now.tm_min;
    for (size_t i = 0; i < alarms.size(); ++i) {
        if (alarms[i].triggered) continue;
        if (compare_datetime(alarms[i].year, alarms[i].month, alarms[i].day, alarms[i].hour, alarms[i].minute, y,
                             mo, d, h, mi) <= 0) {
            out = alarms[i];
            index = i;
            return true;
        }
    }
    return false;
}

bool mark_alarm_triggered(size_t index, std::string& error) {
    std::vector<Alarm> alarms;
    if (!load_alarms(alarms, error)) return false;
    if (index >= alarms.size()) {
        error = "Bad alarm index";
        return false;
    }
    alarms[index].triggered = true;
    return save_alarms(alarms, error);
}

void play_alarm_beep() {
    // Best-effort: generate 4 short 8 kHz mono beeps via aplay if present.
    // No libasound link; silent no-op when aplay is missing.
    const pid_t pid = fork();
    if (pid != 0) return;
    // Child
    FILE* pipe = popen("aplay -q -t raw -f S16_LE -r 8000 -c 1 2>/dev/null", "w");
    if (!pipe) _exit(0);
    for (int pulse = 0; pulse < 4; ++pulse) {
        for (int i = 0; i < 8000 * 200 / 1000; ++i) {
            // ~1000 Hz square-ish
            const int16_t sample = ((i / 4) % 2) ? 8000 : -8000;
            fwrite(&sample, sizeof(sample), 1, pipe);
        }
        for (int i = 0; i < 8000 * 120 / 1000; ++i) {
            const int16_t sample = 0;
            fwrite(&sample, sizeof(sample), 1, pipe);
        }
    }
    pclose(pipe);
    _exit(0);
}

bool load_events(std::vector<Event>& events, std::string& error) {
    events.clear();
    std::vector<std::string> lines;
    if (!read_lines((fs::path(subdir("calendar")) / "events.txt").string(), lines, error)) return false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        events.push_back(parse_event_line(lines[i], i));
    }
    return true;
}

bool save_events(const std::vector<Event>& events, std::string& error) {
    std::vector<std::string> lines;
    lines.reserve(events.size());
    for (const auto& e : events) lines.push_back(serialize_event(e));
    return write_lines((fs::path(subdir("calendar")) / "events.txt").string(), lines, error);
}

void events_for_day(const std::vector<Event>& all, const std::string& date, std::vector<Event>& out) {
    out.clear();
    for (const auto& e : all) {
        if (e.date == date) out.push_back(e);
    }
}

void event_counts_for_month(const std::vector<Event>& all, int year, int month, int counts[32]) {
    for (int i = 0; i < 32; ++i) counts[i] = 0;
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04d-%02d-", year, month);
    const size_t plen = std::strlen(prefix);
    for (const auto& e : all) {
        if (e.date.size() >= plen && e.date.compare(0, plen, prefix) == 0) {
            int d = 0;
            try {
                d = std::stoi(e.date.substr(8, 2));
            } catch (...) {
                continue;
            }
            if (d >= 1 && d <= 31) counts[d]++;
        }
    }
}

const City* cities() {
    return kCities;
}

int city_count() {
    return static_cast<int>(sizeof(kCities) / sizeof(kCities[0]));
}

std::tm city_tm(float utc_offset) {
    // time() is UTC epoch; add static city offset and format with gmtime.
    const std::time_t city = std::time(nullptr) + static_cast<std::time_t>(utc_offset * 3600.0f);
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &city);
#else
    gmtime_r(&city, &out);
#endif
    return out;
}

std::string format_hm(const std::tm& t, bool clock24) {
    char buf[16];
    if (clock24) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        int hour12 = (t.tm_hour == 0) ? 12 : (t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour);
        const char* ampm = (t.tm_hour < 12) ? "AM" : "PM";
        std::snprintf(buf, sizeof(buf), "%02d:%02d%s", hour12, t.tm_min, ampm);
    }
    return buf;
}

std::string format_hms(const std::tm& t, bool clock24) {
    char buf[16];
    if (clock24) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int hour12 = (t.tm_hour == 0) ? 12 : (t.tm_hour > 12 ? t.tm_hour - 12 : t.tm_hour);
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hour12, t.tm_min);
    }
    return buf;
}

std::string format_offset(float o) {
    char buf[16];
    if (o == static_cast<float>(static_cast<int>(o))) {
        std::snprintf(buf, sizeof(buf), "%+d", static_cast<int>(o));
    } else {
        std::snprintf(buf, sizeof(buf), "%+.1f", o);
    }
    return buf;
}

} // namespace tools::pda
