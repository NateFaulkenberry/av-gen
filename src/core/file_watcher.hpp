#pragma once

// Polling file watcher: checks modification times of registered files at most every
// `intervalSeconds` (wall clock) and reports the ones that changed. Zero dependencies,
// deterministic, and sufficient for a handful of shader files (ADR-006). Not for thousands of
// files; efsw is the documented upgrade.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace avgen {

class FileWatcher {
public:
    explicit FileWatcher(double intervalSeconds = 0.25);

    // Registers (or re-registers) a file; the current mtime becomes the baseline.
    void watch(const std::filesystem::path& path);
    void unwatch(const std::filesystem::path& path);
    void clear();
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

    // Returns the watched paths whose mtime changed (or which appeared/disappeared) since the
    // last report. Cheap when called every frame: only stats the files when the interval elapsed.
    std::vector<std::filesystem::path> poll();
    // Forces a check regardless of the interval (tests).
    std::vector<std::filesystem::path> pollNow();

private:
    struct Entry {
        std::filesystem::file_time_type mtime{};
        bool exists = false;
    };
    static Entry stat(const std::filesystem::path& path);

    double interval_;
    std::chrono::steady_clock::time_point lastPoll_{};
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace avgen
