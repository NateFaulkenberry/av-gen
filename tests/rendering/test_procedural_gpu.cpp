// Procedural geometry on the GPU (ADR-023): instanced draws from InstanceRecords, the deformer
// stack in the vertex shader, determinism, the shared PBR fragment, and distance fog.
// Instances and mesh hashes are filled by local helpers so these tests do not depend on the
// CPU-side distribution/rebuild code; the source mesh itself comes from scene::makeSourceMesh.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

// Deterministic per-instance random in [0, 1) (test-local; any stable hash will do here).
float testRandom(std::uint32_t index, std::uint32_t channel) {
    std::uint32_t h = index * 0x9E3779B1u ^ (channel + 1u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// The first `count` cells of a grid of `columns` per row on XZ, centred on the origin; every
// instance upright, unit scale, white multipliers.
std::vector<scene::InstanceRecord> gridInstances(int count, int columns, float spacing) {
    std::vector<scene::InstanceRecord> out;
    out.reserve(static_cast<std::size_t>(count));
    const int rows = std::max(1, (count + columns - 1) / columns);
    for (int i = 0; i < count; ++i) {
        const int col = i % columns;
        const int row = i / columns;
        const float x = (static_cast<float>(col) - static_cast<float>(columns - 1) * 0.5f) * spacing;
        const float z = (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing;
        scene::InstanceRecord r{};
        r.position = {x, 0.0f, z, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f};
        const auto id = static_cast<std::uint32_t>(i);
        r.random = {testRandom(id, 0), testRandom(id, 1), testRandom(id, 2), testRandom(id, 3)};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        out.push_back(r);
    }
    return out;
}

std::uint64_t specHash(const scene::SourceSpec& s) {
    std::uint64_t h = 0xC0FFEE1234ull;
    auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
    mix(static_cast<std::uint64_t>(s.kind));
    mix(static_cast<std::uint64_t>(s.radius * 1000.0f));
    mix(static_cast<std::uint64_t>(s.height * 1000.0f));
    mix(static_cast<std::uint64_t>(s.radialSegments));
    mix(static_cast<std::uint64_t>(s.heightSegments));
    mix(static_cast<std::uint64_t>(s.size.x * 1000.0f));
    return h == 0 ? 1 : h;
}

// A field of thin cylinders ("columns") with `count` instances.
scene::ProceduralGeometry columns(int count, int perRow = 16, float spacing = 0.8f) {
    scene::ProceduralGeometry g;
    g.name = "columns";
    g.source.kind = scene::PrimitiveKind::Cylinder;
    g.source.radius = 0.25f;
    g.source.height = 2.0f;
    g.source.radialSegments = 12;
    g.source.heightSegments = 4;
    g.instances = gridInstances(count, perRow, spacing);
    g.structureVersion = 1;
    g.meshHash = specHash(g.source);
    g.material.baseColor = {0.85f, 0.7f, 0.55f};
    g.material.emissiveIntensity = 0.0f;
    g.material.roughness = 0.6f;
    return g;
}

scene::Scene columnScene(int count) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 7.0f, 14.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    s.procedurals.push_back(columns(count));
    return s;
}

struct Coverage {
    int pixels = 0;
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
};

// Pixels brighter than the (black) background and their bounding box.
Coverage coverage(const gpu::Image8& img) {
    Coverage c;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15) {
                ++c.pixels;
                c.minX = std::min(c.minX, static_cast<int>(x));
                c.maxX = std::max(c.maxX, static_cast<int>(x));
                c.minY = std::min(c.minY, static_cast<int>(y));
                c.maxY = std::max(c.maxY, static_cast<int>(y));
            }
        }
    }
    return c;
}

gpu::Image8 renderOnce(gpu::Context& ctx, const scene::Scene& s, double time, std::uint32_t w = 256, std::uint32_t h = 256) {
    auto shaders = makeShaders(ctx);
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return *img;
}

scene::Deformer twist(float amount, scene::DeformSpace space) {
    scene::Deformer d;
    d.kind = scene::DeformerKind::Twist;
    d.space = space;
    d.amount = amount;
    d.axis = {0.0f, 1.0f, 0.0f};
    return d;
}

} // namespace

TEST_CASE("Procedural alpha cutouts do not occlude the geometry behind them",
          "[gpu][procedural][stylized][depth]") {
    auto context = makeContext();
    auto scene = columnScene(1);
    scene.camera.position = {0.0f, 1.0f, 8.0f};
    scene.camera.target = {0.0f, 1.0f, 0.0f};
    scene.procedurals[0].instances = gridInstances(1, 1, 1.0f);
    scene.procedurals[0].material.unlit = true;
    scene.procedurals[0].material.baseColor = {0.8f, 0.15f, 0.05f};
    const auto unobstructed = renderOnce(*context, scene, 0.0, 96, 96);
    scene::TextureData transparent;
    transparent.name = "transparent";
    transparent.width = transparent.height = 1;
    transparent.data = {255, 255, 255, 0};
    auto foreground = columns(1, 1);
    foreground.name = "cutout";
    foreground.instances[0].position.z = 3.0f;
    foreground.material.alphaMode = scene::AlphaMode::Mask;
    foreground.material.baseColorTexture.texture = scene.addTexture(std::move(transparent));
    scene.procedurals.push_back(std::move(foreground));
    const auto masked = renderOnce(*context, scene, 0.0, 96, 96);
    CHECK(gpu::hashImage(unobstructed) == gpu::hashImage(masked));
    CHECK(coverage(masked).pixels > 40);
    CHECK(context->errorCount() == 0);
}

TEST_CASE("Procedural instances render, coverage grows with count, frames are stable", "[gpu][procedural]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    int previous = 0;
    for (const int count : {1, 8, 32, 128}) {
        INFO("instances " << count);
        const scene::Scene s = columnScene(count);
        auto img = renderer.renderToImage(s, time, 256, 256);
        REQUIRE(img.has_value());
        CHECK(ctx->errorCount() == 0);
        const Coverage c = coverage(*img);
        CHECK(c.pixels > previous);
        previous = c.pixels;
        const auto& st = renderer.stats();
        CHECK(st.procedural.objects == 1);
        CHECK(st.procedural.instances == static_cast<std::uint64_t>(count));
        CHECK(st.procedural.sourceMeshes == 1);
        CHECK(st.procedural.sourceTriangles > 0);
        CHECK(st.procedural.logicalTriangles == st.procedural.sourceTriangles * static_cast<std::uint64_t>(count));
        CHECK(st.procedural.instanceBufferBytes >= static_cast<std::uint64_t>(count) * sizeof(scene::InstanceRecord));
        CHECK(st.drawCalls >= 2); // procedural + tonemap
        CHECK(st.triangles >= st.procedural.logicalTriangles);

        auto again = renderer.renderToImage(s, time, 256, 256);
        REQUIRE(again.has_value());
        CHECK(gpu::hashImage(*img) == gpu::hashImage(*again));
        CHECK(renderer.stats().procedural.uploads == 0); // same structureVersion: nothing re-uploaded
        if (count == 128) {
            if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
                REQUIRE(gpu::writePpm(*img, std::filesystem::path(dumpDir) / "procedural_columns.ppm").has_value());
            }
        }
    }
    CHECK(previous > 500);

    // Invisible objects and empty instance lists draw nothing and raise no errors.
    scene::Scene hidden = columnScene(32);
    hidden.procedurals[0].visible = false;
    auto off = renderer.renderToImage(hidden, time, 64, 64);
    REQUIRE(off.has_value());
    CHECK(renderer.stats().procedural.objects == 0);
    CHECK(coverage(*off).pixels == 0);
    hidden.procedurals[0].visible = true;
    hidden.procedurals[0].instances.clear();
    auto empty = renderer.renderToImage(hidden, time, 64, 64);
    REQUIRE(empty.has_value());
    CHECK(renderer.stats().procedural.objects == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Twist deformer: amount 0 is the identity, amount 1 moves the silhouette", "[gpu][procedural]") {
    auto ctx = makeContext();
    const scene::Scene plain = columnScene(32);
    const auto base = renderOnce(*ctx, plain, 0.0);
    const std::uint64_t baseHash = gpu::hashImage(base);
    const Coverage baseCov = coverage(base);
    REQUIRE(baseCov.pixels > 0);

    scene::Scene zero = plain;
    zero.procedurals[0].deformers.push_back(twist(0.0f, scene::DeformSpace::World));
    const auto zeroImg = renderOnce(*ctx, zero, 0.0);
    CHECK(gpu::hashImage(zeroImg) == baseHash);

    scene::Scene twisted = plain;
    twisted.procedurals[0].deformers.push_back(twist(1.0f, scene::DeformSpace::World));
    const auto twistedImg = renderOnce(*ctx, twisted, 0.0);
    CHECK(gpu::hashImage(twistedImg) != baseHash);
    const Coverage tc = coverage(twistedImg);
    CHECK(tc.pixels > 0);
    const bool shifted = tc.minX != baseCov.minX || tc.maxX != baseCov.maxX || tc.minY != baseCov.minY || tc.maxY != baseCov.maxY;
    CHECK(shifted);

    // A disabled deformer is also the identity, and a local-space twist about the column axis
    // leaves a cylinder's silhouette alone but still renders (the geometry is rotationally symmetric).
    scene::Scene disabled = plain;
    disabled.procedurals[0].deformers.push_back(twist(3.0f, scene::DeformSpace::World));
    disabled.procedurals[0].deformers.back().enabled = false;
    CHECK(gpu::hashImage(renderOnce(*ctx, disabled, 0.0)) == baseHash);
    scene::Scene local = plain;
    local.procedurals[0].deformers.push_back(twist(2.0f, scene::DeformSpace::Local));
    const Coverage lc = coverage(renderOnce(*ctx, local, 0.0));
    CHECK(std::abs(lc.pixels - baseCov.pixels) < baseCov.pixels / 10);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(twistedImg, std::filesystem::path(dumpDir) / "procedural_twist.ppm").has_value());
    }
    CHECK(ctx->errorCount() == 0);
}

// The source transform is step 1 of the chain in procedural.hpp and the trailing factor of
// ProceduralGeometry::instanceMatrix(). It used to be honoured only on the CPU: the vertex shader
// ignored it, so an authored `sourceTransform` silently did nothing on screen.
TEST_CASE("Source transform places the source mesh before the deformers", "[gpu][procedural]") {
    auto ctx = makeContext();

    // One flat plate, seen from straight above, so its silhouette says which way it faces.
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 12.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.up = {0.0f, 0.0f, -1.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    s.addLight(key);

    scene::ProceduralGeometry g;
    g.name = "plate";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {4.0f, 0.2f, 1.0f};
    g.source.subdivisions = 1;
    g.distribution.kind = scene::DistributionKind::Single; // matches the single record below
    g.instances = gridInstances(1, 1, 1.0f);
    g.structureVersion = 1;
    g.meshHash = specHash(g.source);
    g.material.baseColor = {0.85f, 0.7f, 0.55f};
    g.material.emissiveIntensity = 0.0f;
    s.procedurals.push_back(g);

    const auto flat = renderOnce(*ctx, s, 0.0);
    const Coverage flatCov = coverage(flat);
    REQUIRE(flatCov.pixels > 0);

    // An identity source transform changes nothing.
    scene::Scene identity = s;
    identity.procedurals[0].sourceTransform = scene::Transform{};
    ++identity.procedurals[0].structureVersion;
    CHECK(gpu::hashImage(renderOnce(*ctx, identity, 0.0)) == gpu::hashImage(flat));

    // A quarter turn about Y swaps the plate's long and short axes on screen.
    scene::Scene turned = s;
    turned.procedurals[0].sourceTransform.rotation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ++turned.procedurals[0].structureVersion;
    const Coverage turnedCov = coverage(renderOnce(*ctx, turned, 0.0));
    REQUIRE(turnedCov.pixels > 0);
    const int flatWide = flatCov.maxX - flatCov.minX;
    const int flatTall = flatCov.maxY - flatCov.minY;
    const int turnedWide = turnedCov.maxX - turnedCov.minX;
    const int turnedTall = turnedCov.maxY - turnedCov.minY;
    CHECK(flatWide > flatTall);
    CHECK(turnedTall > turnedWide);

    // A translation moves the silhouette by the amount the CPU chain predicts.
    scene::Scene shifted = s;
    shifted.procedurals[0].sourceTransform.position = {3.0f, 0.0f, 0.0f};
    ++shifted.procedurals[0].structureVersion;
    const Coverage shiftedCov = coverage(renderOnce(*ctx, shifted, 0.0));
    REQUIRE(shiftedCov.pixels > 0);
    CHECK(shiftedCov.minX > flatCov.minX);
    CHECK(shiftedCov.maxX > flatCov.maxX);
    const glm::vec3 predicted = glm::vec3(shifted.procedurals[0].instanceMatrix(0) * glm::vec4(0, 0, 0, 1));
    CHECK(predicted.x > 2.9f);
    CHECK(predicted.x < 3.1f);

    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Procedural rendering is deterministic across renderers and animates with time", "[gpu][procedural]") {
    auto ctx = makeContext();
    scene::Scene s = columnScene(32);
    scene::Deformer sine;
    sine.kind = scene::DeformerKind::Sine;
    sine.space = scene::DeformSpace::World;
    sine.amount = 0.6f;
    sine.axis = {1.0f, 0.0f, 0.0f};
    sine.frequency = 1.5f;
    sine.speed = 2.0f;
    sine.displacementAxis = {0.0f, 1.0f, 0.0f};
    s.procedurals[0].deformers.push_back(sine);
    // Every deformer kind in one stack, so the whole shader path is exercised for errors too.
    scene::Deformer bend;
    bend.kind = scene::DeformerKind::Bend;
    bend.amount = 0.3f;
    bend.falloff = 1.0f;
    s.procedurals[0].deformers.push_back(bend);
    scene::Deformer noise;
    noise.kind = scene::DeformerKind::Noise;
    noise.amount = 0.05f;
    noise.scale = 2.0f;
    noise.speed = 0.5f;
    s.procedurals[0].deformers.push_back(noise);
    scene::Deformer disp;
    disp.kind = scene::DeformerKind::Displacement;
    disp.amount = 0.03f;
    disp.scale = 3.0f;
    s.procedurals[0].deformers.push_back(disp);

    const auto a = renderOnce(*ctx, s, 1.0);
    const auto b = renderOnce(*ctx, s, 1.0);
    CHECK(gpu::hashImage(a) == gpu::hashImage(b));
    const auto later = renderOnce(*ctx, s, 2.0);
    CHECK(gpu::hashImage(later) != gpu::hashImage(a));
    CHECK(coverage(a).pixels > 0);
    CHECK(coverage(later).pixels > 0);
    CHECK(ctx->errorCount() == 0);

    // A second context renders the same bytes.
    auto ctx2 = makeContext();
    const auto c = renderOnce(*ctx2, s, 1.0);
    CHECK(gpu::hashImage(c) == gpu::hashImage(a));
    CHECK(ctx2->errorCount() == 0);
}

TEST_CASE("4096 procedural instances render without GPU errors and re-upload only on structural change",
          "[gpu][procedural]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 12.0f, 26.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.addLight(scene::PunctualLight{});
    s.procedurals.push_back(columns(4096, 64, 0.5f));
    s.procedurals[0].deformers.push_back(twist(0.2f, scene::DeformSpace::World));
    FrameTime time{};
    auto img = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(img.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(renderer.stats().procedural.instances == 4096);
    CHECK(renderer.stats().procedural.uploads == 1);
    CHECK(renderer.stats().procedural.deformers == 1);
    CHECK(coverage(*img).pixels > 1000);

    // Same structure, new frame: no upload. Bumped structureVersion: one upload. More instances
    // than the buffer holds: the buffer grows and uploads once.
    (void)renderer.renderToImage(s, time, 64, 64);
    CHECK(renderer.stats().procedural.uploads == 0);
    s.procedurals[0].structureVersion = 2;
    (void)renderer.renderToImage(s, time, 64, 64);
    CHECK(renderer.stats().procedural.uploads == 1);
    s.procedurals[0].instances = gridInstances(5000, 64, 0.5f);
    s.procedurals[0].structureVersion = 3;
    (void)renderer.renderToImage(s, time, 64, 64);
    CHECK(renderer.stats().procedural.uploads == 1);
    CHECK(renderer.stats().procedural.instances == 5000);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Distance fog pulls a far object towards the fog colour", "[gpu][procedural][fog]") {
    auto ctx = makeContext();
    scene::Scene s;
    const glm::vec3 fog{0.6f, 0.6f, 0.6f};
    s.environment.backgroundColor = fog; // fully fogged surfaces converge on the background
    s.environment.fogColor = fog;
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    s.camera.farPlane = 500.0f;
    scene::MeshData cube;
    {
        // A box source mesh via the procedural generator keeps this test free of other helpers.
        scene::SourceSpec spec;
        spec.kind = scene::PrimitiveKind::Box;
        spec.size = {6.0f, 6.0f, 6.0f};
        auto made = scene::makeSourceMesh(spec);
        REQUIRE(made.has_value());
        cube = std::move(*made);
    }
    const auto mesh = s.addMesh(std::move(cube));
    auto& e = s.addEntity("far-cube", mesh);
    e.transform.position = {0.0f, 0.0f, -40.0f};
    e.material.baseColor = {0.05f, 0.05f, 0.05f};
    e.material.emissiveIntensity = 0.0f;
    e.material.roughness = 1.0f;
    s.addLight(scene::PunctualLight{});

    auto dist = [](const gpu::Image8& img) {
        const auto* centre = img.pixel(img.width / 2, img.height / 2);
        const auto* corner = img.pixel(1, 1);
        return std::abs(int(centre[0]) - int(corner[0])) + std::abs(int(centre[1]) - int(corner[1])) +
               std::abs(int(centre[2]) - int(corner[2]));
    };
    // ADR-705: the surface fog is the air's one density under Beer--Lambert, and `volumeMaxDistance`
    // 0 keeps the march off so this is the surface pass alone. Was exp-squared `fogDensity` 0.05,
    // exp(-(40 * 0.05)^2) = exp(-4); the same exp(-4) is now 0.2 * absorption 0.5 * 40 m.
    s.environment.volumeMaxDistance = 0.0f;
    s.environment.volumeDensity = 0.0f;
    const auto clear = renderOnce(*ctx, s, 0.0, 64, 64);
    s.environment.volumeDensity = 0.2f; // distance 40 -> exp(-4): almost fully fogged
    const auto foggy = renderOnce(*ctx, s, 0.0, 64, 64);
    const int clearDist = dist(clear);
    const int foggyDist = dist(foggy);
    INFO("clear " << clearDist << " foggy " << foggyDist);
    CHECK(clearDist > 100);      // a dark cube against a grey background
    CHECK(foggyDist < clearDist / 4);
    CHECK(gpu::hashImage(clear) != gpu::hashImage(foggy));
    // Density 0 with a different fog colour is a no-op.
    s.environment.volumeDensity = 0.0f;
    s.environment.fogColor = {1.0f, 0.0f, 0.0f};
    CHECK(gpu::hashImage(renderOnce(*ctx, s, 0.0, 64, 64)) == gpu::hashImage(clear));
    CHECK(ctx->errorCount() == 0);
}

// Hidden performance probe: `avgen_render_tests "[.perf][procedural]"` (Release). Reports GPU
// and CPU-update time per frame for 1,000 and 10,000 instances of a 24-segment cylinder at
// 1280x720 with no deformation, one deformer, three deformers, and a noise deformer.
TEST_CASE("Procedural instancing throughput", "[.perf][procedural]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    struct Config {
        const char* name;
        std::vector<scene::Deformer> stack;
    };
    scene::Deformer bend;
    bend.kind = scene::DeformerKind::Bend;
    bend.amount = 0.2f;
    scene::Deformer sine;
    sine.kind = scene::DeformerKind::Sine;
    sine.space = scene::DeformSpace::World;
    sine.amount = 0.3f;
    sine.speed = 1.0f;
    scene::Deformer noise;
    noise.kind = scene::DeformerKind::Noise;
    noise.amount = 0.1f;
    noise.scale = 1.5f;
    noise.speed = 0.5f;
    const std::vector<Config> configs = {
        {"none", {}},
        {"one (twist)", {twist(0.5f, scene::DeformSpace::Local)}},
        {"three (twist, bend, sine)", {twist(0.5f, scene::DeformSpace::Local), bend, sine}},
        {"noise", {noise}},
    };
    for (const int count : {1000, 10000}) {
        for (const auto& cfg : configs) {
            scene::Scene s;
            s.camera.position = {0.0f, 20.0f, 60.0f};
            s.camera.target = {0.0f, 0.0f, 0.0f};
            s.addLight(scene::PunctualLight{});
            s.procedurals.push_back(columns(count, 100, 0.7f));
            s.procedurals[0].source.radialSegments = 24;
            s.procedurals[0].source.heightSegments = 4;
            s.procedurals[0].meshHash = specHash(s.procedurals[0].source);
            s.procedurals[0].deformers = cfg.stack;
            FixedStepClock clock(60.0);
            double gpuSum = 0.0, cpuSum = 0.0;
            int counted = 0;
            for (int i = 0; i < 90; ++i) {
                auto img = renderer.renderToImage(s, clock.tick(), 1280, 720);
                REQUIRE(img.has_value());
                if (i >= 30 && renderer.stats().gpuFrameMs >= 0.0) {
                    gpuSum += renderer.stats().gpuFrameMs;
                    cpuSum += renderer.stats().procedural.cpuUpdateMs;
                    ++counted;
                }
            }
            CHECK(ctx->errorCount() == 0);
            WARN("procedural " << count << " instances, " << cfg.name << ": GPU " << (counted ? gpuSum / counted : -1.0)
                               << " ms/frame, CPU update " << (counted ? cpuSum / counted : -1.0) << " ms, "
                               << renderer.stats().procedural.logicalTriangles << " triangles");
        }
    }
}
