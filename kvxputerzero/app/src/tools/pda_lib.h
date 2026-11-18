#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace tools::pda {

enum class Channel : int {
    Notes = 0,
    Memos,
    Todo,
    Calendar,
    Contacts,
    Alarms,
    WorldClock,
    Calculator,
    Count,
};

enum class WcFace : int {
    Cities = 0,
    Charge = 1,
    Digital = 2,
    Count = 3,
};

struct NoteRef {
    std::string path;
    std::string title;
};

struct Task {
    bool done = false;
    std::string due;
    std::string text;
};

struct Contact {
    std::string name;
    std::string phone;
    std::string address;
};

struct Alarm {
    bool triggered = false;
    int year = 2025;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    std::string label;
};

struct Event {
    std::string date;  // YYYY-MM-DD
    std::string time;  // HH:MM
    std::string title;
    size_t line_index = 0;
};

struct City {
    const char* name;
    float utc_offset;
};

const char* channel_label(Channel ch);
bool ensure_dirs(std::string& error);

std::string strip_pipes(std::string s);
std::string sanitize_name(const std::string& name);

int days_in_month(int year, int month);
int day_of_week(int year, int month, int day); // 0=Sun
void add_days(int& year, int& month, int& day, int delta);
void add_months(int& year, int& month, int& day, int delta);
std::string format_date(int year, int month, int day);
std::string format_time(int hour, int minute);
bool parse_date(const std::string& s, int& year, int& month, int& day);
bool parse_time(const std::string& s, int& hour, int& minute);
bool parse_yyyymmdd(const std::string& s, int& year, int& month, int& day);
bool parse_hhmm(const std::string& s, int& hour, int& minute);
int compare_datetime(int y1, int mo1, int d1, int h1, int mi1, int y2, int mo2, int d2, int h2, int mi2);

std::tm local_now();
bool clock_looks_set();

// Notes
bool list_notes(std::vector<NoteRef>& out, std::string& error);
bool read_note(const std::string& path, std::string& body, std::string& error);
bool save_note(std::string& path, const std::string& body, std::string& error);
bool delete_note(const std::string& path, std::string& error);

// Memos
bool load_memos(std::vector<std::string>& lines, std::string& error);
bool save_memos(const std::vector<std::string>& lines, std::string& error);

// To-Do
bool load_tasks(std::vector<Task>& tasks, std::string& error);
bool save_tasks(const std::vector<Task>& tasks, std::string& error);
void sort_tasks(std::vector<Task>& tasks);

// Contacts
bool load_contacts(std::vector<Contact>& contacts, std::string& error);
bool save_contacts(const std::vector<Contact>& contacts, std::string& error);

// Alarms
bool load_alarms(std::vector<Alarm>& alarms, std::string& error);
bool save_alarms(const std::vector<Alarm>& alarms, std::string& error);
std::string alarm_label(const Alarm& a);
bool find_due_alarm(Alarm& out, size_t& index, std::string& error);
bool mark_alarm_triggered(size_t index, std::string& error);
void play_alarm_beep(); // best-effort; no-op if aplay/ALSA unavailable

// Calendar events
bool load_events(std::vector<Event>& events, std::string& error);
bool save_events(const std::vector<Event>& events, std::string& error);
void events_for_day(const std::vector<Event>& all, const std::string& date, std::vector<Event>& out);
void event_counts_for_month(const std::vector<Event>& all, int year, int month, int counts[32]);

// World clock
const City* cities();
int city_count();
std::tm city_tm(float utc_offset);
std::string format_hm(const std::tm& t, bool clock24);
std::string format_hms(const std::tm& t, bool clock24);
std::string format_offset(float o);

} // namespace tools::pda
