#include "core/file_watcher.hpp"

namespace avgen {

FileWatcher::FileWatcher(double intervalSeconds) : interval_(intervalSeconds) {}

FileWatcher::Entry FileWatcher::stat(const std::filesystem::path& path) {
    Entry entry;
    std::error_code ec;
    entry.exists = std::filesystem::is_regular_file(path, ec) && !ec;
    if (entry.exists) {
        entry.mtime = std::filesystem::last_write_time(path, ec);
        if (ec) {
            entry.exists = false;
        }
    }
    return entry;
}

void FileWatcher::watch(const std::filesystem::path& path) { entries_[path.string()] = stat(path); }

void FileWatcher::unwatch(const std::filesystem::path& path) { entries_.erase(path.string()); }

void FileWatcher::clear() { entries_.clear(); }

std::vector<std::filesystem::path> FileWatcher::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - lastPoll_).count() < interval_) {
        return {};
    }
    lastPoll_ = now;
    return pollNow();
}

std::vector<std::filesystem::path> FileWatcher::pollNow() {
    std::vector<std::filesystem::path> changed;
    for (auto& [key, entry] : entries_) {
        const Entry current = stat(key);
        if (current.exists != entry.exists || (current.exists && current.mtime != entry.mtime)) {
            entry = current;
            changed.emplace_back(key);
        }
    }
    return changed;
}

} // namespace avgen
