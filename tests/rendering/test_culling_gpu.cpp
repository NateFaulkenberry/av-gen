// GPU frustum/distance culling and LOD for procedural instances (ADR-029, shaders/cull.wgsl):
// the compacted per-level lists against a CPU reference that runs the same decision
// (rendering::cullLodLevel), the monotonicity of the distance/screen-size limits, determinism
// across renderers, stable (ascending) order, and the end-to-end guarantee that culling an
// entirely visible scene renders exactly the image the direct path renders.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;

// A stable mesh-cache key for a hand-built object (the renderer only needs it to change when the
// source would produce a different mesh).
std::uint64_t specHash(const scene::SourceSpec& s) {
    std::uint64_t h = 0xC0FFEE4321ull;
    const auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
    mix(static_cast<std::uint64_t>(s.kind));
    mix(static_cast<std::uint64_t>(s.size.x * 1000.0f));
    mix(static_cast<std::uint64_t>(s.size.y * 1000.0f));
    mix(static_cast<std::uint64_t>(s.subdivisions));
    return h == 0 ? 1 : h;
}

scene::InstanceRecord recordAt(glm::vec3 position, float scale, std::uint32_t id) {
    scene::InstanceRecord r{};
    r.position = glm::vec4(position, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {scale, scale, scale, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(id)};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    return r;
}

// A `columns` x `rows` grid of unit boxes on XZ centred on the origin.
scene::ProceduralGeometry boxGrid(int columns, int rows, float spacing) {
    scene::ProceduralGeometry g;
    g.name = "grid";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {1.0f, 1.0f, 1.0f};
    g.source.subdivisions = 1;
    g.meshHash = specHash(g.source);
    g.structureVersion = 1;
    g.material.baseColor = {0.8f, 0.7f, 0.6f};
    g.material.roughness = 0.6f;
    g.instances.reserve(static_cast<std::size_t>(columns) * rows);
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < columns; ++x) {
            const float px = (static_cast<float>(x) - static_cast<float>(columns - 1) * 0.5f) * spacing;
            const float pz = (static_cast<float>(z) - static_cast<float>(rows - 1) * 0.5f) * spacing;
            g.instances.push_back(recordAt({px, 0.0f, pz}, 1.0f,
                                           static_cast<std::uint32_t>(g.instances.size())));
        }
    }
    return g;
}

scene::Scene sceneWith(scene::ProceduralGeometry object) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 6.0f, 24.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    s.procedurals.push_back(std::move(object));
    return s;
}

// The CPU reference: the level (or -1) the cull pass must assign to each instance, using exactly
// the camera terms the renderer derives from the scene and the viewport.
std::vector<int> referenceLevels(const scene::Scene& s, std::uint32_t width = kWidth, std::uint32_t height = kHeight) {
    const auto& object = s.procedurals[0];
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 viewProj = s.camera.projection(aspect) * s.camera.view();
    const rendering::FrustumPlanes planes = rendering::frustumPlanes(viewProj);
    rendering::CullCamera camera;
    camera.position = s.camera.position;
    camera.projScale = rendering::cullProjScale(s.camera.fovYRadians, height);
    const float sourceRadius = scene::sourceBoundingRadius(object.source);
    std::vector<int> levels;
    levels.reserve(object.instances.size());
    for (const scene::InstanceRecord& r : object.instances) {
        const glm::vec3 scale = glm::abs(glm::vec3(r.scale));
        const float radius = sourceRadius * std::max({scale.x, scale.y, scale.z});
        levels.push_back(rendering::cullLodLevel(object.lod, planes, camera, glm::vec3(r.position), radius));
    }
    return levels;
}

// The reference's compacted list for one level (ascending, exactly what the shader must produce).
std::vector<std::uint32_t> referenceList(const std::vector<int>& levels, int level) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i] == level) {
            out.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return out;
}

gpu::Image8 renderWith(rendering::SceneRenderer& renderer, const scene::Scene& s, double time = 0.0,
                       std::uint32_t width = kWidth, std::uint32_t height = kHeight) {
    renderer.procedurals().setViewport(width, height);
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, width, height);
    REQUIRE(img.has_value());
    return *img;
}

int litPixels(const gpu::Image8& img) {
    int count = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE("Frustum culling of a 10k grid matches the CPU reference exactly", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // A 200 x 200 unit field of boxes seen from just above its centre: the camera sees a wedge of
    // it, so a known and non-trivial subset survives.
    scene::Scene s = sceneWith(boxGrid(100, 100, 2.0f));
    s.camera.position = {0.0f, 3.0f, 30.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.procedurals[0].lod.cull = true;
    REQUIRE(s.procedurals[0].instances.size() == 10000);

    (void)renderWith(renderer, s);
    CHECK(ctx->errorCount() == 0);

    const std::vector<int> levels = referenceLevels(s);
    const std::vector<std::uint32_t> expected = referenceList(levels, 0);
    REQUIRE(expected.size() > 100);              // the camera really does see a chunk of the grid
    REQUIRE(expected.size() < levels.size() - 100); // and really does reject a chunk of it

    auto counts = renderer.procedurals().readCullCounts("grid");
    REQUIRE(counts.has_value());
    CHECK(counts->records == 10000);
    CHECK(counts->visible == expected.size());
    CHECK(counts->lod[0] == expected.size());
    CHECK(counts->culled == 10000 - expected.size());

    auto visible = renderer.procedurals().readVisibleIndices("grid", 0);
    REQUIRE(visible.has_value());
    CHECK(*visible == expected);
    // Stable compaction: the surviving indices are strictly ascending.
    CHECK(std::is_sorted(visible->begin(), visible->end()));
    CHECK(std::adjacent_find(visible->begin(), visible->end()) == visible->end());

    // The renderer's aggregate stats follow (they lag by a frame or two, so render again).
    for (int i = 0; i < 4; ++i) {
        (void)renderWith(renderer, s);
    }
    const auto& st = renderer.stats().procedural;
    CHECK(st.cullObjects == 1);
    CHECK(st.visibleInstances == expected.size());
    CHECK(st.culledInstances == 10000 - expected.size());
    CHECK(st.lodCounts[0] == expected.size());
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Instances behind the camera are culled", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Half the boxes in front of the camera, half behind it, nothing else in the way.
    scene::ProceduralGeometry g = boxGrid(1, 1, 1.0f);
    g.instances.clear();
    for (int i = 0; i < 64; ++i) {
        const float z = static_cast<float>(i - 32) * 2.0f - 1.0f; // -65 .. +61, never exactly 0
        g.instances.push_back(recordAt({0.0f, 0.0f, z}, 1.0f, static_cast<std::uint32_t>(i)));
    }
    g.lod.cull = true;
    scene::Scene s = sceneWith(std::move(g));
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f}; // looking down -Z: negative z is in front

    (void)renderWith(renderer, s);
    auto visible = renderer.procedurals().readVisibleIndices("grid", 0);
    REQUIRE(visible.has_value());
    CHECK(!visible->empty());
    for (const std::uint32_t index : *visible) {
        CHECK(s.procedurals[0].instances[index].position.z < 0.0f);
    }
    CHECK(*visible == referenceList(referenceLevels(s), 0));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("maxDistance and minScreenRadius reduce the visible count monotonically", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // 400 boxes over 28.5 units seen from 40 units straight up: the whole grid is inside the
    // frustum (half-extent 40 tan(25 deg) = 18.6 > 14.25), so only the limits can remove anything.
    scene::Scene s = sceneWith(boxGrid(20, 20, 1.5f));
    s.camera.position = {0.0f, 40.0f, 0.001f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.procedurals[0].lod.cull = true;

    const auto visibleCount = [&](const scene::Scene& scene) {
        (void)renderWith(renderer, scene);
        auto counts = renderer.procedurals().readCullCounts("grid");
        REQUIRE(counts.has_value());
        CHECK(counts->visible == referenceList(referenceLevels(scene), 0).size());
        return counts->visible;
    };

    const std::uint32_t all = visibleCount(s);
    CHECK(all == 400); // nothing culled yet

    // Instance distances run from 40 (the centre) to 44.8 (the corners).
    std::uint32_t previous = all;
    for (const float limit : {60.0f, 44.5f, 43.0f, 42.0f, 41.0f}) {
        scene::Scene limited = s;
        limited.procedurals[0].lod.maxDistance = limit;
        const std::uint32_t count = visibleCount(limited);
        INFO("maxDistance " << limit);
        CHECK(count <= previous);
        previous = count;
    }
    CHECK(previous < all);

    // Projected radii run from 4.18 pixels (the centre) down to 3.73 (the corners).
    previous = all;
    for (const float pixels : {1.0f, 3.5f, 3.8f, 4.0f, 4.15f}) {
        scene::Scene limited = s;
        limited.procedurals[0].lod.minScreenRadius = pixels;
        const std::uint32_t count = visibleCount(limited);
        INFO("minScreenRadius " << pixels);
        CHECK(count <= previous);
        previous = count;
    }
    CHECK(previous < all);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("LOD assignment matches the CPU reference by distance and by screen size", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // A line of boxes marching away from the camera: 200 known distances across four levels.
    scene::ProceduralGeometry g = boxGrid(1, 1, 1.0f);
    g.instances.clear();
    for (int i = 0; i < 200; ++i) {
        g.instances.push_back(recordAt({0.0f, 0.0f, -static_cast<float>(i)}, 1.0f, static_cast<std::uint32_t>(i)));
    }
    g.lod.lodCount = 4;
    g.lod.lodByScreenSize = false;
    g.lod.lodDistances[0] = 20.0f;
    g.lod.lodDistances[1] = 60.0f;
    g.lod.lodDistances[2] = 120.0f;
    scene::Scene s = sceneWith(std::move(g));
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};

    const auto checkLevels = [&](const scene::Scene& scene) {
        (void)renderWith(renderer, scene);
        const std::vector<int> levels = referenceLevels(scene);
        auto counts = renderer.procedurals().readCullCounts("grid");
        REQUIRE(counts.has_value());
        for (int level = 0; level < 4; ++level) {
            const std::vector<std::uint32_t> expected = referenceList(levels, level);
            INFO("level " << level);
            CHECK(counts->lod[static_cast<std::size_t>(level)] == expected.size());
            auto list = renderer.procedurals().readVisibleIndices("grid", level);
            REQUIRE(list.has_value());
            CHECK(*list == expected);
            CHECK(std::is_sorted(list->begin(), list->end()));
        }
    };

    // By distance: every level must actually be populated, or the test proves nothing.
    checkLevels(s);
    {
        const std::vector<int> levels = referenceLevels(s);
        for (int level = 0; level < 4; ++level) {
            INFO("level " << level);
            CHECK(!referenceList(levels, level).empty());
        }
    }

    // By projected radius, with the same instances and descending pixel thresholds.
    scene::Scene byScreen = s;
    byScreen.procedurals[0].lod.lodByScreenSize = true;
    byScreen.procedurals[0].lod.lodDistances[0] = 8.0f;
    byScreen.procedurals[0].lod.lodDistances[1] = 4.0f;
    byScreen.procedurals[0].lod.lodDistances[2] = 2.0f;
    checkLevels(byScreen);
    {
        const std::vector<int> levels = referenceLevels(byScreen);
        for (int level = 0; level < 4; ++level) {
            INFO("screen level " << level);
            CHECK(!referenceList(levels, level).empty());
        }
    }

    // Culling on top of LOD keeps the same ladder and only removes instances.
    scene::Scene culled = s;
    culled.procedurals[0].lod.cull = true;
    culled.procedurals[0].lod.maxDistance = 100.0f;
    checkLevels(culled);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Culling a fully visible scene renders exactly the uncalled image", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Small grid, camera well back: everything is inside the frustum, so culling must keep every
    // instance, in order, and the rasterised image must be bit-identical.
    scene::Scene plain = sceneWith(boxGrid(8, 8, 2.0f));
    plain.camera.position = {0.0f, 14.0f, 34.0f};
    plain.camera.target = {0.0f, 0.0f, 0.0f};
    const gpu::Image8 direct = renderWith(renderer, plain);
    const std::uint64_t directHash = gpu::hashImage(direct);
    CHECK(litPixels(direct) > 500);

    scene::Scene culled = plain;
    culled.procedurals[0].lod.cull = true;
    const gpu::Image8 indirect = renderWith(renderer, culled);
    auto counts = renderer.procedurals().readCullCounts("grid");
    REQUIRE(counts.has_value());
    REQUIRE(counts->culled == 0); // the premise: nothing is outside the frustum
    CHECK(counts->visible == 64);
    CHECK(gpu::hashImage(indirect) == directHash);

    // A LOD ladder that never leaves level 0 (all thresholds 0) is also the identity.
    scene::Scene lod = culled;
    lod.procedurals[0].lod.lodCount = 4;
    CHECK(gpu::hashImage(renderWith(renderer, lod)) == directHash);

    // Culling something out has to change the image (the check above is not vacuous).
    scene::Scene partial = culled;
    partial.procedurals[0].lod.maxDistance = 30.0f;
    const gpu::Image8 fewer = renderWith(renderer, partial);
    CHECK(gpu::hashImage(fewer) != directHash);
    CHECK(litPixels(fewer) < litPixels(direct));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Culling is deterministic across fresh renderers", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    scene::Scene s = sceneWith(boxGrid(30, 30, 2.0f));
    s.camera.position = {6.0f, 5.0f, 18.0f};
    s.camera.target = {-4.0f, 0.0f, -6.0f};
    s.procedurals[0].lod.cull = true;
    s.procedurals[0].lod.lodCount = 3;
    s.procedurals[0].lod.lodByScreenSize = true;
    s.procedurals[0].lod.lodDistances[0] = 6.0f;
    s.procedurals[0].lod.lodDistances[1] = 3.0f;

    const auto run = [&](std::uint64_t& hash, rendering::CullCounts& counts, std::vector<std::uint32_t>& list) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        hash = gpu::hashImage(renderWith(renderer, s, 1.5));
        auto c = renderer.procedurals().readCullCounts("grid");
        REQUIRE(c.has_value());
        counts = *c;
        auto l = renderer.procedurals().readVisibleIndices("grid", 0);
        REQUIRE(l.has_value());
        list = *l;
    };

    std::uint64_t hashA = 0, hashB = 0;
    rendering::CullCounts countsA{}, countsB{};
    std::vector<std::uint32_t> listA, listB;
    run(hashA, countsA, listA);
    run(hashB, countsB, listB);
    CHECK(hashA == hashB);
    CHECK(countsA.visible == countsB.visible);
    CHECK(countsA.lod == countsB.lod);
    CHECK(listA == listB);
    CHECK(!listA.empty());
    CHECK(std::is_sorted(listA.begin(), listA.end()));
    CHECK(countsA.culled > 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Culling throughput", "[.perf][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    struct Sample {
        double gpuMs = 0.0;
        double cullMs = 0.0;
        std::uint64_t visible = 0;
        std::uint64_t culled = 0;
    };
    const auto measure = [&](const scene::Scene& s) {
        FixedStepClock clock(60.0);
        Sample sample;
        int counted = 0;
        int cullCounted = 0;
        for (int i = 0; i < 90; ++i) {
            renderer.procedurals().setViewport(1920, 1080);
            auto img = renderer.renderToImage(s, clock.tick(), 1920, 1080);
            REQUIRE(img.has_value());
            if (i >= 30) {
                if (renderer.stats().gpuFrameMs >= 0.0) {
                    sample.gpuMs += renderer.stats().gpuFrameMs;
                    ++counted;
                }
                if (renderer.stats().procedural.cullMs >= 0.0) {
                    sample.cullMs += renderer.stats().procedural.cullMs;
                    ++cullCounted;
                }
                sample.visible = renderer.stats().procedural.visibleInstances;
                sample.culled = renderer.stats().procedural.culledInstances;
            }
        }
        sample.gpuMs = counted > 0 ? sample.gpuMs / counted : -1.0;
        sample.cullMs = cullCounted > 0 ? sample.cullMs / cullCounted : -1.0;
        return sample;
    };

    // 100k boxes; the camera sits inside the field so roughly half of it is off screen.
    scene::Scene boxes = sceneWith(boxGrid(320, 313, 1.2f));
    boxes.camera.position = {0.0f, 4.0f, 0.0f};
    boxes.camera.target = {0.0f, 1.0f, -60.0f}; // looking along -Z: the half behind is off screen
    const Sample off = measure(boxes);
    WARN("100k boxes, culling off: GPU " << off.gpuMs << " ms/frame");
    boxes.procedurals[0].lod.cull = true;
    const Sample on = measure(boxes);
    WARN("100k boxes, culling on: GPU " << on.gpuMs << " ms/frame, cull pass " << on.cullMs << " ms, visible "
                                        << on.visible << ", culled " << on.culled);
    CHECK(on.culled > 0);

    // 1M point instances with culling on.
    scene::ProceduralGeometry points = boxGrid(1000, 1000, 0.04f);
    points.source.kind = scene::PrimitiveKind::Point;
    points.source.pointSize = 0.02f;
    points.meshHash = specHash(points.source) ^ 0x9E37ull;
    points.material.unlit = true;
    points.lod.cull = true;
    scene::Scene cloud = sceneWith(std::move(points));
    cloud.camera.position = {0.0f, 6.0f, 22.0f};
    cloud.camera.target = {0.0f, 0.0f, 0.0f};
    const Sample millionOn = measure(cloud);
    WARN("1M points, culling on: GPU " << millionOn.gpuMs << " ms/frame, cull pass " << millionOn.cullMs
                                       << " ms, visible " << millionOn.visible << ", culled " << millionOn.culled);
    cloud.procedurals[0].lod.cull = false;
    const Sample millionOff = measure(cloud);
    WARN("1M points, culling off: GPU " << millionOff.gpuMs << " ms/frame");
    CHECK(ctx->errorCount() == 0);
}

// Depth layers (ADR-038) were parsed, validated, hashed and serialised, and read by nothing. Their
// instance-side half is `density`, which thins a distance band, and `detail`, which moves the LOD
// ladder; the per-pixel half lives in the composite pass. A scene with no layers must classify
// exactly as it did before they existed, or every golden frame in the suite moves.
TEST_CASE("Depth layers thin a distance band and move the LOD ladder", "[gpu][culling][composition]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = sceneWith(boxGrid(20, 20, 1.5f));
    s.camera.position = {0.0f, 40.0f, 0.001f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.procedurals[0].lod.cull = true;

    const auto visibleCount = [&](const scene::Scene& scene) {
        (void)renderWith(renderer, scene);
        auto counts = renderer.procedurals().readCullCounts("grid");
        REQUIRE(counts.has_value());
        return counts->visible;
    };

    const std::uint32_t all = visibleCount(s);
    CHECK(all == 400);

    // A band covering the whole grid at half density keeps roughly half of it, and the same scene
    // rendered twice keeps exactly the same instances: the thinning is keyed on the instance, not
    // on the frame, so a thinned band does not flicker under motion.
    scene::Scene thinned = s;
    thinned.composition.layers.push_back(scene::DepthLayer{.name = "band", .start = 0.0f, .end = 200.0f, .density = 0.5f});
    const std::uint32_t half = visibleCount(thinned);
    CHECK(half > 150);
    CHECK(half < 250);
    CHECK(visibleCount(thinned) == half);

    // Density is monotonic, and 1.0 is exactly the untouched count.
    std::uint32_t previous = 0;
    for (const float density : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        scene::Scene graded = s;
        graded.composition.layers.push_back(
            scene::DepthLayer{.name = "band", .start = 0.0f, .end = 200.0f, .density = density});
        const std::uint32_t count = visibleCount(graded);
        INFO("density " << density);
        CHECK(count >= previous);
        previous = count;
    }
    CHECK(previous == all);

    // Bands clamp at the last one, which is a real authoring hazard worth pinning: a scene that
    // declares only a near band applies that band's density to everything beyond it too.
    scene::Scene nearOnly = s;
    nearOnly.composition.layers.push_back(
        scene::DepthLayer{.name = "near", .start = 0.0f, .end = 10.0f, .density = 0.0f});
    CHECK(visibleCount(nearOnly) == 0); // the grid is at 40 units and past the last band

    // Naming the far band restores it.
    scene::Scene banded = nearOnly;
    banded.composition.layers.push_back(
        scene::DepthLayer{.name = "far", .start = 10.0f, .end = 200.0f, .density = 1.0f});
    CHECK(visibleCount(banded) == all);

    CHECK(ctx->errorCount() == 0);
}
