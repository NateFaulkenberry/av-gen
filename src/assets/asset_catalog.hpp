#pragma once

#include "core/error.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::assets {

enum class AssetSource { Builtin, Project, External };

struct AssetRecord {
    std::string id;
    std::string name;
    std::string type;
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

} // namespace avgen::assets
