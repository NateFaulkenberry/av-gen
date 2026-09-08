#include "assets/gltf_loader.hpp"
#include "assets/image.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("avgen_gltf_test_") + name);
}

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const char* name)
        : path(tempPath(name)) {
        std::filesystem::remove(path);
    }
    ~TempFile() { std::filesystem::remove(path); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool approx(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(eps)));
}

// ---- GLB builder --------------------------------------------------------------------------------

// Accumulates the BIN chunk; every blob starts on a 4-byte boundary as the glTF spec requires.
struct BinBuilder {
    std::vector<std::uint8_t> bytes;
    nlohmann::json bufferViews = nlohmann::json::array();

    // Appends `data` and returns the bufferView index.
    template <typename T>
    std::size_t addView(const std::vector<T>& data) {
        while (bytes.size() % 4 != 0) {
            bytes.push_back(0);
        }
        const std::size_t offset = bytes.size();
        const std::size_t length = data.size() * sizeof(T);
        bytes.resize(offset + length);
        std::memcpy(bytes.data() + offset, data.data(), length);
        bufferViews.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}});
        return bufferViews.size() - 1;
    }
};

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

std::vector<std::uint8_t> packGlb(const std::string& json, const std::vector<std::uint8_t>& bin) {
    std::vector<std::uint8_t> jsonChunk(json.begin(), json.end());
    while (jsonChunk.size() % 4 != 0) {
        jsonChunk.push_back(' ');
    }
    std::vector<std::uint8_t> binChunk = bin;
    while (binChunk.size() % 4 != 0) {
        binChunk.push_back(0);
    }
    const auto total = static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + binChunk.size());
    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546C67u); // "glTF"
    appendU32(glb, 2u);
    appendU32(glb, total);
    appendU32(glb, static_cast<std::uint32_t>(jsonChunk.size()));
    appendU32(glb, 0x4E4F534Au); // "JSON"
    glb.insert(glb.end(), jsonChunk.begin(), jsonChunk.end());
    appendU32(glb, static_cast<std::uint32_t>(binChunk.size()));
    appendU32(glb, 0x004E4942u); // "BIN\0"
    glb.insert(glb.end(), binChunk.begin(), binChunk.end());
    REQUIRE(glb.size() == total);
    return glb;
}

// The 2x2 base colour texture embedded in the test asset.
const std::vector<std::uint8_t> kTexturePixels = {
    255, 0, 0,   255, 0,   255, 0,   255, //
    0,   0, 255, 255, 255, 255, 255, 128, //
};

// Builds the fixture asset:
//   node0 "parent"  T(1,2,3)         -> child node1 "child" S(2), mesh0 (full triangle, material0)
//   node2 "pointLight" T(5,0,0)      -> point light, 20 cd
//   node3 "spotLight"  T(0,3,0), R(-90 deg about X) -> spot light shining down -Y
//   node4 "camera"     T(4,1,0), R(90 deg about Y)  -> perspective camera looking down -X
//   node5 "noNormals"                -> mesh1 (position-only triangle, no material)
std::vector<std::uint8_t> buildFixtureGlb() {
    BinBuilder bin;
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::vector<float> normals = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const std::vector<float> uvs = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const std::vector<std::uint16_t> indices = {0, 1, 2};
    const auto png = assets::encodePng(2, 2, kTexturePixels);
    REQUIRE(png.has_value());

    const std::size_t posView = bin.addView(positions);
    const std::size_t nrmView = bin.addView(normals);
    const std::size_t uvView = bin.addView(uvs);
    const std::size_t idxView = bin.addView(indices);
    const std::size_t pngView = bin.addView(*png);

    using json = nlohmann::json;
    json doc;
    doc["asset"] = {{"version", "2.0"}, {"generator", "avgen test"}};
    doc["extensionsUsed"] = {"KHR_lights_punctual", "KHR_materials_emissive_strength"};
    doc["buffers"] = {{{"byteLength", bin.bytes.size()}}};
    doc["bufferViews"] = bin.bufferViews;
    doc["accessors"] = {
        {{"bufferView", posView},
         {"componentType", 5126},
         {"count", 3},
         {"type", "VEC3"},
         {"min", {0.0, 0.0, 0.0}},
         {"max", {1.0, 1.0, 0.0}}},
        {{"bufferView", nrmView}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", uvView}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}},
        {{"bufferView", idxView}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
    };
    doc["images"] = {{{"bufferView", pngView}, {"mimeType", "image/png"}, {"name", "checker"}}};
    doc["samplers"] = {{{"wrapS", 33648}, {"wrapT", 10497}, {"magFilter", 9728}, {"minFilter", 9729}}};
    doc["textures"] = {{{"source", 0}, {"sampler", 0}}};
    doc["materials"] = {{
        {"name", "mat"},
        {"pbrMetallicRoughness",
         {{"baseColorFactor", {0.2, 0.4, 0.6, 1.0}},
          {"metallicFactor", 0.3},
          {"roughnessFactor", 0.7},
          {"baseColorTexture", {{"index", 0}}}}},
        {"emissiveFactor", {1.0, 0.5, 0.0}},
        {"extensions", {{"KHR_materials_emissive_strength", {{"emissiveStrength", 4.0}}}}},
    }};
    doc["meshes"] = {
        {{"name", "triangle"},
         {"primitives",
          {{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
            {"indices", 3},
            {"material", 0}}}}},
        {{"name", "bare"}, {"primitives", {{{"attributes", {{"POSITION", 0}}}, {"indices", 3}}}}},
    };
    const double s = std::numbers::sqrt2 / 2.0; // sin/cos of 45 degrees
    doc["nodes"] = {
        {{"name", "parent"}, {"translation", {1.0, 2.0, 3.0}}, {"children", {1}}},
        {{"name", "child"}, {"scale", {2.0, 2.0, 2.0}}, {"mesh", 0}},
        {{"name", "pointLight"},
         {"translation", {5.0, 0.0, 0.0}},
         {"extensions", {{"KHR_lights_punctual", {{"light", 0}}}}}},
        {{"name", "spotLight"},
         {"translation", {0.0, 3.0, 0.0}},
         {"rotation", {-s, 0.0, 0.0, s}},
         {"extensions", {{"KHR_lights_punctual", {{"light", 1}}}}}},
        {{"name", "camera"}, {"translation", {4.0, 1.0, 0.0}}, {"rotation", {0.0, s, 0.0, s}}, {"camera", 0}},
        {{"name", "noNormals"}, {"mesh", 1}},
    };
    doc["extensions"] = {{"KHR_lights_punctual",
                          {{"lights",
                            {{{"type", "point"}, {"intensity", 20.0}, {"color", {1.0, 0.5, 0.25}}},
                             {{"type", "spot"},
                              {"intensity", 7.0},
                              {"range", 12.0},
                              {"spot", {{"innerConeAngle", 0.2}, {"outerConeAngle", 0.5}}}}}}}}};
    doc["cameras"] = {
        {{"type", "perspective"}, {"perspective", {{"yfov", 0.8}, {"znear", 0.05}, {"zfar", 150.0}}}}};
    doc["scenes"] = {{{"nodes", {0, 2, 3, 4, 5}}}};
    doc["scene"] = 0;
    return packGlb(doc.dump(), bin.bytes);
}

const scene::Entity& findEntity(const scene::Scene& s, const std::string& name) {
    for (const auto& e : s.entities) {
        if (e.name == name) {
            return e;
        }
    }
    FAIL("entity not found: " << name);
    return s.entities.front();
}

} // namespace

TEST_CASE("loadGltf imports the fixture asset", "[assets][gltf]") {
    TempFile file("fixture.glb");
    writeBytes(file.path, buildFixtureGlb());

    scene::Scene s;
    const auto result = assets::loadGltf(file.path, s);
    REQUIRE(result.has_value());
    const auto& summary = *result;
    CHECK(summary.meshes == 2);
    CHECK(summary.entities == 2);
    CHECK(summary.materials == 1);
    CHECK(summary.textures == 1);
    CHECK(summary.lights == 2);
    CHECK(summary.cameras == 1);
    CHECK(summary.warnings.empty());
    CHECK(s.meshes.size() == 2);
    CHECK(s.entities.size() == 2);
    CHECK(s.textures.size() == 1);
    CHECK(s.lights.size() == 2);
    CHECK(s.cameras.size() == 1);
    CHECK(s.meshVersion == 2);
    CHECK(s.textureVersion == 1);

    SECTION("hierarchy is flattened to world transforms") {
        const auto& child = findEntity(s, "child");
        CHECK(approx(child.transform.position, {1.0f, 2.0f, 3.0f}));
        CHECK(approx(child.transform.scale, {2.0f, 2.0f, 2.0f}));
        CHECK_THAT(static_cast<double>(child.transform.rotation.w), WithinAbs(1.0, 1e-5));
        const auto& bare = findEntity(s, "noNormals");
        CHECK(approx(bare.transform.position, {0.0f, 0.0f, 0.0f}));
        CHECK(approx(bare.transform.scale, {1.0f, 1.0f, 1.0f}));
    }

    SECTION("vertex data") {
        const auto& child = findEntity(s, "child");
        REQUIRE(child.mesh < s.meshes.size());
        const auto& mesh = s.meshes[child.mesh];
        CHECK(mesh.name == "child/prim0");
        CHECK(mesh.valid());
        REQUIRE(mesh.vertices.size() == 3);
        CHECK(approx(mesh.vertices[1].position, {1.0f, 0.0f, 0.0f}));
        CHECK(approx(mesh.vertices[2].position, {0.0f, 1.0f, 0.0f}));
        CHECK(approx(mesh.vertices[0].normal, {0.0f, 0.0f, 1.0f}));
        CHECK(mesh.vertices[1].uv == glm::vec2(1.0f, 0.0f));
        CHECK(mesh.vertices[2].uv == glm::vec2(0.0f, 1.0f));
        CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2});
    }

    SECTION("material factors and texture reference") {
        const auto& m = findEntity(s, "child").material;
        CHECK(approx(m.baseColor, {0.2f, 0.4f, 0.6f}));
        CHECK_THAT(static_cast<double>(m.opacity), WithinAbs(1.0, 1e-6));
        CHECK_THAT(static_cast<double>(m.metallic), WithinAbs(0.3, 1e-6));
        CHECK_THAT(static_cast<double>(m.roughness), WithinAbs(0.7, 1e-6));
        CHECK(approx(m.emissiveColor, {1.0f, 0.5f, 0.0f}));
        CHECK_THAT(static_cast<double>(m.emissiveIntensity), WithinAbs(4.0, 1e-6));
        CHECK(m.alphaMode == scene::AlphaMode::Opaque);
        CHECK_FALSE(m.doubleSided);
        CHECK_FALSE(m.unlit);
        REQUIRE(m.baseColorTexture.valid());
        CHECK(m.baseColorTexture.texture == 0);
        CHECK(m.baseColorTexture.uvSet == 0);
        CHECK(m.baseColorTexture.wrapU == scene::WrapMode::Mirror);
        CHECK(m.baseColorTexture.wrapV == scene::WrapMode::Repeat);
        CHECK_FALSE(m.baseColorTexture.linearFilter);
        CHECK_FALSE(m.metallicRoughnessTexture.valid());
        CHECK_FALSE(m.normalTexture.valid());
        CHECK_FALSE(m.emissiveTexture.valid());
        CHECK_FALSE(m.occlusionTexture.valid());

        // A primitive without a material gets the glTF default.
        const auto& d = findEntity(s, "noNormals").material;
        CHECK(d.baseColor == glm::vec3(1.0f));
        CHECK_THAT(static_cast<double>(d.roughness), WithinAbs(0.5, 1e-6));
        CHECK(d.metallic == 0.0f);
        CHECK(d.emissiveIntensity == 0.0f);
        CHECK_FALSE(d.baseColorTexture.valid());
    }

    SECTION("embedded texture is decoded as sRGB") {
        const auto& t = s.textures[0];
        CHECK(t.name == "checker");
        CHECK(t.width == 2);
        CHECK(t.height == 2);
        CHECK(t.format == scene::TextureFormat::Rgba8Srgb);
        CHECK(t.valid());
        CHECK(t.data == kTexturePixels);
    }

    SECTION("punctual lights") {
        const auto& point = s.lights[0];
        CHECK(point.name == "pointLight");
        CHECK(point.type == scene::PunctualLight::Type::Point);
        CHECK(approx(point.position, {5.0f, 0.0f, 0.0f}));
        CHECK_THAT(static_cast<double>(point.intensity), WithinAbs(20.0, 1e-6));
        CHECK(approx(point.color, {1.0f, 0.5f, 0.25f}));
        CHECK(point.range == 0.0f);
        CHECK(point.enabled);

        const auto& spot = s.lights[1];
        CHECK(spot.name == "spotLight");
        CHECK(spot.type == scene::PunctualLight::Type::Spot);
        CHECK(approx(spot.position, {0.0f, 3.0f, 0.0f}));
        CHECK(approx(spot.direction, {0.0f, -1.0f, 0.0f}));
        CHECK_THAT(static_cast<double>(spot.intensity), WithinAbs(7.0, 1e-6));
        CHECK_THAT(static_cast<double>(spot.range), WithinAbs(12.0, 1e-6));
        CHECK_THAT(static_cast<double>(spot.innerConeAngle), WithinAbs(0.2, 1e-6));
        CHECK_THAT(static_cast<double>(spot.outerConeAngle), WithinAbs(0.5, 1e-6));
    }

    SECTION("perspective camera; the active camera is left alone") {
        const auto& cam = s.cameras[0];
        CHECK(cam.name == "camera");
        CHECK(approx(cam.position, {4.0f, 1.0f, 0.0f}));
        CHECK(approx(cam.target - cam.position, {-1.0f, 0.0f, 0.0f}));
        CHECK(approx(cam.up, {0.0f, 1.0f, 0.0f}));
        CHECK_THAT(static_cast<double>(cam.fovYRadians), WithinAbs(0.8, 1e-6));
        CHECK_THAT(static_cast<double>(cam.nearPlane), WithinAbs(0.05, 1e-6));
        CHECK_THAT(static_cast<double>(cam.farPlane), WithinAbs(150.0, 1e-4));
        CHECK(s.camera.position == scene::Camera{}.position);
    }

    SECTION("bounds cover the transformed primitives") {
        // child: unit triangle scaled by 2 at (1,2,3) -> [1,2,3]..[3,4,3]; noNormals: [0,0,0]..[1,1,0]
        CHECK(approx(summary.boundsMin, {0.0f, 0.0f, 0.0f}));
        CHECK(approx(summary.boundsMax, {3.0f, 4.0f, 3.0f}));
        const auto [lo, hi] = s.bounds();
        CHECK(approx(lo, summary.boundsMin));
        CHECK(approx(hi, summary.boundsMax));
    }

    SECTION("a primitive without normals gets unit-length generated normals") {
        const auto& mesh = s.meshes[findEntity(s, "noNormals").mesh];
        REQUIRE(mesh.vertices.size() == 3);
        for (const auto& v : mesh.vertices) {
            CHECK_THAT(static_cast<double>(glm::length(v.normal)), WithinAbs(1.0, 1e-5));
            CHECK(approx(v.normal, {0.0f, 0.0f, 1.0f})); // counter-clockwise triangle in the XY plane
            CHECK(v.uv == glm::vec2(0.0f));
        }
    }
}

TEST_CASE("loadGltf options", "[assets][gltf]") {
    TempFile file("options.glb");
    writeBytes(file.path, buildFixtureGlb());

    SECTION("loadImages=false keeps factors but no textures") {
        scene::Scene s;
        assets::GltfLoadOptions options;
        options.loadImages = false;
        const auto result = assets::loadGltf(file.path, s, options);
        REQUIRE(result.has_value());
        CHECK(result->textures == 0);
        CHECK(s.textures.empty());
        const auto& m = findEntity(s, "child").material;
        CHECK_FALSE(m.baseColorTexture.valid());
        CHECK(approx(m.baseColor, {0.2f, 0.4f, 0.6f}));
    }

    SECTION("generateNormals=false leaves zero normals and warns") {
        scene::Scene s;
        assets::GltfLoadOptions options;
        options.generateNormals = false;
        const auto result = assets::loadGltf(file.path, s, options);
        REQUIRE(result.has_value());
        CHECK_FALSE(result->warnings.empty());
        const auto& mesh = s.meshes[findEntity(s, "noNormals").mesh];
        CHECK(mesh.vertices[0].normal == glm::vec3(0.0f));
    }

    SECTION("namePrefix is applied to entity names") {
        scene::Scene s;
        assets::GltfLoadOptions options;
        options.namePrefix = "fixture/";
        REQUIRE(assets::loadGltf(file.path, s, options).has_value());
        CHECK(s.entities[0].name.rfind("fixture/", 0) == 0);
        (void)findEntity(s, "fixture/child");
    }
}

TEST_CASE("loadGltf failures leave the scene untouched", "[assets][gltf]") {
    scene::Scene s;
    scene::MeshData tri;
    tri.name = "existing";
    tri.vertices = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                    {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    tri.indices = {0, 1, 2};
    const auto meshId = s.addMesh(tri);
    s.addEntity("existing", meshId);
    scene::TextureData tex;
    tex.width = 1;
    tex.height = 1;
    tex.data = {1, 2, 3, 4};
    s.addTexture(tex);
    const auto meshVersion = s.meshVersion;
    const auto textureVersion = s.textureVersion;

    const auto checkUntouched = [&] {
        CHECK(s.meshes.size() == 1);
        CHECK(s.entities.size() == 1);
        CHECK(s.textures.size() == 1);
        CHECK(s.lights.empty());
        CHECK(s.cameras.empty());
        CHECK(s.meshVersion == meshVersion);
        CHECK(s.textureVersion == textureVersion);
    };

    SECTION("missing path") {
        const auto result = assets::loadGltf(tempPath("missing.glb"), s);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().message.empty());
        checkUntouched();
    }

    SECTION("garbage file") {
        TempFile file("garbage.glb");
        writeBytes(file.path, std::vector<std::uint8_t>(64, 0x5A));
        const auto result = assets::loadGltf(file.path, s);
        REQUIRE_FALSE(result.has_value());
        CHECK_FALSE(result.error().message.empty());
        checkUntouched();
    }

    SECTION("truncated GLB") {
        TempFile file("truncated.glb");
        auto glb = buildFixtureGlb();
        glb.resize(glb.size() / 2);
        writeBytes(file.path, glb);
        const auto result = assets::loadGltf(file.path, s);
        REQUIRE_FALSE(result.has_value());
        checkUntouched();
    }
}

TEST_CASE("loadGltf twice appends with offset ids", "[assets][gltf]") {
    TempFile file("twice.glb");
    writeBytes(file.path, buildFixtureGlb());

    scene::Scene s;
    REQUIRE(assets::loadGltf(file.path, s).has_value());
    const std::size_t meshes1 = s.meshes.size();
    const std::size_t textures1 = s.textures.size();
    const std::size_t entities1 = s.entities.size();
    assets::GltfLoadOptions options;
    options.namePrefix = "second/";
    const auto second = assets::loadGltf(file.path, s, options);
    REQUIRE(second.has_value());
    CHECK(s.meshes.size() == meshes1 * 2);
    CHECK(s.textures.size() == textures1 * 2);
    CHECK(s.entities.size() == entities1 * 2);
    CHECK(s.lights.size() == 4);
    CHECK(s.cameras.size() == 2);

    const auto& child2 = findEntity(s, "second/child");
    CHECK(child2.mesh >= meshes1);
    CHECK(child2.mesh < s.meshes.size());
    CHECK(s.meshes[child2.mesh].name == "child/prim0");
    REQUIRE(child2.material.baseColorTexture.valid());
    CHECK(child2.material.baseColorTexture.texture == static_cast<scene::TextureId>(textures1));
    CHECK(s.textures[child2.material.baseColorTexture.texture].width == 2);
    // The first import's references are unchanged.
    CHECK(findEntity(s, "child").material.baseColorTexture.texture == 0);
    for (const auto& e : s.entities) {
        CHECK(e.mesh < s.meshes.size());
    }
}

TEST_CASE("loadGltf imports DamagedHelmet from AVGEN_SAMPLE_ASSETS", "[assets][gltf][samples]") {
    const char* dir = std::getenv("AVGEN_SAMPLE_ASSETS");
    if (dir == nullptr) {
        SKIP("AVGEN_SAMPLE_ASSETS is not set");
    }
    const std::filesystem::path path = std::filesystem::path(dir) / "DamagedHelmet.glb";
    if (!std::filesystem::exists(path)) {
        SKIP("DamagedHelmet.glb not found in AVGEN_SAMPLE_ASSETS");
    }
    scene::Scene s;
    const auto result = assets::loadGltf(path, s);
    REQUIRE(result.has_value());
    CHECK(result->entities >= 1);
    CHECK(result->textures == 5);
    CHECK(result->meshes >= 1);
    CHECK(result->materials == 1);
    CHECK(glm::all(glm::lessThan(result->boundsMin, result->boundsMax)));
    for (const auto& t : s.textures) {
        CHECK(t.valid());
        CHECK(t.width >= 256);
    }
    for (const auto& m : s.meshes) {
        CHECK(m.valid());
    }
    const auto& material = s.entities.front().material;
    CHECK(material.baseColorTexture.valid());
    CHECK(material.metallicRoughnessTexture.valid());
    CHECK(material.normalTexture.valid());
    CHECK(material.emissiveTexture.valid());
    CHECK(material.occlusionTexture.valid());
    CHECK(s.textures[material.baseColorTexture.texture].format == scene::TextureFormat::Rgba8Srgb);
    CHECK(s.textures[material.normalTexture.texture].format == scene::TextureFormat::Rgba8Unorm);
    CHECK(material.emissiveIntensity > 0.0f);
}

TEST_CASE("loadGltf imports the other Khronos samples without errors", "[assets][gltf][samples]") {
    const char* dir = std::getenv("AVGEN_SAMPLE_ASSETS");
    if (dir == nullptr) {
        SKIP("AVGEN_SAMPLE_ASSETS is not set");
    }
    for (const char* file : {"BoxTextured.glb", "MetalRoughSpheres.glb"}) {
        const std::filesystem::path path = std::filesystem::path(dir) / file;
        if (!std::filesystem::exists(path)) {
            continue;
        }
        INFO(file);
        scene::Scene s;
        const auto result = assets::loadGltf(path, s);
        REQUIRE(result.has_value());
        CHECK(result->entities >= 1);
        CHECK(result->textures >= 1);
        CHECK(glm::all(glm::lessThan(result->boundsMin, result->boundsMax)));
        for (const auto& m : s.meshes) {
            CHECK(m.valid());
        }
        for (const auto& t : s.textures) {
            CHECK(t.valid());
        }
    }
}
