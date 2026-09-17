// The LOD / Geometry Lab, against the pixels the renderer actually produced (§37).
//
// The trap this file exists to avoid is the one the brief names: it is easy to "verify" a ladder by
// re-deriving the expected rung from the same inputs the shader uses, which agrees by construction
// and proves nothing. `tests/unit/test_triangle_size_analysis.cpp` and
// `tests/unit/test_representation_band_analysis.cpp` both transcribe `cullLodLevel` for exactly
// that reason and both say so. So nothing here compares one CPU function against another. Every
// assertion is against the rung the GPU compacted the instance into (`readCullCounts`, the stats
// the cull pass itself wrote) and the **silhouette the frame drew**, measured out of the returned
// image. Where the two disagree the image wins.
//
// The fixtures are production assets (§29). CommonTree_1 is Glowmere's `canopy` layer, normalised
// to 14 m, and it is the case the last bug hid in: geometry that stands on its own origin, where
// every "bounding radius" rule gives a different answer. Rock_Medium_1 is `boulders`, near enough
// centred on its origin that the two rules almost agree, and it is the control -- an arm that moves
// the tree and not the rock is telling the truth about which quantity changed.
#include "assets/gltf_loader.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

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

constexpr std::uint32_t kWidth = 960;
constexpr std::uint32_t kHeight = 720;

// One production asset, merged to a single mesh. Merged for the reason the Visibility Lab merges
// it: ADR-108 makes one cull decision cover every material part, so the decision is about this
// geometry whether it reaches the GPU as one mesh or three.
struct Source {
    std::shared_ptr<scene::MeshData> mesh;
    glm::vec3 lo{0.0f};
    glm::vec3 hi{0.0f};
    float sourceScale = 1.0f; // the layer's "make this thing N metres tall" normalisation
    bool valid = false;
};

Source loadAsset(const std::string& file, float targetHeight) {
    Source out;
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / file;
    if (!fs::is_regular_file(path)) {
        return out;
    }
    scene::Scene s;
    if (!assets::loadGltf(path, s, {}) || s.meshes.empty()) {
        return out;
    }
    auto merged = std::make_shared<scene::MeshData>();
    merged->name = file;
    for (const scene::MeshData& part : s.meshes) {
        const auto base = static_cast<std::uint32_t>(merged->vertices.size());
        merged->vertices.insert(merged->vertices.end(), part.vertices.begin(), part.vertices.end());
        for (const std::uint32_t index : part.indices) {
            merged->indices.push_back(base + index);
        }
    }
    if (merged->vertices.empty() || merged->indices.empty()) {
        return out;
    }
    const auto [lo, hi] = merged->bounds();
    out.mesh = merged;
    out.lo = lo;
    out.hi = hi;
    out.sourceScale = targetHeight / std::max(hi.y - lo.y, 1e-4f);
    out.valid = true;
    return out;
}

// Glowmere's scatter ladder, verbatim (src/scene/composition.cpp, the Terrain case).
scene::LodSettings scatterLod(float viewDistance, float minScreenRadius) {
    scene::LodSettings lod;
    lod.cull = true;
    lod.maxDistance = viewDistance;
    lod.minScreenRadius = minScreenRadius;
    lod.lodCount = scene::kMaxLodLevels;
    lod.lodByScreenSize = true;
    lod.lodDistances[0] = 28.0f;
    lod.lodDistances[1] = 11.0f;
    lod.lodDistances[2] = 4.0f;
    // ADR-082's two stability terms are off in every case here. They are legitimate and documented,
    // and they make the answer depend on the previous frame -- which is the one thing a probe about
    // *this* frame's decision must not have.
    lod.lodSpread = 0.0f;
    lod.lodHysteresis = 0.0f;
    return lod;
}

scene::ProceduralGeometry instanceOf(const Source& src, const std::string& name, std::uint64_t meshHash,
                                     const scene::LodSettings& lod, glm::vec3 position) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Mesh;
    g.source.asset = name;
    g.source.assetMesh = src.mesh;
    g.meshHash = meshHash;
    g.structureVersion = 1;
    g.sourceTransform.scale = glm::vec3(src.sourceScale);
    g.material.baseColor = {0.7f, 0.75f, 0.6f};
    g.material.roughness = 0.8f;
    g.lod = lod;
    scene::InstanceRecord r{};
    r.position = glm::vec4(position, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances.push_back(r);
    return g;
}

scene::Scene bareScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.55f, -0.75f));
    key.intensity = 5.0f;
    s.addLight(key);
    s.camera.fovYRadians = glm::radians(50.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 4000.0f;
    return s;
}

// The rectangle of lit pixels: what the frame drew, in pixels, with no reference to any counter.
struct Silhouette {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    int pixels = 0;
    [[nodiscard]] bool empty() const { return pixels == 0; }
    [[nodiscard]] float width() const { return static_cast<float>(maxX - minX + 1); }
    [[nodiscard]] float height() const { return static_cast<float>(maxY - minY + 1); }
    // Half the larger dimension: the radius of the circle that just contains what was drawn, which
    // is the quantity the ladder's `screenRadius` claims to be a prediction of.
    [[nodiscard]] float radius() const { return 0.5f * std::max(width(), height()); }
};

Silhouette silhouetteOf(const gpu::Image8& img) {
    Silhouette s;
    s.minX = static_cast<int>(img.width);
    s.minY = static_cast<int>(img.height);
    s.maxX = -1;
    s.maxY = -1;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] <= 18) { // the background is black and unlit
                continue;
            }
            s.minX = std::min(s.minX, static_cast<int>(x));
            s.minY = std::min(s.minY, static_cast<int>(y));
            s.maxX = std::max(s.maxX, static_cast<int>(x));
            s.maxY = std::max(s.maxY, static_cast<int>(y));
            ++s.pixels;
        }
    }
    if (s.pixels == 0) {
        s.minX = s.minY = s.maxX = s.maxY = 0;
    }
    return s;
}

// The rung the GPU put the instance on: the level whose compacted count is not zero. -1 when the
// pass rejected it, -2 when the object has no cull state. Read from the stats buffer the cull pass
// wrote, not from a second copy of the rule.
int drawnRung(rendering::ProceduralRenderer& procedurals, const std::string& name) {
    auto counts = procedurals.readCullCounts(name);
    if (!counts) {
        return -2;
    }
    for (int level = 0; level < scene::kMaxLodLevels; ++level) {
        if (counts->lod[static_cast<std::size_t>(level)] > 0) {
            return level;
        }
    }
    return -1;
}

struct Shot {
    Silhouette silhouette;
    int rung = -2;
};

// One instance of `src`, `distance` metres away, drawn once. The camera is level with the middle of
// the object so the whole of it stays in frame at every distance: the question is which mesh was
// drawn, and a frame edge cutting the subject would answer a different one.
Shot shoot(rendering::SceneRenderer& renderer, const Source& src, const std::string& name,
           std::uint64_t hash, const scene::LodSettings& lod, float distance, float eye, int frames = 1) {
    FrameTime t{};
    Shot out;
    for (int frame = 0; frame < frames; ++frame) {
        scene::Scene s = bareScene();
        s.procedurals.push_back(instanceOf(src, name, hash, lod, glm::vec3(0.0f)));
        s.camera.position = {0.0f, eye, distance};
        s.camera.target = {0.0f, eye, 0.0f};
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        out.silhouette = silhouetteOf(*img);
        out.rung = drawnRung(renderer.procedurals(), name);
    }
    return out;
}

} // namespace

// -------------------------------------------------------------------------------------------------
// §14 "LOD popping": the frame the swap happens on.
// -------------------------------------------------------------------------------------------------
TEST_CASE("A rung change draws the object on the frame it happens", "[gpu][lod]") {
    // The claim under test is the plainest one a LOD system makes: choosing a cheaper
    // representation is still choosing a representation. On the frame an instance moves to a level
    // it has not been on recently, that level must be drawn.
    //
    // The arm and its control are the same camera at the same distance, rendered twice with the
    // same renderer. Nothing about the frame differs except how long the renderer has known that
    // the level is occupied, so a difference between the two is that knowledge and nothing else.
    const Source tree = loadAsset("CommonTree_1.gltf", 14.0f);
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.procedurals().setViewport(kWidth, kHeight);
    const scene::LodSettings lod = scatterLod(4000.0f, 0.0f);

    // Warm the object up somewhere the ladder puts it on rung 0, so every far level has been empty
    // for long enough to be a candidate for not being recorded.
    for (int i = 0; i < 6; ++i) {
        (void)shoot(renderer, tree, "canopy", 0xC0FFEE01ull, lod, 60.0f, 7.0f);
    }

    // Now the swap. `arrival` is the first frame at a distance whose rung is not rung 0; `settled`
    // is the identical frame once the renderer has seen that level occupied.
    constexpr float kFar = 460.0f;
    const Shot arrival = shoot(renderer, tree, "canopy", 0xC0FFEE01ull, lod, kFar, 7.0f);
    const Shot settled = shoot(renderer, tree, "canopy", 0xC0FFEE01ull, lod, kFar, 7.0f, 4);
    CHECK(ctx->errorCount() == 0);

    std::printf("  arrival frame: rung %d, %d lit px (%.0f x %.0f)\n", arrival.rung, arrival.silhouette.pixels,
                static_cast<double>(arrival.silhouette.width()), static_cast<double>(arrival.silhouette.height()));
    std::printf("  settled frame: rung %d, %d lit px (%.0f x %.0f)\n", settled.rung, settled.silhouette.pixels,
                static_cast<double>(settled.silhouette.width()), static_cast<double>(settled.silhouette.height()));

    // Both arms reached: the distance really does put the instance somewhere other than rung 0, and
    // the settled frame really does draw it. Without these the assertion is vacuous (ADR-182).
    INFO("rung on arrival " << arrival.rung << ", once settled " << settled.rung);
    REQUIRE(arrival.rung > 0);
    REQUIRE(settled.rung == arrival.rung);
    REQUIRE(settled.silhouette.pixels > 0);

    INFO("the cull pass compacted the instance into rung " << arrival.rung
         << " and the frame drew " << arrival.silhouette.pixels << " lit pixels");
    CHECK(arrival.silhouette.pixels > 0);
    // Not merely non-empty: the same object, the same size. A single stray pixel would pass the
    // line above and would not be the tree.
    CHECK(static_cast<float>(arrival.silhouette.pixels) >
          0.5f * static_cast<float>(settled.silhouette.pixels));
}

// -------------------------------------------------------------------------------------------------
// §14 "approach object / recede from object", "geometry switching": what each rung actually draws.
// -------------------------------------------------------------------------------------------------
TEST_CASE("The representation a rung draws occupies the object's own screen region", "[gpu][lod]") {
    // The invariant, stated without reference to any threshold: a LOD level is a cheaper way to draw
    // the *same object*, so whatever rung is selected, what lands on screen must cover the region
    // the object covers. A level that is the wrong size, or in the wrong place, is not a level --
    // it is a different object, and the swap to it is a pop by construction.
    //
    // Measured against the drawn image, which is why it can fail while every counter in the frame
    // reports a healthy ladder. That is ADR-085's lesson one layer down: `lod=2/30/124/0` says how
    // many instances chose each level and nothing about what the levels look like.
    const Source tree = loadAsset("CommonTree_1.gltf", 14.0f);
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.procedurals().setViewport(kWidth, kHeight);

    // Each rung is reached at a fixed 90 m by moving the *thresholds* rather than the camera, so
    // every shot has the same perspective and the same lighting and the only difference between
    // them is which mesh the ladder selected. A dolly would confound the two.
    struct Arm {
        int wanted;
        glm::vec3 thresholds;
    };
    const std::vector<Arm> arms{
        {0, {0.0f, 0.0f, 0.0f}},            // the ladder ends at rung 0
        {1, {1.0e6f, 0.0f, 0.0f}},          // everything is small enough for rung 1, and no further
        {2, {1.0e6f, 1.0e6f, 0.0f}},        // ... rung 2, the impostor
        {3, {1.0e6f, 1.0e6f, 1.0e6f}},      // ... rung 3, the dot
    };
    constexpr float kRange = 90.0f;
    constexpr float kEye = 7.0f;
    std::vector<Shot> shots;
    for (const Arm& arm : arms) {
        scene::LodSettings lod = scatterLod(4000.0f, 0.0f);
        lod.lodDistances[0] = arm.thresholds.x;
        lod.lodDistances[1] = arm.thresholds.y;
        lod.lodDistances[2] = arm.thresholds.z;
        // Four frames: the arrival-frame defect has its own case above, and this one is about the
        // geometry rather than about when it appears.
        Shot shot = shoot(renderer, tree, "canopy", 0xC0FFEE02ull, lod, kRange, kEye, 4);
        INFO("arm asked for rung " << arm.wanted << " and the cull pass reported " << shot.rung);
        REQUIRE(shot.rung == arm.wanted);
        shots.push_back(shot);
    }
    CHECK(ctx->errorCount() == 0);

    for (std::size_t i = 0; i < shots.size(); ++i) {
        std::printf("  rung %zu: %.0f x %.0f px at rows %d..%d, %d lit px\n", i,
                    static_cast<double>(shots[i].silhouette.width()),
                    static_cast<double>(shots[i].silhouette.height()), shots[i].silhouette.minY,
                    shots[i].silhouette.maxY, shots[i].silhouette.pixels);
    }

    // Rung 0 is the object: every other rung is measured against it, so the "right" region is the
    // one the full mesh occupied in the identical frame rather than a number chosen here.
    const Silhouette& full = shots[0].silhouette;
    REQUIRE_FALSE(full.empty());
    // Rungs 1 and 2 are the ones Glowmere's population actually occupies (231 / 134 / 127 / 0 on
    // the multicam baseline), and they carry the invariant in full.
    for (std::size_t i = 1; i <= 2; ++i) {
        const Silhouette& s = shots[i].silhouette;
        INFO("rung " << i << " drew rows " << s.minY << ".." << s.maxY << " where rung 0 drew "
                     << full.minY << ".." << full.maxY);
        REQUIRE_FALSE(s.empty());
        // The foot stays on the ground, and the crown stays where the crown was. A level drawn
        // through the impostor path takes its vertices as offsets in the camera's basis from the
        // instance record -- skipping the source transform, so at the asset's authored size rather
        // than the layer's, and centred on the foot of the trunk. Before this was fixed, rung 2 of
        // this tree drew rows 362..419 where rung 0 draws 304..424: half the height, sunk to the
        // ground, at every distance past the second threshold.
        CHECK(std::abs(static_cast<float>(s.maxY - full.maxY)) < 0.15f * full.height());
        CHECK(std::abs(static_cast<float>(s.minY - full.minY)) < 0.15f * full.height());
        CHECK(s.height() > 0.8f * full.height());
        CHECK(s.height() < 1.2f * full.height());
    }
    // Rung 3 is a **recorded defect, not fixed here**, and it is the reason the loop above stops at
    // rung 2. At 2.9% of its triangles CommonTree_1 comes back from the sloppy simplifier with the
    // bottom 31% of its bounding box gone -- the trunk -- while the level reports a relative error
    // of 0.065, which is 5.3 times smaller than the deviation it actually has. That contradicts
    // `assets/mesh_lod.hpp`'s own claim that the error "never understates", and it is measured in
    // `tests/unit/test_lod_ladder.cpp` under [.analysis]. The bound below is the measured behaviour
    // and exists so that it cannot get worse unnoticed; it is not a statement that this is right.
    {
        const Silhouette& s = shots[3].silhouette;
        INFO("rung 3 drew rows " << s.minY << ".." << s.maxY << " where rung 0 drew " << full.minY
                                 << ".." << full.maxY << " -- the trunk is missing, see the comment");
        REQUIRE_FALSE(s.empty());
        // The crown is still the crown: whatever survives is in the right place and the right size.
        CHECK(std::abs(static_cast<float>(s.minY - full.minY)) < 0.15f * full.height());
        CHECK(s.height() > 0.5f * full.height());
    }
}

// -------------------------------------------------------------------------------------------------
// §14 "incorrect thresholds / different mesh bounds": the threshold question itself.
// -------------------------------------------------------------------------------------------------
TEST_CASE("A pixel threshold means the same size for every asset", "[gpu][lod]") {
    // The ladder selects on projected size, so its promise is that a threshold *is* a size: two
    // objects that look the same get drawn the same way. This is that promise, stated so it cannot
    // be satisfied by reimplementing the rule -- for each asset, find by bisection the distance at
    // which the GPU's own rung changes from 0 to 1, then measure how big the object was **on
    // screen** at that moment, out of the rendered image.
    //
    // The two assets are chosen for the property the last bug turned on. CommonTree_1 stands on its
    // own origin; Rock_Medium_1 is near enough centred on its. Anything that measures size from a
    // sphere about the *record position* -- which is what shaders/cull.wgsl does -- therefore reads
    // them differently by a factor that is a fact about where the artist put the origin and not
    // about how big they look.
    const Source tree = loadAsset("CommonTree_1.gltf", 14.0f);
    const Source rock = loadAsset("Rock_Medium_1.gltf", 14.0f); // the same height: the origin
                                                                // convention is then the only
                                                                // difference between them
    if (!tree.valid || !rock.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.procedurals().setViewport(kWidth, kHeight);
    const scene::LodSettings lod = scatterLod(6000.0f, 0.0f);

    // The distance at which the GPU stops choosing rung 0, and the silhouette it drew just inside
    // it. Bisection over rendered frames; four frames per probe so the arrival-frame defect cannot
    // be mistaken for a rung change.
    struct Crossing {
        float distance = 0.0f;
        Silhouette silhouette;
    };
    const auto crossing = [&](const Source& src, const char* name, std::uint64_t hash) {
        float near = 20.0f;
        float far = 3000.0f;
        for (int step = 0; step < 15; ++step) {
            const float mid = 0.5f * (near + far);
            const Shot shot = shoot(renderer, src, name, hash, lod, mid, 7.0f, 4);
            (shot.rung == 0 ? near : far) = mid;
        }
        Crossing out;
        out.distance = near;
        out.silhouette = shoot(renderer, src, name, hash, lod, near, 7.0f, 4).silhouette;
        return out;
    };

    const Crossing treeAt = crossing(tree, "tree", 0xA0A0A001ull);
    const Crossing rockAt = crossing(rock, "rock", 0xA0A0A002ull);
    CHECK(ctx->errorCount() == 0);

    std::printf("  tree loses its full mesh at %.1f m, drawn %.0f x %.0f px (radius %.1f px)\n",
                static_cast<double>(treeAt.distance), static_cast<double>(treeAt.silhouette.width()),
                static_cast<double>(treeAt.silhouette.height()), static_cast<double>(treeAt.silhouette.radius()));
    std::printf("  rock loses its full mesh at %.1f m, drawn %.0f x %.0f px (radius %.1f px)\n",
                static_cast<double>(rockAt.distance), static_cast<double>(rockAt.silhouette.width()),
                static_cast<double>(rockAt.silhouette.height()), static_cast<double>(rockAt.silhouette.radius()));

    REQUIRE_FALSE(treeAt.silhouette.empty());
    REQUIRE_FALSE(rockAt.silhouette.empty());
    // The threshold is 28 px of projected radius. A bounding *sphere* is a loose description of a
    // tree and of a rock by different amounts, so the drawn radius at the crossing cannot be 28 for
    // both -- but it has to be the same number for both to within that looseness, or the pixel the
    // threshold is written in is not a pixel of anything. 1.35 is the ratio of the two assets' own
    // sphere-to-silhouette looseness, measured in tests/unit/test_lod_ladder.cpp; anything past it
    // is the origin convention and not the shape.
    const float ratio = std::max(treeAt.silhouette.radius(), rockAt.silhouette.radius()) /
                        std::max(std::min(treeAt.silhouette.radius(), rockAt.silhouette.radius()), 1e-3f);
    INFO("the tree gives up its full mesh at " << treeAt.silhouette.radius()
         << " px of drawn radius and the rock at " << rockAt.silhouette.radius() << " px: ratio " << ratio);
    CHECK(ratio < 1.35f);
}

// -------------------------------------------------------------------------------------------------
// The other half of the threshold ledger: what the triangles buy.
// -------------------------------------------------------------------------------------------------
TEST_CASE("What the first threshold costs and what it buys", "[gpu][lod]") {
    // The instrument the threshold question needs. A rung boundary is a trade -- triangles against
    // the difference a viewer could see -- and the cost side has always been reported (`tris=`)
    // while the quality side has not. This measures both at a range of apparent sizes, for the two
    // representations either side of the first threshold, in frames that differ in nothing else.
    //
    // The difference measure is ADR-085's own, so the numbers are comparable with the one quality
    // statement this ladder already has: pixels differing by more than 2/255.
    const Source tree = loadAsset("CommonTree_1.gltf", 14.0f);
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.procedurals().setViewport(kWidth, kHeight);
    FrameTime t{};

    scene::LodSettings full = scatterLod(6000.0f, 0.0f);
    full.lodDistances[0] = 0.0f; // the ladder ends at rung 0
    scene::LodSettings reduced = scatterLod(6000.0f, 0.0f);
    reduced.lodDistances[0] = 1.0e6f; // everything is rung 1, and no further
    reduced.lodDistances[1] = 0.0f;

    const auto render = [&](const scene::LodSettings& lod, float distance) {
        std::optional<gpu::Image8> last;
        for (int frame = 0; frame < 3; ++frame) {
            scene::Scene s = bareScene();
            s.procedurals.push_back(instanceOf(tree, "canopy", 0xB00B1E01ull, lod, glm::vec3(0.0f)));
            s.camera.position = {0.0f, 7.0f, distance};
            s.camera.target = {0.0f, 7.0f, 0.0f};
            auto img = renderer.renderToImage(s, t, kWidth, kHeight);
            REQUIRE(img.has_value());
            last = std::move(*img);
        }
        return *last;
    };

    std::printf("  drawn radius  distance  rung0 px  rung1 px  mass  box dy  differing >2/255\n");
    std::vector<float> shares;
    // The sizes that matter, in drawn pixels of radius: where the ladder switched before the cull
    // radius was corrected (about 22 px for this asset), where it switches now (13.5 px, measured
    // above), and a size either side of both.
    for (const float wanted : {40.0f, 22.0f, 13.5f, 8.0f}) {
        // Bisect the distance on the rendered image, as everywhere else in this file.
        float nearD = 20.0f;
        float farD = 3000.0f;
        for (int step = 0; step < 14; ++step) {
            const float mid = 0.5f * (nearD + farD);
            (silhouetteOf(render(full, mid)).radius() >= wanted ? nearD : farD) = mid;
        }
        const gpu::Image8 a = render(full, nearD);
        const gpu::Image8 b = render(reduced, nearD);
        const Silhouette s = silhouetteOf(a);
        int differing = 0;
        for (std::uint32_t y = 0; y < a.height; ++y) {
            for (std::uint32_t x = 0; x < a.width; ++x) {
                const auto* pa = a.pixel(x, y);
                const auto* pb = b.pixel(x, y);
                const int d = std::max({std::abs(pa[0] - pb[0]), std::abs(pa[1] - pb[1]),
                                        std::abs(pa[2] - pb[2])});
                if (d > 2) {
                    ++differing;
                }
            }
        }
        const float share = s.pixels > 0 ? static_cast<float>(differing) / static_cast<float>(s.pixels) : 0.0f;
        shares.push_back(share);
        // The per-pixel measure saturates on foliage -- a third of the triangles is a different
        // arrangement of leaves, so nearly every pixel of the object moves at every size, and the
        // number says "the meshes differ" rather than "the swap is visible". What a viewer sees
        // change when the rung flips is the object's *mass* and its *outline*, so both are here,
        // and they are what the verdict rests on.
        const Silhouette r = silhouetteOf(b);
        const float mass = s.pixels > 0 ? static_cast<float>(r.pixels) / static_cast<float>(s.pixels) : 0.0f;
        std::printf("  %11.1f  %8.1f  %8d  %8d  %4.2f  %6d  %14.1f%%\n", static_cast<double>(s.radius()),
                    static_cast<double>(nearD), s.pixels, r.pixels, static_cast<double>(mass),
                    (r.maxY - r.minY) - (s.maxY - s.minY), 100.0 * static_cast<double>(share));
    }
    CHECK(ctx->errorCount() == 0);

    // The control. The measure has to be able to see the swap at all -- a metric that reported
    // nothing at every size would print four zeroes and read as "the ladder is free" (ADR-182).
    REQUIRE(shares.front() > 0.0f);
}
