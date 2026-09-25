#pragma once

// "Does this machine have what this project loads?" -- asked up front, so a test built on a project
// whose assets are gitignored SKIPs with the missing file named instead of failing half-loaded.
//
// The GitHub-hosted runner has none of the gitignored packs (docs/development/ci.md, "Coverage
// gaps"): no `assets/aliens`, no `assets/quaternius`, no `~/Desktop/*.mp3`. Without them the
// Glowmere Rook/Umbra benchmark still loads -- Rook is there with no rig, the trees are missing --
// and a Director or motion test then fails on numbers that only ever meant anything with the real
// scene. The answer to that is a SKIP, which the CI summary counts and names, never a pass that
// asserted nothing (docs/testing.md).
//
// The list is read from the project itself -- its audio, its scene, and every `"asset"` path the
// scene names -- so it cannot drift from what the loader will actually open.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace avgen::testsupport {

namespace detail {

inline void collectAssetPaths(const nlohmann::json& node, const std::filesystem::path& base,
                              std::vector<std::filesystem::path>& out) {
    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if (key == "asset" && value.is_string()) {
                out.push_back(base / value.get<std::string>());
            } else {
                collectAssetPaths(value, base, out);
            }
        }
    } else if (node.is_array()) {
        for (const auto& value : node) {
            collectAssetPaths(value, base, out);
        }
    }
}

inline nlohmann::json readJsonOrNull(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return nullptr;
    }
    return nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
}

} // namespace detail

// Every file the project at `project` loads that is not on this machine: its audio, its scene file,
// and each asset its scene references. Empty means the project can load whole.
inline std::vector<std::filesystem::path> missingProjectAssets(const std::filesystem::path& project) {
    namespace fs = std::filesystem;
    std::vector<fs::path> wanted{project};
    const nlohmann::json doc = detail::readJsonOrNull(project);
    const fs::path dir = project.parent_path();
    if (doc.is_object() && doc.contains("assets") && doc["assets"].is_object()) {
        const nlohmann::json& assets = doc["assets"];
        if (assets.contains("audio") && assets["audio"].is_object() && assets["audio"].contains("path") &&
            assets["audio"]["path"].is_string()) {
            wanted.push_back(dir / assets["audio"]["path"].get<std::string>());
        }
        if (assets.contains("scene") && assets["scene"].is_object()) {
            const nlohmann::json& scenePath = assets["scene"].value("path", nlohmann::json{});
            const nlohmann::json& inner = scenePath.is_object() ? scenePath.value("path", nlohmann::json{}) : scenePath;
            if (inner.is_string()) {
                const fs::path scene = dir / inner.get<std::string>();
                wanted.push_back(scene);
                const nlohmann::json sceneDoc = detail::readJsonOrNull(scene);
                if (!sceneDoc.is_discarded() && !sceneDoc.is_null()) {
                    detail::collectAssetPaths(sceneDoc, scene.parent_path(), wanted);
                }
            }
        }
    }
    std::vector<fs::path> missing;
    for (const fs::path& p : wanted) {
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            const fs::path normal = p.lexically_normal();
            if (std::find(missing.begin(), missing.end(), normal) == missing.end()) {
                missing.push_back(normal);
            }
        }
    }
    return missing;
}

// SKIPs the running test, naming the first missing file and how many more, unless everything the
// project loads is present. Call it first in the test, before the project is loaded.
inline void skipUnlessProjectAssetsPresent(const std::filesystem::path& project) {
    const auto missing = missingProjectAssets(project);
    if (!missing.empty()) {
        SKIP("needs local assets: " << project.filename().string() << " loads " << missing.front().string()
                                     << (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more"
                                                            : std::string{})
                                     << ", which this machine does not have (gitignored; see docs/development/ci.md)");
    }
}

// The Director's and the motion system's benchmark: Glowmere Valley 2 multicam, Rook and Umbra.
inline std::filesystem::path glowmereBenchmarkProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json";
}

inline void skipUnlessGlowmereBenchmarkAssetsPresent() {
    skipUnlessProjectAssetsPresent(glowmereBenchmarkProject());
}

} // namespace avgen::testsupport
