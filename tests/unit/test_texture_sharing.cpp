// ADR-2110: glTF images referenced by URI carry their resolved file, and a flattened scene holds one texture per file
// (and format) however many assets reference it.

#include "assets/gltf_loader.hpp"
#include "assets/image.hpp"
#include "scene/asset_parts.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// One textured triangle whose base colour (and, when `withNormal`, normal) texture is the image at `uri`.
void writeTriangle(const fs::path& dir, const std::string& uri, bool withNormal) {
    fs::create_directories(dir);
    const std::array<float, 15> data{0, 0, 0, 1, 0, 0, 0, 1, 0, /* uv */ 0, 0, 1, 0, 0, 1};
    std::ofstream(dir / "tri.bin", std::ios::binary).write(reinterpret_cast<const char*>(data.data()), sizeof(data));
    const std::string normal = withNormal ? R"(, "normalTexture": {"index": 0})" : "";
    std::ofstream(dir / "tri.gltf") << R"({"asset": {"version": "2.0"},
  "buffers": [{"uri": "tri.bin", "byteLength": 60}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, {"buffer": 0, "byteOffset": 36, "byteLength": 24}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
                {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"}],
  "images": [{"uri": ")" << uri << R"("}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}})" << normal << R"(}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "material": 0}]}],
  "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0})";
}

} // namespace

TEST_CASE("assets that reference one image file share its texture", "[unit][assets][adr2110]") {
    const fs::path root = fs::temp_directory_path() / "avgen-adr2110";
    fs::remove_all(root);
    fs::create_directories(root / "tex");
    std::vector<std::uint8_t> rgba(4 * 4 * 4, 200);
    REQUIRE(assets::writePng(root / "tex" / "atlas.png", 4, 4, rgba));
    // two assets in different folders naming the same file by different relative paths, and a third with its own
    writeTriangle(root / "a", "../tex/atlas.png", false);
    writeTriangle(root / "b" / "deep", "../../tex/./atlas.png", true);
    REQUIRE(assets::writePng(root / "tex" / "other.png", 4, 4, rgba));
    writeTriangle(root / "c", "../tex/other.png", false);

    scene::Scene a;
    scene::Scene b;
    scene::Scene c;
    REQUIRE(assets::loadGltf(root / "a" / "tri.gltf", a));
    REQUIRE(assets::loadGltf(root / "b" / "deep" / "tri.gltf", b));
    REQUIRE(assets::loadGltf(root / "c" / "tri.gltf", c));
    REQUIRE(a.textures.size() == 1);
    REQUIRE(b.textures.size() == 2); // base colour (sRGB) and normal (linear): two formats of one file
    CHECK_FALSE(a.textures[0].source.empty());
    CHECK(a.textures[0].source == b.textures[0].source);
    CHECK(a.textures[0].source != c.textures[0].source);

    std::vector<scene::TextureData> sceneTextures;
    scene::TextureSharing sharing;
    const auto ta = sharing.add(sceneTextures, a.textures);
    const auto tb = sharing.add(sceneTextures, b.textures);
    const auto tc = sharing.add(sceneTextures, c.textures);
    // a's atlas, b's normal-map copy of it (another format), c's own file: three, not four
    CHECK(sceneTextures.size() == 3);
    CHECK(sharing.shared() == 1);
    const auto sameFormat = [&](std::size_t i) { return b.textures[i].format == a.textures[0].format; };
    const std::size_t bSrgb = sameFormat(0) ? 0 : 1;
    CHECK(tb[bSrgb] == ta[0]);
    CHECK(tb[1 - bSrgb] != ta[0]);
    CHECK(tc[0] != ta[0]);

    // an entity's references go through the table
    scene::Entity e = b.entities.at(0);
    scene::remapEntityTextures(e, tb);
    CHECK(e.material.baseColorTexture.texture == tb[b.entities[0].material.baseColorTexture.texture]);
    // embedded or generated data (no source) is never shared
    scene::TextureData generated = a.textures[0];
    generated.source.clear();
    const auto tg1 = sharing.add(sceneTextures, std::vector<scene::TextureData>{generated});
    const auto tg2 = sharing.add(sceneTextures, std::vector<scene::TextureData>{generated});
    CHECK(tg1[0] != tg2[0]);
    fs::remove_all(root);
}
