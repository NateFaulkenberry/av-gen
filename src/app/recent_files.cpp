#include "app/recent_files.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>

namespace avgen::app {

using nlohmann::json;

namespace {
constexpr const char* kFormatName = "avgen-recent";
constexpr int kFormatVersion = 1;
} // namespace

RecentFiles::RecentFiles(std::filesystem::path storeFile, std::size_t maxEntries)
    : storeFile_(std::move(storeFile))
    , maxEntries_(std::max<std::size_t>(maxEntries, 1)) {}

std::filesystem::path RecentFiles::normalise(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    return absolute.lexically_normal();
}

void RecentFiles::trim() {
    if (entries_.size() > maxEntries_) {
        entries_.resize(maxEntries_);
    }
}

Result<void> RecentFiles::load() {
    std::error_code ec;
    if (!std::filesystem::exists(storeFile_, ec)) {
        entries_.clear();
        return {};
    }
    std::ifstream in(storeFile_);
    if (!in) {
        return fail("cannot open '{}' for reading", storeFile_.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail("'{}' is not a valid recent-files list", storeFile_.string());
    }
    const auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != kFormatName) {
        return fail("'{}' is not a recent-files list: expected format '{}'", storeFile_.string(),
                    kFormatName);
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        return fail("'{}' is missing an integer 'version'", storeFile_.string());
    }
    if (version->get<int>() > kFormatVersion || version->get<int>() < 1) {
        return fail("'{}' has unsupported recent-files version {}", storeFile_.string(), version->get<int>());
    }
    const auto entries = doc.find("entries");
    if (entries == doc.end() || !entries->is_array()) {
        return fail("'{}': 'entries' must be an array", storeFile_.string());
    }
    std::vector<std::filesystem::path> loaded;
    loaded.reserve(entries->size());
    for (const json& entry : *entries) {
        if (!entry.is_string()) {
            return fail("'{}': every entry must be a string", storeFile_.string());
        }
        std::filesystem::path path = std::filesystem::path(entry.get<std::string>()).lexically_normal();
        if (path.empty() || std::ranges::find(loaded, path) != loaded.end()) {
            continue;
        }
        loaded.push_back(std::move(path));
    }
    entries_ = std::move(loaded);
    trim();
    return {};
}

Result<void> RecentFiles::save() const {
    if (const auto parent = storeFile_.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return fail("cannot create '{}': {}", parent.string(), ec.message());
        }
    }
    json doc;
    doc["format"] = kFormatName;
    doc["version"] = kFormatVersion;
    json list = json::array();
    for (const auto& entry : entries_) {
        list.push_back(entry.string());
    }
    doc["entries"] = std::move(list);
    std::ofstream out(storeFile_);
    if (!out) {
        return fail("cannot open '{}' for writing", storeFile_.string());
    }
    out << doc.dump(2) << '\n';
    if (!out) {
        return fail("failed while writing '{}'", storeFile_.string());
    }
    return {};
}

void RecentFiles::add(const std::filesystem::path& path) {
    std::filesystem::path normal = normalise(path);
    if (normal.empty()) {
        return;
    }
    std::erase(entries_, normal);
    entries_.insert(entries_.begin(), std::move(normal));
    trim();
}

bool RecentFiles::remove(const std::filesystem::path& path) {
    const std::filesystem::path normal = normalise(path);
    if (normal.empty()) {
        return false;
    }
    return std::erase(entries_, normal) > 0;
}

void RecentFiles::clear() {
    entries_.clear();
}

void RecentFiles::pruneMissing() {
    std::erase_if(entries_, [](const std::filesystem::path& entry) {
        std::error_code ec;
        return !std::filesystem::exists(entry, ec);
    });
}

} // namespace avgen::app
