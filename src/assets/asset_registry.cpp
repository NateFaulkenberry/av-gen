#include "assets/asset_registry.hpp"

#include "assets/image.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace avgen::assets {

namespace {

constexpr const char* kSrgbSuffix = "|srgb";
constexpr const char* kLinearSuffix = "|linear";

std::string imageKey(const std::filesystem::path& resolved, bool srgb) {
    return resolved.string() + (srgb ? kSrgbSuffix : kLinearSuffix);
}

// Strips the "|srgb" / "|linear" suffix from an image cache key.
std::string imagePathFromKey(const std::string& key) {
    const auto bar = key.rfind('|');
    return bar == std::string::npos ? key : key.substr(0, bar);
}

bool isSceneExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".gltf" || ext == ".glb";
}

// Canonical form of a path that may or may not exist: symlinks resolved when it does (macOS's
// /tmp and /var are symlinks), lexical normalisation otherwise.
std::filesystem::path canonicalise(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return canonical;
        }
    }
    return path.lexically_normal();
}

} // namespace

AssetRegistry::AssetRegistry(std::filesystem::path baseDirectory)
    : base_(std::move(baseDirectory)) {}

void AssetRegistry::setBaseDirectory(std::filesystem::path baseDirectory) {
    base_ = std::move(baseDirectory);
}

std::filesystem::path AssetRegistry::absoluteBase() const {
    std::error_code ec;
    std::filesystem::path base = base_.empty() ? std::filesystem::current_path(ec) : base_;
    if (base.is_relative()) {
        base = std::filesystem::current_path(ec) / base;
    }
    return canonicalise(base);
}

std::filesystem::path AssetRegistry::resolve(const std::filesystem::path& path) const {
    const std::string text = path.generic_string();
    if (text.rfind("asset://project/", 0) == 0) {
        return canonicalise(absoluteBase() / "assets" / text.substr(std::string("asset://project/").size()));
    }
    if (text.rfind("asset://builtin/", 0) == 0) {
        return canonicalise(absoluteBase() / text.substr(std::string("asset://builtin/").size()));
    }
    if (path.is_absolute()) {
        return canonicalise(path);
    }
    return canonicalise(absoluteBase() / path);
}

std::filesystem::path AssetRegistry::relativise(const std::filesystem::path& path) const {
    const std::filesystem::path resolved = resolve(path);
    const std::filesystem::path relative = resolved.lexically_relative(absoluteBase());
    if (relative.empty() || relative.native().starts_with("..")) {
        return resolved;
    }
    return relative;
}

std::string AssetRegistry::assetId(const std::filesystem::path& path) const {
    const auto resolved = resolve(path);
    const auto assetsRoot = canonicalise(absoluteBase() / "assets");
    const auto relative = resolved.lexically_relative(assetsRoot);
    if (relative.empty() || relative.native().starts_with("..")) {
        return {};
    }
    return "asset://project/" + relative.generic_string();
}

Result<std::shared_ptr<const SceneAsset>> AssetRegistry::loadScene(const std::filesystem::path& path) {
    const std::filesystem::path resolved = resolve(path);
    const std::string key = resolved.string();
    if (const auto it = scenes_.find(key); it != scenes_.end()) {
        return it->second;
    }
    auto asset = std::make_shared<SceneAsset>();
    asset->path = resolved;
    auto summary = loadGltf(resolved, asset->scene);
    if (!summary) {
        return std::unexpected(summary.error());
    }
    asset->summary = std::move(*summary);
    asset->version = 1;
    scenes_[key] = asset;
    return asset;
}

Result<std::shared_ptr<const ImageAsset>> AssetRegistry::loadImage(const std::filesystem::path& path,
                                                                   bool srgb) {
    const std::filesystem::path resolved = resolve(path);
    const std::string key = imageKey(resolved, srgb);
    if (const auto it = images_.find(key); it != images_.end()) {
        return it->second;
    }
    auto image = assets::loadImage(resolved, srgb);
    if (!image) {
        return std::unexpected(image.error());
    }
    auto asset = std::make_shared<ImageAsset>();
    asset->path = resolved;
    asset->image = std::move(*image);
    asset->version = 1;
    images_[key] = asset;
    return asset;
}

Result<void> AssetRegistry::reload(const std::filesystem::path& path) {
    const std::filesystem::path resolved = resolve(path);
    const std::string key = resolved.string();
    bool known = false;

    if (const auto it = scenes_.find(key); it != scenes_.end()) {
        known = true;
        auto fresh = std::make_shared<SceneAsset>();
        fresh->path = resolved;
        auto summary = loadGltf(resolved, fresh->scene);
        if (!summary) {
            return std::unexpected(summary.error());
        }
        fresh->summary = std::move(*summary);
        fresh->version = it->second->version + 1;
        it->second = fresh;
        log::info("reloaded scene asset '{}' (version {})", resolved.filename().string(), fresh->version);
    }
    for (const bool srgb : {true, false}) {
        const auto it = images_.find(imageKey(resolved, srgb));
        if (it == images_.end()) {
            continue;
        }
        known = true;
        auto image = assets::loadImage(resolved, srgb);
        if (!image) {
            return std::unexpected(image.error());
        }
        auto fresh = std::make_shared<ImageAsset>();
        fresh->path = resolved;
        fresh->image = std::move(*image);
        fresh->version = it->second->version + 1;
        it->second = fresh;
        log::info("reloaded image asset '{}' (version {})", resolved.filename().string(), fresh->version);
    }
    if (known) {
        return {};
    }
    // Unknown path: load it fresh, choosing the asset type by extension.
    if (isSceneExtension(resolved)) {
        auto scene = loadScene(resolved);
        if (!scene) {
            return std::unexpected(scene.error());
        }
        return {};
    }
    auto image = loadImage(resolved, true);
    if (!image) {
        return std::unexpected(image.error());
    }
    return {};
}

void AssetRegistry::clear() {
    scenes_.clear();
    images_.clear();
}

std::vector<std::filesystem::path> AssetRegistry::loadedPaths() const {
    std::vector<std::filesystem::path> paths;
    paths.reserve(scenes_.size() + images_.size());
    for (const auto& [key, asset] : scenes_) { // std::map: keys already sorted
        paths.emplace_back(key);
    }
    std::string last;
    for (const auto& [key, asset] : images_) {
        std::string path = imagePathFromKey(key);
        if (path == last) {
            continue; // both srgb variants of one file
        }
        last = path;
        paths.emplace_back(std::move(path));
    }
    return paths;
}

} // namespace avgen::assets
