#pragma once

// Asset registry (milestone 0.7, ADR-017): loads each file once and hands out shared, immutable
// assets keyed by resolved path. Paths are resolved against a base directory (the project's
// directory) so scene files stay relocatable. Reloading a path produces a new asset object;
// consumers compare `version` to notice.

#include "assets/gltf_loader.hpp"
#include "core/error.hpp"
#include "scene/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace avgen::assets {

struct SceneAsset {
    std::filesystem::path path;   // resolved, absolute
    scene::Scene scene;           // meshes, textures, entities, lights, cameras as imported
    GltfLoadSummary summary;
    std::uint64_t version = 1;
};

struct ImageAsset {
    std::filesystem::path path;
    scene::TextureData image;
    std::uint64_t version = 1;
};

class AssetRegistry {
public:
    explicit AssetRegistry(std::filesystem::path baseDirectory = {});

    void setBaseDirectory(std::filesystem::path baseDirectory);
    [[nodiscard]] const std::filesystem::path& baseDirectory() const { return base_; }
    // Absolute paths pass through; relative ones are joined with the base directory (or the
    // current directory when no base is set). Normalised (lexically) but not required to exist.
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& path) const;
    // Inverse of resolve() for saving: relative to the base directory when possible.
    [[nodiscard]] std::filesystem::path relativise(const std::filesystem::path& path) const;

    // Cached loads. Errors are not cached (a failing path is retried on the next call).
    [[nodiscard]] Result<std::shared_ptr<const SceneAsset>> loadScene(const std::filesystem::path& path);
    [[nodiscard]] Result<std::shared_ptr<const ImageAsset>> loadImage(const std::filesystem::path& path, bool srgb);

    // Re-reads a file and replaces its cache entry (version + 1). Unknown paths load fresh.
    [[nodiscard]] Result<void> reload(const std::filesystem::path& path);
    void clear();

    [[nodiscard]] std::size_t sceneCount() const { return scenes_.size(); }
    [[nodiscard]] std::size_t imageCount() const { return images_.size(); }
    [[nodiscard]] std::vector<std::filesystem::path> loadedPaths() const;

private:
    [[nodiscard]] std::filesystem::path absoluteBase() const; // base (or cwd), absolute and canonical

    std::filesystem::path base_;
    std::map<std::string, std::shared_ptr<const SceneAsset>> scenes_;
    std::map<std::string, std::shared_ptr<const ImageAsset>> images_; // key: path + "|srgb"
};

} // namespace avgen::assets
