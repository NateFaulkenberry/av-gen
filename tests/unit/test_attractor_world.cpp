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

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::AttractorGen smallGen(std::uint32_t seed) {
    scene::AttractorGen g;
    g.seed = seed;
    g.resolution = 384; // small: the generator's cost is per texel, and these tests only need its shape
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
    }
}

TEST_CASE("the block parses, round-trips and refuses what it does not know", "[attractor-world][adr1850]") {
    auto a = scene::attractorWorldFromJson(nlohmann::json::parse(
        R"({"seed": 9, "resolution": 512, "strataFold": 0.3, "camera": false, "look": {"palette": 1.5, "exposure": 0.8}})"));
    REQUIRE(a.has_value());
    CHECK(a->enabled);
    CHECK_FALSE(a->driveCamera);
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
}

TEST_CASE("a seed change regenerates the world in place; a look change never does", "[attractor-world][adr1850]") {
    Comp c(sceneWith({{"seed", 4}, {"resolution", 384}}));
    for (const char* leaf : {"seed", "terrainScale", "roughness", "ridgeStrength", "domainWarp", "terraces", "erosion",
                             "strataFold", "palette", "sunAzimuth", "sunElevation", "sunIntensity", "ambient",
                             "atmosphere", "strata", "exposure", "detail", "camera"}) {
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
    c.params.findAs<float>("attractorWorld/camera")->setBase(0.0f);
    c.frame(4.0 / 30.0);
    CHECK(glm::length(c.comp->scene().camera.position - glm::vec3(0, 50, 300)) < 1e-3f);

    // the scene file keeps the authored block
    const nlohmann::json saved = c.comp->toJson();
    REQUIRE(saved.contains("attractorWorld"));
    CHECK(saved["attractorWorld"]["seed"].get<int>() == 4);
}
