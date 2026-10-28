// THE STRANGE ATTRACTOR (ADR-1850): the `"attractorWorld"` block, its deterministic generator and its parameters.
// Brief §9.1 (deterministic generation) and §9.2 (geometry correctness); the GPU half (the pass draws, a look change
// never re-uploads) is tests/rendering/test_attractor_world_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/attractor_world.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace avgen;
using Catch::Approx;

namespace {

scene::AttractorGen smallGen(std::uint32_t seed) {
    scene::AttractorGen g;
    g.seed = seed;
    g.resolution = 384; // small: the generator's cost is per texel, and these tests only need its shape
    g.meshSpacing = 16.0f; // a 1025^2 triangulation grid, so the engine mesh is built in a fraction of a second
    return g;
}

nlohmann::json sceneWith(const nlohmann::json& block) {
    return nlohmann::json{{"format", "avgen-scene"}, {"version", 1}, {"name", "attractor"},
                          {"camera", {{"mode", 1}, {"position", {0, 50, 300}}, {"target", {0, 0, 0}}, {"fov", 40.0}}},
                          {"attractorWorld", block}};
}

struct Comp {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    explicit Comp(const nlohmann::json& scene) {
        auto c = scene::Composition::fromJson(scene, registry);
        REQUIRE(c.has_value());
        comp = std::move(*c);
        comp->attach(params, modulator);
    }
    void frame(double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        comp->update(ft);
    }
};

} // namespace

TEST_CASE("the same seed and knobs make the same world, bit for bit", "[attractor-world][adr1850]") {
    const auto a = scene::generateAttractorWorld(smallGen(7));
    const auto b = scene::generateAttractorWorld(smallGen(7));
    REQUIRE(a->height.size() == b->height.size());
    CHECK(a->hash == b->hash);
    CHECK(a->height == b->height);
    CHECK(a->masks == b->masks);
    CHECK(a->eye == b->eye);
    CHECK(a->target == b->target);
    CHECK(a->valley == b->valley);
}

TEST_CASE("different seeds make different compositions, not moved copies", "[attractor-world][adr1850]") {
    const auto a = scene::generateAttractorWorld(smallGen(1));
    const auto b = scene::generateAttractorWorld(smallGen(2));
    CHECK(a->hash != b->hash);
    // the height fields differ over most of the square, not in a few texels
    std::size_t differ = 0;
    for (std::size_t i = 0; i < a->height.size(); ++i) {
        differ += std::abs(a->height[i] - b->height[i]) > 5.0f ? 1u : 0u;
    }
    CHECK(differ > a->height.size() / 3);
    // and the composition itself moves: the camera, the masses
    CHECK(glm::length(a->eye - b->eye) > 20.0f);
    CHECK(glm::length(a->masses[1].centre - b->masses[1].centre) > 50.0f);
}

TEST_CASE("the generated world is finite, bounded and framed", "[attractor-world][adr1850]") {
    for (std::uint32_t seed : {1u, 3u, 11u}) {
        INFO("seed " << seed);
        const auto w = scene::generateAttractorWorld(smallGen(seed));
        REQUIRE(w->res == 384);
        REQUIRE(w->height.size() == 384u * 384u);
        REQUIRE(w->masks.size() == w->height.size());
        bool finite = true;
        for (float h : w->height) {
            finite = finite && std::isfinite(h);
        }
        CHECK(finite);
        CHECK(w->minHeight > -50.0f);
        CHECK(w->maxHeight < 2000.0f);
        CHECK(w->maxHeight - w->minHeight > 200.0f); // a real landscape, not a plain
        // the camera stands above the ground it is on, and looks down a little, inside the square
        CHECK(w->eye.y > w->sample(glm::vec2(w->eye.x, w->eye.z)));
        CHECK(w->target.y < w->eye.y);
        CHECK(std::abs(w->eye.x) < w->extent * 0.5f);
        CHECK(std::abs(w->eye.z) < w->extent * 0.5f);
        CHECK(w->fovDeg > 20.0f);
        CHECK(w->fovDeg < 60.0f);
        CHECK(w->masses.size() == 3u);
        CHECK(w->valley.size() > 16u);
        CHECK_FALSE(w->branches.empty());
        CHECK(w->branches.size() <= static_cast<std::size_t>(scene::kAttractorMaxBranches));
    }
}

TEST_CASE("the block parses, round-trips and refuses what it does not know", "[attractor-world][adr1850]") {
    auto a = scene::attractorWorldFromJson(nlohmann::json::parse(
        R"({"seed": 9, "resolution": 512, "strataFold": 0.3, "camera": "flight", "look": {"palette": 1.5, "exposure": 0.8}})"));
    REQUIRE(a.has_value());
    CHECK(a->enabled);
    CHECK(a->camera == scene::AttractorCameraMode::Flight);
    CHECK(a->gen.seed == 9u);
    CHECK(a->gen.resolution == 512);
    CHECK(a->gen.strataFold == Approx(0.3f));
    CHECK(a->look.palette == Approx(1.5f));
    auto back = scene::attractorWorldFromJson(scene::attractorWorldToJson(*a));
    REQUIRE(back.has_value());
    CHECK(back->gen.hash() == a->gen.hash());
    CHECK(back->look.exposure == Approx(0.8f));

    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"sead": 1})")).has_value());
    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"look": {"pallete": 1}})")).has_value());
    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"resolution": 100000})")).has_value());
    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"erosion": 3})")).has_value());
    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"terrainScale": "big"})")).has_value());
    CHECK_FALSE(scene::attractorWorldFromJson(nlohmann::json::parse(R"({"camera": "drone"})")).has_value());
}

TEST_CASE("a seed change regenerates the world in place; a look change never does", "[attractor-world][adr1850]") {
    Comp c(sceneWith({{"seed", 4}, {"resolution", 384}}));
    for (const char* leaf : {"seed", "terrainScale", "roughness", "ridgeStrength", "domainWarp", "terraces", "erosion",
                             "strataFold", "palette", "sunAzimuth", "sunElevation", "sunIntensity", "ambient",
                             "atmosphere", "strata", "exposure", "detail", "camera", "branchingDensity", "flightSeconds"}) {
        INFO(leaf);
        CHECK(c.params.find(std::string("attractorWorld/") + leaf) != nullptr);
    }
    c.frame(0.0);
    const auto first = c.comp->scene().attractorWorld.world;
    REQUIRE(first != nullptr);
    CHECK(c.comp->scene().attractorWorld.active());
    // the generated composition places the camera
    CHECK(glm::length(c.comp->scene().camera.position - first->eye) < 1e-3f);

    // a look change: same world object, no generation
    const std::uint64_t gens = scene::attractorWorldGenerations();
    c.params.findAs<float>("attractorWorld/palette")->setBase(2.0f);
    c.params.findAs<float>("attractorWorld/sunElevation")->setBase(20.0f);
    c.frame(1.0 / 30.0);
    CHECK(c.comp->scene().attractorWorld.world == first);
    CHECK(c.comp->scene().attractorWorld.live.palette == Approx(2.0f));
    CHECK(scene::attractorWorldGenerations() == gens);

    // a seed change: a new world, once, and the camera follows it
    c.params.findAs<int>("attractorWorld/seed")->setBase(5);
    c.frame(2.0 / 30.0);
    const auto second = c.comp->scene().attractorWorld.world;
    REQUIRE(second != nullptr);
    CHECK(second != first);
    CHECK(second->gen.seed == 5u);
    CHECK(scene::attractorWorldGenerations() == gens + 1);
    c.frame(3.0 / 30.0);
    CHECK(c.comp->scene().attractorWorld.world == second);
    CHECK(scene::attractorWorldGenerations() == gens + 1);
    CHECK(glm::length(c.comp->scene().camera.position - second->eye) < 1e-3f);

    // the camera hands back to the scene's own
    c.params.findAs<int>("attractorWorld/camera")->setBase(0);
    c.frame(4.0 / 30.0);
    CHECK(glm::length(c.comp->scene().camera.position - glm::vec3(0, 50, 300)) < 1e-3f);

    // the scene file keeps the authored block
    const nlohmann::json saved = c.comp->toJson();
    REQUIRE(saved.contains("attractorWorld"));
    CHECK(saved["attractorWorld"]["seed"].get<int>() == 4);
}

TEST_CASE("in the live editor a seed change generates on a worker and the old world stays until it lands",
          "[attractor-world][adr1851]") {
    Comp c(sceneWith({{"seed", 21}, {"resolution", 384}}));
    c.comp->setInteractiveRebuildBudget(2.0); // what the live engine sets; offline never does
    c.frame(0.0);
    const auto first = c.comp->scene().attractorWorld.world;
    REQUIRE(first != nullptr);
    c.params.findAs<int>("attractorWorld/seed")->setBase(22);
    c.frame(1.0 / 30.0);
    // the frame that asked did not wait: the old world is still drawn (unless the worker was already done)
    const auto during = c.comp->scene().attractorWorld.world;
    CHECK((during == first || during->gen.seed == 22u));
    std::shared_ptr<const scene::AttractorWorld> now = during;
    for (int i = 0; i < 400 && now->gen.seed != 22u; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        c.frame((2.0 + i) / 30.0);
        now = c.comp->scene().attractorWorld.world;
    }
    CHECK(now->gen.seed == 22u);
    CHECK_FALSE(c.comp->attractorWorldGenerating());
    // the same world as a synchronous generation would make
    CHECK(now->height == scene::generateAttractorWorld(now->gen)->height);
}

// ---- the flythrough (ADR-1853) ------------------------------------------------------------------------------------

TEST_CASE("the flythrough is deterministic, smooth, inside the world and never touches the rock", "[attractor-world][adr1853]") {
    for (std::uint32_t seed : {1u, 2u, 5u}) {
        INFO("seed " << seed);
        scene::AttractorGen g;
        g.seed = seed;
        g.resolution = 1024;
        const auto w = scene::generateAttractorWorld(g);
        REQUIRE(w->flight.size() == static_cast<std::size_t>(g.flightSeconds * scene::AttractorWorld::kFlightRate) + 1);
        CHECK(w->flight == scene::generateAttractorWorld(g)->flight);
        float maxStep = 0.0f, maxRoll = 0.0f, minClear = 1e9f, lowest = 1e9f;
        for (std::size_t i = 0; i < w->flight.size(); ++i) {
            const glm::vec4 f = w->flight[i];
            REQUIRE(std::isfinite(f.x + f.y + f.z + f.w));
            CHECK(std::abs(f.x) < 0.75f * w->extent * 0.5f); // deep inside the square: no edge to see
            CHECK(std::abs(f.z) < 0.75f * w->extent * 0.5f);
            // clear of everything within 20 m around it
            float ground = w->sample(glm::vec2(f.x, f.z));
            for (int a = 0; a < 8; ++a) {
                const float ang = static_cast<float>(a) * 0.785398f;
                ground = std::max(ground, w->sample(glm::vec2(f.x, f.z) + 20.0f * glm::vec2(std::cos(ang), std::sin(ang))));
            }
            minClear = std::min(minClear, f.y - ground);
            lowest = std::min(lowest, f.y - w->sample(glm::vec2(f.x, f.z)));
            maxRoll = std::max(maxRoll, std::abs(f.w));
            if (i > 0) {
                const float st = glm::length(glm::vec3(f) - glm::vec3(w->flight[i - 1]));
                maxStep = std::max(maxStep, st);
            }
        }
        CHECK(minClear > 10.0f);
        CHECK(lowest < 70.0f);      // it really goes down into the gorge
        CHECK(maxStep < 120.0f / 30.0f); // under 120 m/s
        CHECK(maxRoll <= 0.2f + 1e-4f);
        // the pose: starts at the composed vista, looks ahead, and holds after the end
        const auto p0 = scene::attractorCamera(*w, scene::AttractorCameraMode::Flight, 0.0);
        CHECK(glm::length(p0.eye - w->eye) < 5.0f); // the flight opens on the vista (smoothing may settle a few metres)
        const auto pm = scene::attractorCamera(*w, scene::AttractorCameraMode::Flight, 25.0);
        CHECK(glm::length(pm.target - pm.eye) > 20.0f);
        const auto pe = scene::attractorCamera(*w, scene::AttractorCameraMode::Flight, 1000.0);
        CHECK(glm::length(pe.eye - glm::vec3(w->flight.back())) < 1e-3f);
        // the composed mode is the still vista
        const auto pc = scene::attractorCamera(*w, scene::AttractorCameraMode::Composed, 25.0);
        CHECK(pc.eye == w->eye);
    }
}

// A measurement, not a check: the scene camera of the flythrough frame by frame, stepped as an offline render steps it,
// written to $AVGEN_CAMERA_TRACE (CSV). Hidden; run by hand.
TEST_CASE("attractor flythrough camera trace (hidden)", "[.attractor-camera-trace]") {
    const char* out = std::getenv("AVGEN_CAMERA_TRACE");
    const char* scenePath = std::getenv("AVGEN_CAMERA_TRACE_SCENE");
    REQUIRE(out != nullptr);
    REQUIRE(scenePath != nullptr);
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::loadFile(scenePath, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    std::FILE* f = std::fopen(out, "w");
    REQUIRE(f != nullptr);
    std::fprintf(f, "frame,t,px,py,pz,fx,fy,fz,ux,uy,uz\n");
    for (int i = 0; i < 46 * 30 + 1; ++i) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = i / 30.0;
        ft.deltaTime = 1.0 / 30.0;
        ft.frameIndex = static_cast<std::uint64_t>(i);
        (*c)->update(ft);
        const scene::Camera& cam = (*c)->scene().camera;
        const glm::vec3 fw = glm::normalize(cam.target - cam.position);
        std::fprintf(f, "%d,%.6f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", i, i / 30.0, cam.position.x, cam.position.y,
                     cam.position.z, fw.x, fw.y, fw.z, cam.up.x, cam.up.y, cam.up.z);
    }
    std::fclose(f);
}

// ---- the engine-native terrain mesh (ADR-1857) ------------------------------------------------------------------

TEST_CASE("the terrain mesh is valid, wound up, finite, bounded, deterministic and follows the surface",
          "[attractor-world][adr1857]") {
    const auto w = scene::generateAttractorWorld(smallGen(3));
    REQUIRE(w->terrainMesh != nullptr);
    const scene::AttractorTerrainMesh& tm = *w->terrainMesh;
    CHECK(tm.triangles > 1000u);
    CHECK(tm.chunks.size() == 256u);
    std::size_t tris = 0, verts = 0, degenerate = 0, downward = 0;
    float maxErr = 0.0f;
    for (const auto& ch : tm.chunks) {
        REQUIRE(ch.positions.size() == ch.normals.size());
        REQUIRE(ch.positions.size() == ch.uvs.size());
        REQUIRE(ch.indices.size() % 3 == 0);
        verts += ch.positions.size();
        tris += ch.indices.size() / 3;
        for (std::uint32_t i : ch.indices) REQUIRE(i < ch.positions.size());
        for (std::size_t i = 0; i < ch.positions.size(); ++i) {
            const glm::vec3 p = ch.positions[i], n = ch.normals[i];
            REQUIRE(std::isfinite(p.x + p.y + p.z + n.x + n.y + n.z + ch.uvs[i].x + ch.uvs[i].y));
            CHECK(std::abs(glm::length(n) - 1.0f) < 1e-3f);
            CHECK(n.y > 0.0f); // a height field's normal points up
            CHECK(glm::all(glm::greaterThanEqual(p, ch.boundsMin - 1e-3f)));
            CHECK(glm::all(glm::lessThanEqual(p, ch.boundsMax + 1e-3f)));
            maxErr = std::max(maxErr, std::max(w->minHeight - p.y, p.y - w->maxHeight));
        }
        for (std::size_t i = 0; i + 2 < ch.indices.size(); i += 3) {
            const glm::vec3 a = ch.positions[ch.indices[i]], b = ch.positions[ch.indices[i + 1]], c = ch.positions[ch.indices[i + 2]];
            const glm::vec3 cr = glm::cross(b - a, c - a);
            if (glm::length(cr) < 1e-6f) ++degenerate;
            if (cr.y < 0.0f) ++downward; // every face is wound counter-clockwise seen from above
        }
    }
    CHECK(tris == tm.triangles);
    CHECK(verts == tm.vertices);
    CHECK(degenerate == 0u);
    CHECK(downward == 0u);
    // the vertices are samples of the rendered surface (the B-spline of the bake), which stays inside the bake's range
    CHECK(maxErr < 1.0f);
    // deterministic
    const auto again = scene::generateAttractorWorld(smallGen(3));
    CHECK(again->terrainMesh->chunks[100].positions == tm.chunks[100].positions);
    CHECK(again->terrainMesh->chunks[100].indices == tm.chunks[100].indices);
    // the river sits at the water line, over ground that dips below it
    CHECK_FALSE(tm.riverIndices.empty());
    for (const glm::vec3& p : tm.riverPositions) CHECK(p.y == Approx(-0.8f));
    // off: no mesh
    scene::AttractorGen g = smallGen(3);
    g.mesh = false;
    CHECK(scene::generateAttractorWorld(g)->terrainMesh == nullptr);
}

TEST_CASE("the composition draws the terrain mesh as scene entities with the block's program, the river as water",
          "[attractor-world][adr1857]") {
    Comp c(sceneWith({{"seed", 4}, {"resolution", 384}, {"meshSpacing", 16.0}, {"terrainProgram", "rock"}}));
    c.frame(0.0);
    const scene::Scene& s = c.comp->scene();
    std::size_t terrain = 0, water = 0;
    for (const auto& e : s.entities) {
        if (e.name.rfind("attractor.terrain", 0) == 0) {
            ++terrain;
            CHECK(e.material.program == "rock");
            CHECK(e.style == scene::MeshStyle::Lit);
        }
        if (e.name == "attractor.river") {
            ++water;
            CHECK(e.style == scene::MeshStyle::Water);
        }
    }
    CHECK(terrain > 100u);
    CHECK(water == 1u);
    CHECK(s.waters.size() == 1u);
    bool sun = false;
    for (const auto& l : s.lights) sun = sun || l.name == "attractor.sun";
    CHECK(sun);
}

// ---- the terrain mesh's baked occlusion and uv.y packing (ADR-1859) ---------------------------------------------

TEST_CASE("the terrain mesh packs a whole playa level and the baked occlusion into uv.y", "[attractor-world][adr1859]") {
    const auto w = scene::generateAttractorWorld(smallGen(3));
    REQUIRE(w->terrainMesh != nullptr);
    std::size_t n = 0, occluded = 0, open = 0, playa = 0;
    double sumUp = 0.0, sumSteep = 0.0;
    std::size_t nUp = 0, nSteep = 0;
    for (const auto& ch : w->terrainMesh->chunks) {
        for (std::size_t i = 0; i < ch.uvs.size(); ++i) {
            const float y = ch.uvs[i].y;
            const float level = std::floor(y);
            const float ao = (y - level) / scene::kAttractorOcclusionScale;
            REQUIRE(level >= 0.0f);
            REQUIRE(level <= static_cast<float>(scene::kAttractorPlayaLevels));
            REQUIRE(ao >= -1e-3f);
            REQUIRE(ao <= 1.0f + 1e-3f);
            ++n;
            if (ao < 0.7f) ++occluded;
            if (ao > 0.97f) ++open;
            if (level > 0.0f) ++playa;
            // the horizon is measured from the tangent plane: a wall is not dark merely for facing sideways
            if (ch.normals[i].y > 0.95f) { sumUp += ao; ++nUp; }
            if (ch.normals[i].y < 0.4f) { sumSteep += ao; ++nSteep; }
        }
    }
    REQUIRE(n > 1000u);
    INFO("occluded " << occluded << ", open " << open << ", playa " << playa << " of " << n);
    CHECK(occluded > 0u); // crevices exist
    CHECK(open > n / 10); // and so does open ground
    REQUIRE(nUp > 0u);
    REQUIRE(nSteep > 0u);
    INFO("mean visibility: flat " << sumUp / nUp << ", steep " << sumSteep / nSteep);
    CHECK(sumSteep / nSteep > 0.6); // not the half a sideways-facing horizon search would give
}

// ---- uv.x is the unwarped strata coordinate (ADR-1860) -----------------------------------------------------------

TEST_CASE("the terrain mesh's uv.x interpolates the strata coordinate better unwarped than warped",
          "[attractor-world][adr1860]") {
    const auto w = scene::generateAttractorWorld(smallGen(3));
    REQUIRE(w->terrainMesh != nullptr);
    const float band = w->strataSeed[0].z;
    REQUIRE(band > 0.0f);
    // attractor_world.cpp's bandWarp, line for line: what uv.x used to carry (and what the program now applies)
    const auto warp = [band](float s) {
        const float a = s / (band * 2.3f) + 0.7f, b = s / (band * 0.93f) + 1.7f;
        return s + band * (0.32f * std::sin(a) + 0.18f * std::sin(b));
    };
    const float texel = w->extent / static_cast<float>(w->res);
    const auto offsetAt = [&](glm::vec2 xz) { // bilinear over the baked strata offset (texel centres)
        const glm::vec2 u = (xz - w->origin) / texel - 0.5f;
        const glm::ivec2 i0 = glm::clamp(glm::ivec2(glm::floor(u)), glm::ivec2(0), glm::ivec2(w->res - 2));
        const glm::vec2 f = glm::clamp(u - glm::vec2(i0), 0.0f, 1.0f);
        const auto at = [&](int x, int y) { return w->strataOffset[static_cast<std::size_t>(y) * w->res + x]; };
        return glm::mix(glm::mix(at(i0.x, i0.y), at(i0.x + 1, i0.y), f.x), glm::mix(at(i0.x, i0.y + 1), at(i0.x + 1, i0.y + 1), f.x), f.y);
    };
    // At each tall triangle's centroid (several bands of drop, where the streaks were), compare what the rasteriser
    // interpolates with the true coordinate there, both before and after the warp the program applies.
    double errUnwarped = 0.0, errWarped = 0.0;
    std::size_t tall = 0;
    for (const auto& ch : w->terrainMesh->chunks) {
        for (std::size_t i = 0; i + 2 < ch.indices.size(); i += 3) {
            const std::uint32_t v[3] = {ch.indices[i], ch.indices[i + 1], ch.indices[i + 2]};
            float lo = 1e30f, hi = -1e30f, s = 0.0f, ws = 0.0f;
            glm::vec3 c(0.0f);
            for (const std::uint32_t k : v) {
                lo = std::min(lo, ch.positions[k].y);
                hi = std::max(hi, ch.positions[k].y);
                c += ch.positions[k] / 3.0f;
                s += ch.uvs[k].x * band / 3.0f;
                ws += warp(ch.uvs[k].x * band) / 3.0f;
            }
            if (hi - lo < 4.0f * band) continue;
            ++tall;
            const float truth = c.y + offsetAt(glm::vec2(c.x, c.z));
            // the program warps the interpolated value; the old mesh interpolated the warped values
            errUnwarped += std::abs(warp(s) - warp(truth)) / band;
            errWarped += std::abs(ws - warp(truth)) / band;
        }
    }
    REQUIRE(tall > 0u);
    errUnwarped /= static_cast<double>(tall);
    errWarped /= static_cast<double>(tall);
    INFO("tall triangles " << tall << ", mean band error at the centroid: unwarped " << errUnwarped << ", warped " << errWarped);
    CHECK(errUnwarped < errWarped);
}
