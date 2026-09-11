#include "assets/asset_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <fstream>
#include <nlohmann/json.hpp>

namespace avgen::assets {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string typeFor(const std::string& ext) {
    if (ext == ".glb" || ext == ".gltf") return "model";
    if (ext == ".hdr" || ext == ".exr") return "environment";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".webp") return "texture";
    if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".aif" || ext == ".ogg" || ext == ".m4a") return "audio";
    if (ext == ".json") return "scene";
    return {};
}

std::string sourcePrefix(AssetSource source) {
    return source == AssetSource::Builtin ? "builtin" : source == AssetSource::Project ? "project" : "external";
}

} // namespace

const char* assetSourceName(AssetSource source) {
    switch (source) {
    case AssetSource::Builtin: return "builtin";
    case AssetSource::Project: return "project";
    case AssetSource::External: return "external";
    }
    return "external";
}

Result<std::vector<AssetRecord>> catalogAssets(const std::vector<std::filesystem::path>& contentRoots,
                                               const std::filesystem::path& projectRoot, std::size_t limit) {
    std::vector<AssetRecord> records;
    std::error_code ec;
    const auto projectAssets = projectRoot.empty() ? std::filesystem::path{} :
        std::filesystem::weakly_canonical(projectRoot / "assets", ec);
    std::map<std::string, AssetRecord> manifestRecords;
    if (!projectRoot.empty()) {
        std::ifstream in(projectRoot / "assets" / "manifest.json");
        if (in) {
            const nlohmann::json manifest = nlohmann::json::parse(in, nullptr, false);
            if (manifest.is_object() && manifest.value("format", std::string{}) == "avgen-project-assets" &&
                manifest["assets"].is_array()) {
                for (const auto& item : manifest["assets"]) {
                    if (!item.is_object() || !item.value("id", std::string{}).starts_with("asset://project/")) continue;
                    const std::filesystem::path path = std::filesystem::weakly_canonical(projectRoot / item.value("path", std::string{}), ec);
                    if (ec || !std::filesystem::is_regular_file(path, ec)) continue;
                    AssetRecord record;
                    record.id = item.value("id", std::string{});
                    record.name = path.stem().string();
                    record.type = item.value("type", std::string{});
                    record.source = AssetSource::Project;
                    record.path = path;
                    record.tags.push_back(record.type);
                    manifestRecords[path.string()] = std::move(record);
                }
            }
        }
    }
    for (const auto& root : contentRoots) {
        const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
        if (ec || !std::filesystem::is_directory(canonicalRoot, ec)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 canonicalRoot, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (records.size() >= limit) break;
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().filename() == "manifest.json") continue;
            const std::string type = typeFor(lower(entry.path().extension().string()));
            if (type.empty()) continue;
            const auto path = std::filesystem::weakly_canonical(entry.path(), ec);
            if (ec) continue;
            AssetSource source = AssetSource::Builtin;
            if (!projectAssets.empty() && path.lexically_relative(projectAssets).string().rfind("..", 0) != 0) {
                source = AssetSource::Project;
            } else if (canonicalRoot == projectRoot) {
                source = AssetSource::Project;
            }
            const auto relative = path.lexically_relative(canonicalRoot).generic_string();
            AssetRecord record;
            if (const auto manifest = manifestRecords.find(path.string()); manifest != manifestRecords.end()) {
                record = manifest->second;
            }
            record.source = source;
            if (record.type.empty()) record.type = type;
            record.path = path;
            if (record.name.empty()) record.name = path.stem().string();
            if (record.id.empty()) record.id = "asset://" + sourcePrefix(source) + "/" + relative;
            if (record.tags.empty()) record.tags.push_back(record.type);
            records.push_back(std::move(record));
        }
    }
    std::sort(records.begin(), records.end(), [](const AssetRecord& a, const AssetRecord& b) { return a.id < b.id; });
    records.erase(std::unique(records.begin(), records.end(), [](const AssetRecord& a, const AssetRecord& b) { return a.id == b.id; }), records.end());
    return records;
}

std::vector<AssetRecord> searchAssets(const std::vector<AssetRecord>& records, const std::string& query, std::size_t limit) {
    const std::string needle = lower(query);
    std::vector<AssetRecord> result;
    for (const auto& record : records) {
        if (needle.empty() || lower(record.id).find(needle) != std::string::npos ||
            lower(record.name).find(needle) != std::string::npos || lower(record.type).find(needle) != std::string::npos) {
            result.push_back(record);
            if (result.size() >= limit) break;
        }
    }
    return result;
}

} // namespace avgen::assets
