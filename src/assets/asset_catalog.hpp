#pragma once

// The asset catalogue (what a session can list) and the project's asset manifest, `assets/manifest.json`
// (ADR-2156, Engine API stage S3; the form is 03-phase0-api-contracts/01-api-contracts.md section 3.2).
//
// **One id.** A project asset's id is `asset://project/<path under <projectDir>/assets, with extension>`,
// exactly what `AssetRegistry::assetId` produces and `AssetRegistry::resolve` reads back. The tools resolve
// ids through `api::PathPolicy` (anchored to the open PROJECT's folder), never here. Anything that is not a
// project asset has no asset id: the catalogue names it by its absolute path, which every path tool accepts.
//
// **The manifest is the project's asset lock file.** Version 2:
//
//   { "format": "avgen-project-assets", "version": 2,
//     "assets": [ { "id", "type", "path", "size", "sha256", "source"?, "import"? } ] }
//
//   - `path` is relative to the project folder (`assets/models/column.glb`);
//   - `source` is what the client said about where the file came from (`asset.import`'s optional input);
//   - `import` is the engine's own load summary (`GltfLoadSummary` and what the loaded scene shows).
//
// Version 1 (`{id, type, path, sha256}`, written before ADR-2156) is read. Its ids were extensionless
// (`asset://project/models/column` for `column.glb`), which no resolver could map back to the file, so the
// reader matches each entry by `path` and reports the canonical id. That is a correction, not an alias: the
// next write is version 2 with the corrected ids, and nothing accepts the old spelling.
// Keys this build does not read are kept, at the top level and in each entry.

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::assets {

enum class AssetSource { Builtin, Project, External };

struct AssetRecord {
    std::string id; // `asset://project/...` for a project asset; the absolute path for anything else
    std::string name;
    std::string type; // the catalogue's vocabulary: model, environment, texture, audio, scene
    AssetSource source = AssetSource::External;
    std::filesystem::path path;
    std::vector<std::string> tags;
};

[[nodiscard]] const char* assetSourceName(AssetSource source);

// Scans only caller-authorized roots. `projectRoot` is used solely to classify files under the
// current project's assets directory; it never expands the readable roots or write permissions.
[[nodiscard]] Result<std::vector<AssetRecord>> catalogAssets(
    const std::vector<std::filesystem::path>& contentRoots,
    const std::filesystem::path& projectRoot = {}, std::size_t limit = 2000);

[[nodiscard]] std::vector<AssetRecord> searchAssets(const std::vector<AssetRecord>& records,
                                                    const std::string& query, std::size_t limit = 100);

// ---- the manifest ---------------------------------------------------------------------------------

inline constexpr const char* kManifestFormat = "avgen-project-assets";
inline constexpr int kManifestVersion = 2;

struct ManifestEntry {
    std::string id;
    std::string type; // the import folder: models, environments, textures
    std::string path; // relative to the project folder, generic separators
    std::uint64_t size = 0;
    std::string sha256;
    nlohmann::json source; // null, or the client's {file, tool, toolVersion, exportProfile, exportSettingsSha256}
    nlohmann::json import; // null, or the load summary
    nlohmann::json extra = nlohmann::json::object(); // keys this build does not read, kept verbatim
};

struct AssetManifest {
    int versionRead = 0; // 0: there was no file
    std::vector<ManifestEntry> assets;
    nlohmann::json extra = nlohmann::json::object();
    bool corrected = false; // an id was not the canonical one for its path (every v1 entry)

    [[nodiscard]] ManifestEntry* findByPath(const std::string& relativePath);
    [[nodiscard]] const ManifestEntry* findByPath(const std::string& relativePath) const;
    // Inserts or replaces the entry with the same path.
    void upsert(ManifestEntry entry);
    // Always the current version.
    [[nodiscard]] nlohmann::json toJson() const;
};

// `<projectFolder>/assets/manifest.json`.
[[nodiscard]] std::filesystem::path manifestPath(const std::filesystem::path& projectFolder);
// The canonical id for a manifest path (`assets/models/x.glb` -> `asset://project/models/x.glb`); empty when
// the path is not below `assets/` or climbs out of it.
[[nodiscard]] std::string projectAssetIdForManifestPath(const std::string& relativePath);
// Reads either version. A missing file is an empty manifest (versionRead 0); a file that is not a manifest
// is an error, so a caller never overwrites what it could not read.
[[nodiscard]] Result<AssetManifest> readManifest(const std::filesystem::path& projectFolder);

} // namespace avgen::assets
