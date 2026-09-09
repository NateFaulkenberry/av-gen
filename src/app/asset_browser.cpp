#include "app/asset_browser.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace avgen::app {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Reads "format", "name", "category" and "description" from a JSON file without loading the rest
// into memory twice; returns false when the file is not JSON.
bool readJsonMeta(const std::filesystem::path& path, std::string& format, std::string& name, std::string& category,
                  std::string& description) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }
    const auto str = [&](const char* key) {
        return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
    };
    format = str("format");
    name = str("name");
    category = str("category");
    description = str("description");
    return true;
}

} // namespace

const char* assetKindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Project: return "project";
    case AssetKind::Scene: return "scene";
    case AssetKind::Graph: return "graph";
    case AssetKind::Preset: return "preset";
    case AssetKind::Model: return "model";
    case AssetKind::Environment: return "environment";
    case AssetKind::Shader: return "shader";
    case AssetKind::Audio: return "audio";
    case AssetKind::Unknown: break;
    }
    return "unknown";
}

AssetKind assetKindForFile(const std::filesystem::path& path) {
    const std::string ext = lower(path.extension().string());
    if (ext == ".gltf" || ext == ".glb") {
        return AssetKind::Model;
    }
    if (ext == ".hdr" || ext == ".exr") {
        return AssetKind::Environment;
    }
    if (ext == ".wgsl" || ext == ".fs" || ext == ".frag") {
        return AssetKind::Shader;
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".m4a") {
        return AssetKind::Audio;
    }
    if (ext != ".json") {
        return AssetKind::Unknown;
    }
    std::string format;
    std::string name;
    std::string category;
    std::string description;
    if (!readJsonMeta(path, format, name, category, description)) {
        return AssetKind::Unknown;
    }
    if (format == "avgen-project") {
        return AssetKind::Project;
    }
    if (format == "avgen-scene") {
        return AssetKind::Scene;
    }
    if (format == "avgen-graph") {
        return AssetKind::Graph;
    }
    if (format == "avgen-preset") {
        return AssetKind::Preset;
    }
    return AssetKind::Unknown;
}

bool AssetEntry::operator<(const AssetEntry& other) const {
    if (category != other.category) {
        return category < other.category;
    }
    return name < other.name;
}

std::filesystem::path thumbnailPathFor(const std::filesystem::path& asset) {
    std::filesystem::path out = asset;
    out.replace_extension();
    out += ".thumb.png";
    return out;
}

std::vector<AssetEntry> scanAssets(const std::vector<std::filesystem::path>& dirs) {
    std::vector<AssetEntry> out;
    std::error_code ec;
    for (const std::filesystem::path& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        auto it = std::filesystem::recursive_directory_iterator(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            const std::filesystem::path& p = it->path();
            const std::string stem = p.filename().string();
            if (!stem.empty() && (stem.front() == '.' || stem == "build")) {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it.depth() > 4) {
                it.disable_recursion_pending();
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }
            if (p.string().find(".thumb.png") != std::string::npos) {
                continue;
            }
            const AssetKind kind = assetKindForFile(p);
            if (kind == AssetKind::Unknown) {
                continue;
            }
            AssetEntry entry;
            entry.kind = kind;
            entry.path = p;
            entry.bytes = std::filesystem::file_size(p, ec);
            entry.name = p.stem().string();
            entry.category = p.parent_path().filename().string();
            if (p.extension() == ".json") {
                std::string format;
                std::string name;
                std::string category;
                std::string description;
                if (readJsonMeta(p, format, name, category, description)) {
                    if (!name.empty()) {
                        entry.name = name;
                    }
                    if (!category.empty()) {
                        entry.category = category;
                    }
                    entry.description = description;
                }
            }
            const std::filesystem::path thumb = thumbnailPathFor(p);
            if (std::filesystem::exists(thumb, ec)) {
                entry.thumbnail = thumb;
            }
            out.push_back(std::move(entry));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<const AssetEntry*> filterAssets(const std::vector<AssetEntry>& assets, AssetKind kind,
                                            std::string_view search) {
    const std::string needle = lower(search);
    std::vector<const AssetEntry*> out;
    for (const AssetEntry& a : assets) {
        if (kind != AssetKind::Unknown && a.kind != kind) {
            continue;
        }
        if (!needle.empty()) {
            const std::string haystack = lower(a.name + " " + a.category + " " + a.description);
            if (haystack.find(needle) == std::string::npos) {
                continue;
            }
        }
        out.push_back(&a);
    }
    return out;
}

} // namespace avgen::app
