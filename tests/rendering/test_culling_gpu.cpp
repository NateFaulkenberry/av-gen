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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

// The two cases below compare GPU classification against a hard-threshold CPU reference, so they
// pin the per-instance spread off (ADR-082). The spread deliberately gives every instance its own
// slightly offset threshold -- that is the whole point of it -- while what these cases test is the
// threshold semantics underneath, which are unchanged. Spread's own behaviour has its own case.
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
    s.procedurals[0].lod.lodSpread = 0.0f;

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
    g.lod.lodSpread = 0.0f;
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

TEST_CASE("The indirect args the draw reads match the counts the cull pass wrote", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Every level populated, and several objects, because the failure this guards against needed
    // both: the args a draw reads were written into a per-object buffer sized exactly to hold them,
    // and the backend then silently handed the draw zeroes for the levels near its end. The cull
    // pass's own stats were right the whole time, so counts alone cannot see it -- only the bytes
    // the draw itself reads can. Two thirds of a world's ground cover went missing this way and
    // every counter in the frame said it was there.
    scene::Scene s;
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    // Deliberately unalike: a different instance count and a different ladder per object, so that
    // every object's per-level counts are distinct. Four objects that scattered the same way would
    // pass whether or not each draw reads its own slot.
    for (int object = 0; object < 4; ++object) {
        scene::ProceduralGeometry g = boxGrid(1, 1, 1.0f);
        g.name = "grid" + std::to_string(object);
        g.instances.clear();
        const int count = 60 + object * 37;
        for (int i = 0; i < count; ++i) {
            g.instances.push_back(recordAt({static_cast<float>(object) * 3.0f, 0.0f, -static_cast<float>(i)}, 1.0f,
                                           static_cast<std::uint32_t>(i)));
        }
        g.lod.cull = true;
        g.lod.lodCount = 4;
        g.lod.lodByScreenSize = false;
        g.lod.lodDistances[0] = 8.0f + static_cast<float>(object) * 3.0f;
        g.lod.lodDistances[1] = 30.0f + static_cast<float>(object) * 7.0f;
        g.lod.lodDistances[2] = 70.0f + static_cast<float>(object) * 11.0f;
        s.procedurals.push_back(std::move(g));
    }
    (void)renderWith(renderer, s);

    std::vector<std::array<std::uint32_t, 4>> signatures;
    for (int object = 0; object < 4; ++object) {
        const std::string name = "grid" + std::to_string(object);
        auto counts = renderer.procedurals().readCullCounts(name);
        REQUIRE(counts.has_value());
        std::uint32_t total = 0;
        signatures.push_back(counts->lod);
        for (int level = 0; level < 4; ++level) {
            INFO(name << " level " << level);
            auto args = renderer.procedurals().readIndirectArgs(name, level);
            REQUIRE(args.has_value());
            const std::uint32_t expected = counts->lod[static_cast<std::size_t>(level)];
            CHECK((*args)[1] == expected);   // instanceCount
            CHECK((*args)[0] > 0u);          // indexCount: the level's mesh, never zero
            total += expected;
        }
        CHECK(total > 0u);
    }
    // The premise of the check above: no two objects share a per-level signature, so reading a
    // neighbour's slot cannot pass for reading your own.
    for (std::size_t i = 0; i < signatures.size(); ++i) {
        for (std::size_t j = i + 1; j < signatures.size(); ++j) {
            INFO("objects " << i << " and " << j << " must differ");
            CHECK(signatures[i] != signatures[j]);
        }
    }
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

// ---- whole-object rejection on the CPU (P2: never encode work that is certain to be empty) -----
//
// `objectFullyCulled` lets the renderer skip an object's cull dispatches and every one of its
// indirect draws, in every pass, before the frame is encoded. It is only allowed to say "yes" when
// shaders/cull.wgsl would reject every single record, so the property under test is soundness:
// whenever it fires, the per-instance reference (`cullLodLevel`, which the GPU is separately shown
// to match) must have culled all of them.

namespace {

rendering::CullCamera cullCameraOf(const scene::Scene& s, std::uint32_t height = kHeight) {
    rendering::CullCamera camera;
    camera.position = s.camera.position;
    camera.projScale = rendering::cullProjScale(s.camera.fovYRadians, height);
    return camera;
}

rendering::FrustumPlanes planesOf(const scene::Scene& s, std::uint32_t width = kWidth,
                                  std::uint32_t height = kHeight) {
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    return rendering::frustumPlanes(s.camera.projection(aspect) * s.camera.view());
}

// How many of the object's instances the per-instance reference keeps.
int survivorCount(const scene::Scene& s) {
    const std::vector<int> levels = referenceLevels(s);
    return static_cast<int>(std::count_if(levels.begin(), levels.end(), [](int l) { return l >= 0; }));
}

bool fullyCulledFor(const scene::Scene& s) {
    const auto& object = s.procedurals[0];
    return rendering::objectFullyCulled(object.lod, planesOf(s), cullCameraOf(s), glm::mat4(1.0f),
                                        rendering::instanceBounds(object.instances),
                                        scene::sourceBoundingRadius(object.source));
}

} // namespace

TEST_CASE("Whole-object bounds summarise the record set", "[culling]") {
    const scene::ProceduralGeometry grid = boxGrid(4, 4, 2.0f);
    const rendering::InstanceBounds bounds = rendering::instanceBounds(grid.instances);
    REQUIRE(bounds.valid);
    CHECK(bounds.min.x == Catch::Approx(-3.0f));
    CHECK(bounds.max.x == Catch::Approx(3.0f));
    CHECK(bounds.min.y == Catch::Approx(0.0f));
    CHECK(bounds.max.z == Catch::Approx(3.0f));
    CHECK(bounds.maxAbsScale == Catch::Approx(1.0f));

    // The largest |scale| of any record, whatever sign or axis it is on.
    scene::ProceduralGeometry mixed = grid;
    mixed.instances[5].scale = {-4.0f, 1.0f, 1.0f, 0.0f};
    CHECK(rendering::instanceBounds(mixed.instances).maxAbsScale == Catch::Approx(4.0f));

    CHECK_FALSE(rendering::instanceBounds({}).valid);
}

TEST_CASE("An object is rejected whole only when every instance would be culled", "[culling]") {
    scene::Scene s = sceneWith(boxGrid(8, 8, 2.0f));
    s.procedurals[0].lod.cull = true;

    SECTION("in view, nothing is rejected") {
        REQUIRE(survivorCount(s) > 0);
        CHECK_FALSE(fullyCulledFor(s));
    }

    SECTION("behind the camera") {
        s.camera.position = {0.0f, 6.0f, 40.0f};
        s.camera.target = {0.0f, 6.0f, 80.0f};
        REQUIRE(survivorCount(s) == 0);
        CHECK(fullyCulledFor(s));
    }

    SECTION("past maxDistance") {
        s.camera.position = {0.0f, 6.0f, 400.0f};
        s.camera.target = {0.0f, 0.0f, 0.0f};
        s.procedurals[0].lod.maxDistance = 50.0f;
        REQUIRE(survivorCount(s) == 0);
        CHECK(fullyCulledFor(s));
        // Well within reach of the whole box, and it must not fire.
        s.camera.position = {0.0f, 6.0f, 60.0f};
        s.procedurals[0].lod.maxDistance = 500.0f;
        REQUIRE(survivorCount(s) > 0);
        CHECK_FALSE(fullyCulledFor(s));
    }

    SECTION("below minScreenRadius") {
        s.camera.position = {0.0f, 6.0f, 900.0f};
        s.camera.target = {0.0f, 0.0f, 0.0f};
        s.procedurals[0].lod.minScreenRadius = 20.0f;
        REQUIRE(survivorCount(s) == 0);
        CHECK(fullyCulledFor(s));
    }

    SECTION("culling off: the shader rejects nothing, so neither may this") {
        s.camera.position = {0.0f, 6.0f, 40.0f};
        s.camera.target = {0.0f, 6.0f, 80.0f};
        s.procedurals[0].lod.cull = false;
        CHECK_FALSE(fullyCulledFor(s));
    }
}

TEST_CASE("Whole-object rejection never drops a surviving instance", "[culling]") {
    // A sweep of cameras and limits around a compact object. The invariant is one-directional:
    // firing implies no survivors. Not firing when there are none is merely a missed saving, and
    // the count of those is reported so the test also shows the test is not vacuous.
    scene::Scene s = sceneWith(boxGrid(6, 6, 3.0f));
    s.procedurals[0].lod.cull = true;
    int fired = 0;
    int emptyCases = 0;
    int cases = 0;
    for (int a = 0; a < 12; ++a) {
        const float angle = static_cast<float>(a) * 0.5236f; // 30 degrees
        for (const float radius : {12.0f, 30.0f, 120.0f, 600.0f}) {
            for (const float maxDistance : {0.0f, 40.0f, 200.0f}) {
                for (const float minScreenRadius : {0.0f, 2.0f, 30.0f}) {
                    s.camera.position = {std::cos(angle) * radius, 8.0f, std::sin(angle) * radius};
                    // Half the cameras look at the object, half look directly away from it.
                    s.camera.target = (a % 2 == 0) ? glm::vec3(0.0f) : s.camera.position * 2.0f;
                    s.procedurals[0].lod.maxDistance = maxDistance;
                    s.procedurals[0].lod.minScreenRadius = minScreenRadius;
                    ++cases;
                    const int survivors = survivorCount(s);
                    if (survivors == 0) {
                        ++emptyCases;
                    }
                    if (fullyCulledFor(s)) {
                        ++fired;
                        INFO("survivors " << survivors << " at angle " << angle << " radius " << radius);
                        CHECK(survivors == 0);
                    }
                }
            }
        }
    }
    INFO("cases " << cases << " empty " << emptyCases << " fired " << fired);
    CHECK(fired > 0);
    CHECK(emptyCases > 0);
}

TEST_CASE("A whole-object rejection encodes no draws and changes no pixel", "[gpu][culling]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = sceneWith(boxGrid(8, 8, 2.0f));
    s.procedurals[0].lod.cull = true;
    s.procedurals[0].lod.lodCount = 4;
    s.camera.position = {0.0f, 6.0f, 40.0f};
    s.camera.target = {0.0f, 6.0f, 80.0f}; // the grid is behind the camera

    const gpu::Image8 rejected = renderWith(renderer, s);
    const rendering::ProceduralStats stats = renderer.procedurals().stats();
    CHECK(stats.culledObjects == 1);
    CHECK(stats.indirectDraws == 0);   // over the prepass, the lit pass and every shadow cascade
    CHECK(stats.cullObjects == 0);     // and no cull dispatches either

    // The same frame with the object simply not in it: the pixels must agree exactly.
    scene::Scene without = s;
    without.procedurals[0].visible = false;
    const gpu::Image8 absent = renderWith(renderer, without);
    CHECK(gpu::hashImage(rejected) == gpu::hashImage(absent));

    // Turn the camera back onto it and the object returns the same frame -- nothing pops in.
    scene::Scene facing = s;
    facing.camera.target = {0.0f, 0.0f, 0.0f};
    const gpu::Image8 seen = renderWith(renderer, facing);
    CHECK(renderer.procedurals().stats().culledObjects == 0);
    CHECK(litPixels(seen) > 500);
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-082: stability of the LOD decision ----------------------------------------------------

namespace {

// A wall of instances at one distance, so every one of them meets its LOD threshold at the same
// camera position. That is the worst case for popping and the clearest case to measure: without
// help, all of them change level on the same frame.
scene::Scene coincidentWall(std::size_t count, float threshold) {
    scene::ProceduralGeometry g = boxGrid(1, 1, 1.0f);
    g.instances.clear();
    for (std::size_t i = 0; i < count; ++i) {
        g.instances.push_back(recordAt({0.0f, 0.0f, 0.0f}, 1.0f, static_cast<std::uint32_t>(i)));
    }
    g.lod.lodCount = 2;
    g.lod.lodByScreenSize = false;
    g.lod.lodDistances[0] = threshold;
    g.lod.lodDistances[1] = 0.0f;
    g.lod.lodDistances[2] = 0.0f;
    scene::Scene s = sceneWith(std::move(g));
    s.camera.target = {0.0f, 0.0f, 0.0f};
    return s;
}

} // namespace

TEST_CASE("Per-instance spread turns a mass LOD switch into a migration", "[gpu][culling][lod]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::size_t kCount = 400;
    constexpr float kThreshold = 50.0f;

    // Walks the camera through the threshold and returns the biggest number of instances that
    // changed level between two adjacent steps. That number *is* the pop: it is how much of the
    // world changes mesh on one frame.
    const auto worstStep = [&](float spread) {
        scene::Scene s = coincidentWall(kCount, kThreshold);
        s.procedurals[0].lod.lodSpread = spread;
        s.procedurals[0].lod.lodHysteresis = 0.0f;
        std::uint32_t worst = 0;
        std::uint32_t previous = 0;
        bool first = true;
        for (int step = 0; step <= 40; ++step) {
            // 44 m out to 56 m, which spans the widest spread band this test uses.
            const float distance = 44.0f + 0.3f * static_cast<float>(step);
            s.camera.position = {0.0f, 0.0f, distance};
            (void)renderWith(renderer, s);
            auto counts = renderer.procedurals().readCullCounts("grid");
            REQUIRE(counts.has_value());
            const std::uint32_t atLevel1 = counts->lod[1];
            if (!first) {
                worst = std::max(worst, static_cast<std::uint32_t>(
                                            std::abs(static_cast<int>(atLevel1) - static_cast<int>(previous))));
            }
            previous = atLevel1;
            first = false;
        }
        return worst;
    };

    const std::uint32_t hard = worstStep(0.0f);
    const std::uint32_t spread = worstStep(0.12f);
    INFO("worst single-frame change: hard " << hard << ", spread " << spread << " of " << kCount);

    // With one shared threshold the entire wall changes on a single frame.
    CHECK(hard == kCount);
    // With each instance on its own threshold, no single frame may move more than a fraction of
    // it. A quarter is a generous bound -- it measures in the low tens -- and it is the property
    // that matters rather than an exact figure that would pin the hash.
    CHECK(spread < kCount / 4);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Hysteresis holds a level through camera jitter that would otherwise strobe",
          "[gpu][culling][lod]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::size_t kCount = 200;
    constexpr float kThreshold = 50.0f;

    // Sits the camera exactly on the threshold and breathes by a few centimetres -- far inside the
    // dead zone, far outside floating-point noise. Returns how many times the population changed
    // level. Spread is off so that the dead zone is the only thing under test.
    const auto changes = [&](float hysteresis) {
        scene::Scene s = coincidentWall(kCount, kThreshold);
        s.procedurals[0].lod.lodSpread = 0.0f;
        s.procedurals[0].lod.lodHysteresis = hysteresis;
        // One settling frame, so what follows measures jitter and not start-up.
        s.camera.position = {0.0f, 0.0f, kThreshold + 0.2f};
        (void)renderWith(renderer, s);

        int flips = 0;
        std::uint32_t previous = 0;
        bool first = true;
        for (int step = 0; step < 12; ++step) {
            const float distance = kThreshold + (step % 2 == 0 ? 0.2f : -0.2f);
            s.camera.position = {0.0f, 0.0f, distance};
            (void)renderWith(renderer, s);
            auto counts = renderer.procedurals().readCullCounts("grid");
            REQUIRE(counts.has_value());
            if (!first && counts->lod[1] != previous) {
                ++flips;
            }
            previous = counts->lod[1];
            first = false;
        }
        return flips;
    };

    // Without a dead zone, a 40 cm breath either side of a 50 m threshold flips the whole
    // population every frame. This is the artefact, reproduced.
    const int bare = changes(0.0f);
    INFO("level changes without hysteresis: " << bare);
    CHECK(bare >= 10);

    // With one, the level is decided once and then held.
    const int held = changes(0.12f);
    INFO("level changes with hysteresis: " << held);
    CHECK(held == 0);
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-108: the material parts of one asset are one spatial instance ---------------------------

namespace {

// A lead object and one material part of it: the same records, the same ladder, the same matrix --
// only the mesh differs. The part's mesh is deliberately tiny, so that culling it on its own would
// reject every instance on screen size while the lead keeps all of them. That is the discriminator:
// under the shared decision the part draws what the lead draws; without it the part draws nothing.
std::pair<scene::ProceduralGeometry, scene::ProceduralGeometry> leadAndPart(float minScreenRadius) {
    scene::ProceduralGeometry lead = boxGrid(8, 8, 2.0f);
    lead.name = "asset";
    lead.lod.cull = true;
    lead.lod.lodCount = 2;
    lead.lod.lodByScreenSize = false;
    lead.lod.lodDistances[0] = 26.0f; // splits the grid across both levels
    lead.lod.minScreenRadius = minScreenRadius;

    scene::ProceduralGeometry part = lead;
    part.name = "asset_m1";
    part.partOf = lead.name;
    part.source.size = {0.02f, 0.02f, 0.02f};
    part.source.subdivisions = 3; // a different index count, so a swapped slot cannot pass
    part.meshHash = specHash(part.source);
    part.material.baseColor = {0.2f, 0.9f, 0.3f};
    return {std::move(lead), std::move(part)};
}

scene::Scene assetScene(scene::ProceduralGeometry lead, scene::ProceduralGeometry part) {
    scene::Scene s = sceneWith(std::move(lead));
    s.procedurals.push_back(std::move(part));
    return s;
}

} // namespace

TEST_CASE("A material part is culled by its lead, once, and drawn from that one decision",
          "[gpu][culling][lod]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    // The arm under test: the part declares itself part of the lead.
    rendering::SceneRenderer shared(*ctx, shaders);
    REQUIRE(shared.init().has_value());
    auto [lead, part] = leadAndPart(2.0f);
    const std::uint64_t placements = lead.instances.size();
    scene::Scene sharedScene = assetScene(lead, part);
    (void)renderWith(shared, sharedScene);

    auto leadCounts = shared.procedurals().readCullCounts("asset");
    auto partCounts = shared.procedurals().readCullCounts("asset_m1");
    REQUIRE(leadCounts.has_value());
    REQUIRE(partCounts.has_value());
    CHECK(leadCounts->visible > 0u);
    CHECK(leadCounts->lod[0] > 0u);
    CHECK(leadCounts->lod[1] > 0u);
    // One decision: the part's per-level counts ARE the lead's.
    CHECK(partCounts->lod == leadCounts->lod);
    CHECK(partCounts->records == leadCounts->records);

    // The part's draws read its own mesh with the lead's instance counts.
    std::uint32_t leadIndexCount = 0;
    for (int level = 0; level < 2; ++level) {
        INFO("level " << level);
        auto leadArgs = shared.procedurals().readIndirectArgs("asset", level);
        auto partArgs = shared.procedurals().readIndirectArgs("asset_m1", level);
        REQUIRE(leadArgs.has_value());
        REQUIRE(partArgs.has_value());
        CHECK((*partArgs)[1] == (*leadArgs)[1]); // instanceCount: the shared decision
        CHECK((*partArgs)[0] > 0u);              // indexCount: the part's own mesh
        if (level == 0) {
            // At full resolution the two meshes are genuinely different (the part is subdivided),
            // so a draw reading the lead's slot could not pass for reading its own. The reduced
            // levels are not checked: both boxes decimate to the same index count, which is a fact
            // about boxes rather than about the slot.
            CHECK((*partArgs)[0] != (*leadArgs)[0]);
        }
        leadIndexCount += (*leadArgs)[0];
    }
    CHECK(leadIndexCount > 0u);
    // The spatial instance is counted once however many materials the asset carries.
    CHECK(shared.procedurals().stats().instances == placements);
    // And the classification ran once: two objects, one cull.
    CHECK(shared.procedurals().stats().cullObjects == 1u);
    CHECK(ctx->errorCount() == 0);

    // The negative control. The same scene with nothing shared -- the ONLY difference is the
    // declaration -- and the part now culls itself: its own bounding sphere is a fifth of a pixel
    // across, so minScreenRadius rejects every instance the lead keeps, and both counters double.
    rendering::SceneRenderer apart(*ctx, shaders);
    REQUIRE(apart.init().has_value());
    auto [lead2, part2] = leadAndPart(2.0f);
    part2.partOf.clear();
    scene::Scene apartScene = assetScene(lead2, part2);
    (void)renderWith(apart, apartScene);

    auto leadAlone = apart.procedurals().readCullCounts("asset");
    auto partAlone = apart.procedurals().readCullCounts("asset_m1");
    REQUIRE(leadAlone.has_value());
    REQUIRE(partAlone.has_value());
    CHECK(leadAlone->lod == leadCounts->lod); // the lead is unaffected either way
    CHECK(partAlone->visible == 0u);          // ... and the part has vanished
    CHECK(apart.procedurals().stats().instances == placements * 2);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A part whose placement is not its lead's culls itself", "[gpu][culling][lod]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // The claim that one cull serves both rests entirely on the two being the same placement. Drop
    // one record from the part and the renderer must refuse the share rather than index a list
    // built for a different record set.
    auto [lead, part] = leadAndPart(0.0f);
    part.instances.pop_back();
    scene::Scene s = assetScene(lead, part);
    (void)renderWith(renderer, s);

    auto leadCounts = renderer.procedurals().readCullCounts("asset");
    auto partCounts = renderer.procedurals().readCullCounts("asset_m1");
    REQUIRE(leadCounts.has_value());
    REQUIRE(partCounts.has_value());
    CHECK(leadCounts->records == 64u);
    CHECK(partCounts->records == 63u); // its own cull, over its own records
    CHECK(renderer.procedurals().stats().cullObjects == 2u);
    CHECK(ctx->errorCount() == 0);
}
