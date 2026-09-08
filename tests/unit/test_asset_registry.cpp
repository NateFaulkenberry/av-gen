// AssetRegistry: path resolution, cached loads keyed by resolved path, reload versioning.
#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "support/gltf_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path tempDir() {
    return std::filesystem::temp_directory_path();
}

std::filesystem::path writePng(const char* name) {
    const std::vector<std::uint8_t> pixels = {255, 0, 0,   255, 0,   255, 0,   255,
                                              0,   0, 255, 255, 255, 255, 255, 255};
    const auto path = tempDir() / (std::string("avgen_registry_") + name + ".png");
    REQUIRE(assets::writePng(path, 2, 2, pixels).has_value());
    return path;
}

} // namespace

TEST_CASE("AssetRegistry resolves and relativises paths", "[assets][registry]") {
    SECTION("no base directory: relative paths join the current directory") {
        assets::AssetRegistry registry;
        CHECK(registry.baseDirectory().empty());
        const auto resolved = registry.resolve("models/thing.glb");
        CHECK(resolved.is_absolute());
        CHECK(resolved ==
              std::filesystem::weakly_canonical(std::filesystem::current_path()) / "models/thing.glb");
        CHECK(registry.resolve("/abs/x.glb") == std::filesystem::path("/abs/x.glb"));
        CHECK(registry.relativise(std::filesystem::current_path() / "models/thing.glb") ==
              std::filesystem::path("models/thing.glb"));
    }
    SECTION("with a base directory") {
        const auto base = tempDir() / "avgen_registry_base";
        std::filesystem::create_directories(base);
        assets::AssetRegistry registry(base);
        CHECK(registry.baseDirectory() == base);
        // Missing files normalise lexically; nothing is required to exist.
        CHECK(registry.resolve("a/../b.glb") == std::filesystem::weakly_canonical(base) / "b.glb");
        CHECK(registry.relativise("sub/x.glb") == std::filesystem::path("sub/x.glb"));
        // Existing files are canonicalised (symlinked temp directories on macOS) and still
        // relativise back to their name.
        const auto glb = testsupport::writeTriangleGlb("registry_base");
        const auto inBase = base / "tri.glb";
        std::filesystem::copy_file(glb, inBase, std::filesystem::copy_options::overwrite_existing);
        CHECK(registry.resolve("tri.glb") == std::filesystem::weakly_canonical(inBase));
        CHECK(registry.relativise(registry.resolve("tri.glb")) == std::filesystem::path("tri.glb"));
        CHECK(registry.relativise("tri.glb") == std::filesystem::path("tri.glb"));
        // Outside the base: absolute path.
        const auto outside = registry.relativise("/somewhere/else.glb");
        CHECK(outside.is_absolute());
        CHECK(outside == std::filesystem::path("/somewhere/else.glb"));
        registry.setBaseDirectory(tempDir());
        CHECK(registry.baseDirectory() == tempDir());
        std::filesystem::remove_all(base);
        std::filesystem::remove(glb);
    }
}

TEST_CASE("AssetRegistry caches scenes by resolved path and does not cache errors", "[assets][registry]") {
    const auto glb = testsupport::writeTriangleGlb("registry_scene");
    assets::AssetRegistry registry(tempDir());

    auto first = registry.loadScene(glb);
    REQUIRE(first.has_value());
    CHECK((*first)->version == 1);
    CHECK((*first)->scene.meshes.size() == 1);
    CHECK((*first)->scene.entities.size() == 1);
    CHECK((*first)->summary.lights == 1);
    CHECK((*first)->path == registry.resolve(glb));

    // Same file through a relative spelling: same object.
    auto second = registry.loadScene(glb.filename());
    REQUIRE(second.has_value());
    CHECK(second->get() == first->get());
    CHECK(registry.sceneCount() == 1);

    // Errors are not cached.
    CHECK_FALSE(registry.loadScene("avgen_registry_missing.glb").has_value());
    CHECK(registry.sceneCount() == 1);
    CHECK_FALSE(registry.reload("avgen_registry_missing.glb").has_value());
    CHECK(registry.sceneCount() == 1);

    // Reload replaces the entry with a new object at version + 1; the old handle stays valid.
    REQUIRE(registry.reload(glb).has_value());
    auto third = registry.loadScene(glb);
    REQUIRE(third.has_value());
    CHECK(third->get() != first->get());
    CHECK((*third)->version == 2);
    CHECK((*first)->version == 1);
    CHECK(registry.sceneCount() == 1);

    // Reloading an unknown scene path loads it fresh.
    registry.clear();
    CHECK(registry.sceneCount() == 0);
    REQUIRE(registry.reload(glb).has_value());
    CHECK(registry.sceneCount() == 1);
    std::filesystem::remove(glb);
}

TEST_CASE("AssetRegistry caches images per colour space and lists loaded paths", "[assets][registry]") {
    const auto glb = testsupport::writeTriangleGlb("registry_list");
    const auto png = writePng("image");
    assets::AssetRegistry registry(tempDir());

    auto srgb = registry.loadImage(png, true);
    REQUIRE(srgb.has_value());
    CHECK((*srgb)->image.format == scene::TextureFormat::Rgba8Srgb);
    CHECK((*srgb)->image.width == 2);
    auto srgbAgain = registry.loadImage(png.filename(), true);
    REQUIRE(srgbAgain.has_value());
    CHECK(srgbAgain->get() == srgb->get());
    auto linear = registry.loadImage(png, false);
    REQUIRE(linear.has_value());
    CHECK(linear->get() != srgb->get());
    CHECK((*linear)->image.format == scene::TextureFormat::Rgba8Unorm);
    CHECK(registry.imageCount() == 2);
    CHECK_FALSE(registry.loadImage("avgen_registry_missing.png", true).has_value());
    CHECK(registry.imageCount() == 2);

    REQUIRE(registry.loadScene(glb).has_value());
    const auto paths = registry.loadedPaths();
    REQUIRE(paths.size() == 2); // the scene, then the image once (both colour spaces)
    CHECK(paths[0] == registry.resolve(glb));
    CHECK(paths[1] == registry.resolve(png));

    // Reloading an image refreshes both colour-space variants.
    REQUIRE(registry.reload(png).has_value());
    auto srgb2 = registry.loadImage(png, true);
    auto linear2 = registry.loadImage(png, false);
    REQUIRE(srgb2.has_value());
    REQUIRE(linear2.has_value());
    CHECK((*srgb2)->version == 2);
    CHECK((*linear2)->version == 2);
    CHECK(srgb2->get() != srgb->get());
    CHECK(registry.imageCount() == 2);

    registry.clear();
    CHECK(registry.imageCount() == 0);
    CHECK(registry.sceneCount() == 0);
    CHECK(registry.loadedPaths().empty());
    std::filesystem::remove(glb);
    std::filesystem::remove(png);
}
