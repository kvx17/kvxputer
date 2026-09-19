#include "pda_todo.h"
#include "pda_common.h"
#include "pda_editor.h"

#include "root/storage/paths.h"
#include "root/ui/display.h"
#include <algorithm>
#include <globals.h>

// Storage format: one task per line as "state|due|text"
//   state: "1" done, "0" pending
//   due:   free-form date string (may be empty)
//   text:  task description
struct Task {
    bool done;
    String due;
    String text;
};

static String todoPath() { return String(kvx::paths::PDA_TODO) + "/todo.txt"; }

static Task parseTask(const String &line) {
    Task t;
    t.done = false;
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0) {
        // Legacy / plain line: treat the whole thing as the task text.
        t.text = line;
        return t;
    }
    t.done = line.substring(0, p1) == "1";
    t.due = line.substring(p1 + 1, p2);
    t.text = line.substring(p2 + 1);
    return t;
}

static String serializeTask(const Task &t) {
    return String(t.done ? "1" : "0") + "|" + t.due + "|" + t.text;
}

static String taskLabel(const Task &t) {
    String label = t.done ? "[x] " : "[ ] ";
    label += t.text;
    if (t.due.length()) label += " (" + t.due + ")";
    return label;
}

static String dueSortKey(const Task &t) {
    if (t.due.length() == 0) return "\x7F"; // empty due last
    if (t.due.length() >= 10 && t.due[4] == '-' && t.due[7] == '-') return t.due;
    return String("~") + t.due;
}

static void sortTasks(std::vector<Task> &tasks) {
    std::sort(tasks.begin(), tasks.end(), [](const Task &a, const Task &b) {
        if (a.done != b.done) return !a.done; // pending first
        return dueSortKey(a) < dueSortKey(b);
    });
}

static void writeTasks(FS *fs, const std::vector<Task> &tasks) {
    std::vector<String> lines;
    for (const Task &t : tasks) lines.push_back(serializeTask(t));
    pdaWriteLines(fs, todoPath(), lines);
}

static void pdaTaskActions(FS *fs, std::vector<Task> &tasks, size_t index) {
    bool changed = false;
    bool done = false;

    while (true) {
        std::vector<Option> opts = {
            {tasks[index].done ? "Mark Pending" : "Mark Done", [&]() {
                 tasks[index].done = !tasks[index].done;
                 changed = true;
             }},
            {"Edit Text", [&]() {
                 String edited = tasks[index].text;
                 if (pdaTextEditor(edited, "Edit task:", 120, false) == PDA_EDIT_OK &&
                     edited.length() > 0) {
                     tasks[index].text = edited;
                     changed = true;
                 }
             }},
            {"Set Due Date", [&]() {
                 String due = tasks[index].due;
                 if (pdaTextEditor(due, "Due date (free text):", 40, false) == PDA_EDIT_OK) {
                     tasks[index].due = due;
                     changed = true;
                 }
             }},
            {"Delete", [&]() {
                 tasks.erase(tasks.begin() + index);
                 changed = true;
                 done = true;
             }},
        };
        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Task");
        if (r < 0 || done || returnToMenu || forceHome) break;
        if (changed) writeTasks(fs, tasks); // persist after each edit
    }

    if (changed) writeTasks(fs, tasks);
}

void pdaTodo() {
    FS *fs = nullptr;
    if (!pdaGetFs(fs)) return;
    kvx::paths::ensureDir(*fs, kvx::paths::PDA_TODO);

    while (true) {
        std::vector<String> lines = pdaReadLines(fs, todoPath());
        std::vector<Task> tasks;
        for (const String &l : lines) {
            if (l.length()) tasks.push_back(parseTask(l));
        }
        sortTasks(tasks);

        std::vector<Option> opts;
        opts.push_back({"New Task", [&]() {
                            String text = "";
                            if (pdaTextEditor(text, "New task:", 120, false) != PDA_EDIT_OK ||
                                text.length() == 0)
                                return;
                            String due = "";
                            if (pdaTextEditor(due, "Due date (optional):", 40, false) != PDA_EDIT_OK)
                                due = "";
                            Task t;
                            t.done = false;
                            t.due = due;
                            t.text = text;
                            tasks.push_back(t);
                            sortTasks(tasks);
                            writeTasks(fs, tasks);
                        }});
        for (size_t i = 0; i < tasks.size(); i++) {
            opts.push_back({taskLabel(tasks[i]), [&, i]() { pdaTaskActions(fs, tasks, i); }});
        }

        int r = loopOptions(opts, MENU_TYPE_SUBMENU, "To-Do");
        if (r < 0 || returnToMenu || forceHome) break;
    }
}
