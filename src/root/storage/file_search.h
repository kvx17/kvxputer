#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>

struct FileSearchHit {
    String path;     // absolute path on fs
    String basename;
    String parent;   // truncated parent for subtitle
    bool isDir = false;
    bool onSd = false;
};

// Progressive name search under LittleFS / and SD /kvxputer.
// Restart whenever the query changes. Tick from the UI loop with a time budget.
class FileSearchScanner {
public:
    void restart(const String &query);
    // Run up to budgetMs of work. Returns true while more work remains.
    bool tick(unsigned long budgetMs);
    const std::vector<FileSearchHit> &hits() const { return _hits; }
    bool done() const { return _done; }
    void clear();

private:
    struct DirJob {
        FS *fs;
        String path;
        int depth;
        bool onSd;
    };

    String _normQuery;
    std::vector<DirJob> _queue;
    std::vector<FileSearchHit> _hits;
    size_t _visited = 0;
    bool _done = true;
    bool _started = false;

    void enqueueRoot(FS &fs, const String &root, bool onSd);
    void processOne(DirJob &job);
};
