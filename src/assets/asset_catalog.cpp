#include "assets/asset_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <system_error>

namespace avgen::assets {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

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

// True when `path` is `root` or below it; both already canonical.
bool below(const fs::path& path, const fs::path& root) {
    if (root.empty()) return false;
    const fs::path rel = path.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

// The keys an entry or the document holds that this build reads; everything else is kept.
constexpr const char* kEntryKeys[] = {"id", "type", "path", "size", "sha256", "source", "import"};
constexpr const char* kDocumentKeys[] = {"format", "version", "assets"};

template <std::size_t N>
json extrasOf(const json& object, const char* const (&known)[N]) {
    json out = json::object();
    for (const auto& [key, value] : object.items()) {
        if (std::find_if(std::begin(known), std::end(known), [&](const char* k) { return key == k; }) ==
            std::end(known)) {
            out[key] = value;
        }
    }
    return out;
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

// ---- the manifest ---------------------------------------------------------------------------------

fs::path manifestPath(const fs::path& projectFolder) { return projectFolder / "assets" / "manifest.json"; }

std::string projectAssetIdForManifestPath(const std::string& relativePath) {
    const fs::path p = fs::path(relativePath).lexically_normal();
    const fs::path rel = p.lexically_relative("assets");
    if (p.is_absolute() || rel.empty() || *rel.begin() == ".." || rel == ".") {
        return {};
    }
    return "asset://project/" + rel.generic_string();
}

ManifestEntry* AssetManifest::findByPath(const std::string& relativePath) {
    const std::string want = fs::path(relativePath).lexically_normal().generic_string();
    for (ManifestEntry& e : assets) {
        if (fs::path(e.path).lexically_normal().generic_string() == want) return &e;
    }
    return nullptr;
}

const ManifestEntry* AssetManifest::findByPath(const std::string& relativePath) const {
    return const_cast<AssetManifest*>(this)->findByPath(relativePath);
}

void AssetManifest::upsert(ManifestEntry entry) {
    if (ManifestEntry* existing = findByPath(entry.path)) {
        if (entry.extra.empty()) entry.extra = existing->extra;
        *existing = std::move(entry);
        return;
    }
    assets.push_back(std::move(entry));
}

json AssetManifest::toJson() const {
    json doc = extra.is_object() ? extra : json::object();
    doc["format"] = kManifestFormat;
    doc["version"] = kManifestVersion;
    json list = json::array();
    for (const ManifestEntry& e : assets) {
        json item = e.extra.is_object() ? e.extra : json::object();
        item["id"] = e.id;
        item["type"] = e.type;
        item["path"] = e.path;
        item["size"] = e.size;
        item["sha256"] = e.sha256;
        if (!e.source.is_null()) item["source"] = e.source;
        if (!e.import.is_null()) item["import"] = e.import;
        list.push_back(std::move(item));
    }
    doc["assets"] = std::move(list);
    return doc;
}

Result<AssetManifest> readManifest(const fs::path& projectFolder) {
    AssetManifest manifest;
    const fs::path file = manifestPath(projectFolder);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return manifest;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail("cannot read '{}'", file.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != kManifestFormat ||
        !doc.contains("assets") || !doc["assets"].is_array()) {
        return fail("'{}' is not an {} manifest", file.string(), kManifestFormat);
    }
    const int version = doc.contains("version") && doc["version"].is_number_integer() ? doc["version"].get<int>() : 1;
    if (version < 1 || version > kManifestVersion) {
        return fail("'{}' is manifest version {}; this build reads 1 to {}", file.string(), version, kManifestVersion);
    }
    manifest.versionRead = version;
    manifest.extra = extrasOf(doc, kDocumentKeys);
    for (const json& item : doc["assets"]) {
        if (!item.is_object()) {
            continue; // not an entry anything could have written; dropped on the next write
        }
        ManifestEntry e;
        e.id = item.value("id", std::string{});
        e.type = item.value("type", std::string{});
        e.path = item.value("path", std::string{});
        e.sha256 = item.value("sha256", std::string{});
        if (item.contains("size") && item["size"].is_number_unsigned()) {
            e.size = item["size"].get<std::uint64_t>();
        } else {
            // Version 1 kept no size; the file's own is the truth until it is rewritten.
            const auto bytes = fs::file_size(projectFolder / e.path, ec);
            e.size = ec ? 0 : static_cast<std::uint64_t>(bytes);
        }
        if (item.contains("source")) e.source = item["source"];
        if (item.contains("import")) e.import = item["import"];
        e.extra = extrasOf(item, kEntryKeys);
        // Matched by path: the id is whatever the path says it is (a v1 id lacked the extension).
        if (const std::string canonical = projectAssetIdForManifestPath(e.path); !canonical.empty() && canonical != e.id) {
            e.id = canonical;
            manifest.corrected = true;
        }
        manifest.assets.push_back(std::move(e));
    }
    if (version < kManifestVersion) {
        manifest.corrected = true; // the next write upgrades it
    }
    return manifest;
}

// ---- the catalogue --------------------------------------------------------------------------------

Result<std::vector<AssetRecord>> catalogAssets(const std::vector<fs::path>& contentRoots,
                                               const fs::path& projectRoot, std::size_t limit) {
    std::vector<AssetRecord> records;
    std::error_code ec;
    const fs::path canonicalProject = projectRoot.empty() ? fs::path{} : fs::weakly_canonical(projectRoot, ec);
    const fs::path projectAssets = canonicalProject.empty() ? fs::path{} : canonicalProject / "assets";
    // The manifest's entries by canonical file, for their tags. A manifest that cannot be read lists the files
    // without them: the catalogue is a listing, and the import tool is the one that refuses to overwrite it.
    std::map<std::string, const ManifestEntry*> manifestByFile;
    AssetManifest manifest;
    if (!projectRoot.empty()) {
        if (auto read = readManifest(projectRoot)) {
            manifest = std::move(*read);
            for (const ManifestEntry& e : manifest.assets) {
                const fs::path file = fs::weakly_canonical(projectRoot / e.path, ec);
                if (!ec) manifestByFile[file.string()] = &e;
            }
        }
    }
    for (const auto& root : contentRoots) {
        const auto canonicalRoot = fs::weakly_canonical(root, ec);
        if (ec || !fs::is_directory(canonicalRoot, ec)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(
                 canonicalRoot, fs::directory_options::skip_permission_denied, ec)) {
            if (records.size() >= limit) break;
            if (!entry.is_regular_file(ec)) continue;
            const std::string filename = entry.path().filename().string();
            if (filename == "manifest.json" || filename.starts_with(".")) continue; // the lock file; staging copies
            const std::string type = typeFor(lower(entry.path().extension().string()));
            if (type.empty()) continue;
            const auto path = fs::weakly_canonical(entry.path(), ec);
            if (ec) continue;
            AssetRecord record;
            record.type = type;
            record.path = path;
            record.name = path.stem().string();
            if (below(path, projectAssets)) {
                // The one id: what AssetRegistry::assetId gives for the project folder.
                record.source = AssetSource::Project;
                record.id = "asset://project/" + path.lexically_relative(projectAssets).generic_string();
            } else {
                record.source = below(path, canonicalProject) ? AssetSource::Project : AssetSource::Builtin;
                record.id = path.string(); // no asset id: named by the path every tool accepts
            }
            record.tags.push_back(record.type);
            if (const auto m = manifestByFile.find(path.string()); m != manifestByFile.end()) {
                if (!m->second->type.empty() && m->second->type != record.type) record.tags.push_back(m->second->type);
                if (m->second->source.is_object() && m->second->source.contains("tool") &&
                    m->second->source["tool"].is_string()) {
                    record.tags.push_back(m->second->source["tool"].get<std::string>());
                }
            }
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
