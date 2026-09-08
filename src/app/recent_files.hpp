#pragma once

// Most-recently-used project list for the File menu (milestone 0.9). Entries are absolute,
// lexically normal paths, newest first, persisted as a small JSON file:
//   { "format": "avgen-recent", "version": 1, "entries": [ "/abs/path/a.json", ... ] }

#include "core/error.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace avgen::app {

class RecentFiles {
public:
    explicit RecentFiles(std::filesystem::path storeFile, std::size_t maxEntries = 10);

    // A missing store file yields an empty list (not an error). A malformed file is an error and
    // leaves the current entries untouched.
    Result<void> load();
    // Creates the parent directories of the store file when needed.
    Result<void> save() const;

    // Normalises to an absolute, lexically normal path; moves an existing entry to the front;
    // trims to the maximum. An empty path is ignored.
    void add(const std::filesystem::path& path);
    // Returns true when the (normalised) path was present.
    bool remove(const std::filesystem::path& path);
    void clear();
    // Drops entries whose file no longer exists.
    void pruneMissing();

    [[nodiscard]] const std::vector<std::filesystem::path>& entries() const { return entries_; }
    [[nodiscard]] const std::filesystem::path& storeFile() const { return storeFile_; }
    [[nodiscard]] std::size_t maxEntries() const { return maxEntries_; }

private:
    static std::filesystem::path normalise(const std::filesystem::path& path);
    void trim();

    std::filesystem::path storeFile_;
    std::size_t maxEntries_;
    std::vector<std::filesystem::path> entries_;
};

} // namespace avgen::app
