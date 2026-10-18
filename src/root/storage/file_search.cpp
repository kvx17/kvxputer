#include "root/storage/file_search.h"
#include "root/storage/sd_functions.h"
#include "root/ui/quick_search_match.h"
#include <LittleFS.h>
#include <SD.h>

static constexpr int kMaxDepth = 4;
static constexpr size_t kMaxVisited = 500;
static constexpr size_t kMaxHits = 50;

void FileSearchScanner::clear() {
    _normQuery = "";
    _queue.clear();
    _hits.clear();
    _visited = 0;
    _done = true;
    _started = false;
}

void FileSearchScanner::enqueueRoot(FS &fs, const String &root, bool onSd) {
    if (!fs.exists(root)) return;
    _queue.push_back({&fs, root, 0, onSd});
}

void FileSearchScanner::restart(const String &query) {
    clear();
    _normQuery = qsNormalize(query);
    if (_normQuery.length() == 0) {
        _done = true;
        return;
    }

    enqueueRoot(LittleFS, "/", false);
    if (setupSdCard()) enqueueRoot(SD, "/kvxputer", true);

    _done = _queue.empty();
    _started = true;
}

void FileSearchScanner::processOne(DirJob &job) {
    if (_visited >= kMaxVisited || _hits.size() >= kMaxHits) {
        _done = true;
        return;
    }

    File dir = job.fs->open(job.path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    while (true) {
        if (_visited >= kMaxVisited || _hits.size() >= kMaxHits) {
            _done = true;
            break;
        }

        bool isDir = false;
        String fullPath = dir.getNextFileName(&isDir);
        if (fullPath.length() == 0) break;
        _visited++;

        while (fullPath.indexOf("//") >= 0) fullPath.replace("//", "/");

        String base = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (base == "." || base == "..") continue;

        const int score = qsScoreMatch(_normQuery, base, base, "");
        if (score > 0) {
            FileSearchHit hit;
            hit.path = fullPath;
            hit.basename = base;
            hit.isDir = isDir;
            hit.onSd = job.onSd;
            int lastSlash = fullPath.lastIndexOf('/');
            hit.parent = (lastSlash > 0) ? fullPath.substring(0, lastSlash) : String("/");
            if (hit.parent.length() > 28) {
                hit.parent = String("…") + hit.parent.substring(hit.parent.length() - 26);
            }
            _hits.push_back(hit);
            if (_hits.size() >= kMaxHits) {
                _done = true;
                break;
            }
        }

        if (isDir && job.depth + 1 < kMaxDepth) {
            _queue.push_back({job.fs, fullPath, job.depth + 1, job.onSd});
        }
    }
    dir.close();
}

bool FileSearchScanner::tick(unsigned long budgetMs) {
    if (_done || !_started) return false;
    const unsigned long t0 = millis();
    while (!_queue.empty() && !_done) {
        DirJob job = _queue.front();
        _queue.erase(_queue.begin());
        processOne(job);
        if (millis() - t0 >= budgetMs) break;
    }
    if (_queue.empty() || _visited >= kMaxVisited || _hits.size() >= kMaxHits) _done = true;
    return !_done;
}
