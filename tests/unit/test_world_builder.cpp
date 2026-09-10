// Generate World (ADR-066). This is the integration test the whole phase is about: a recipe and an
// asset library go in, a job runs, and the engine's composition comes out with an ecology on a
// terrain node and a composition the density filters can actually read.

#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "app/world_builder.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>

using namespace avgen;
using namespace std::chrono_literals;

namespace {
assets::AssetLibrary library() {
    const auto doc = nlohmann::json::parse(R"({"assets":[
      {"name":"hero","category":"flora","file":"a.glb","visualImportance":0.95,
       "preferredScale":12.0,"material":{"emissive":0.9}},
      {"name":"bush","category":"flora","file":"b.glb","visualImportance":0.3,"preferredScale":2.0},
      {"name":"fern","category":"flora","file":"c.glb","visualImportance":0.15,
       "preferredScale":0.6,"preferredDensity":0.3},
      {"name":"cap","category":"fungi","file":"d.glb","visualImportance":0.4,"preferredScale":0.5,
       "material":{"emissive":0.8}},
      {"name":"rock","category":"rock","file":"e.glb","visualImportance":0.25,"preferredScale":3.0}
    ]})");
    auto lib = assets::AssetLibrary::fromJson(doc, "/tmp/lib");
    REQUIRE(lib.has_value());
    return std::move(*lib);
}

world::WorldRecipe recipe() {
    const auto doc = nlohmann::json::parse(R"({
      "world":"valley","seed":7,"extent":320.0,
      "composition":{"foreground":0.9,"midground":0.7,"background":0.5,"negative_space":0.4,
                     "focalStrength":0.9},
      "ecology":{"flora":0.9,"fungi":0.6,"rock":0.4}})");
    auto r = world::WorldRecipe::fromJson(doc);
    REQUIRE(r.has_value());
    return *r;
}
} // namespace

TEST_CASE("Generate World runs as a job and produces an installable world", "[app][worldbuilder]") {
    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);
    const auto id = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(id, 10s));

    app::JobStatus status;
    REQUIRE(jobs.status(id, status));
    INFO(status.error);
    CHECK(status.state == app::JobState::Completed);
    CHECK(status.stageCount == 3);
    CHECK(status.progressKnown);   // every stage of this job measures itself
    CHECK(status.progress == 1.0f);

    auto worlds = builder.collect();
    REQUIRE(worlds.size() == 1);
    CHECK(worlds[0].recipe.world == "valley");
    CHECK(!worlds[0].composed.layers.empty());
    CHECK(worlds[0].assetsConsidered == 5);
    // Collected once and only once: the main thread takes them, and taking them again must not
    // install the same world twice.
    CHECK(builder.collect().empty());
}

TEST_CASE("Installing a world puts an ecology on a terrain node", "[app][worldbuilder]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.composition() != nullptr);

    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);
    const auto id = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(id, 10s));
    auto worlds = builder.collect();
    REQUIRE(worlds.size() == 1);

    auto installed = app::installWorld(engine, worlds[0]);
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());

    // A terrain was created for it, sized from the recipe, because Generate World has to work on an
    // empty project rather than demanding a terrain first.
    const scene::CompositionNode* terrain = nullptr;
    for (const auto& node : engine.composition()->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            terrain = node.get();
        }
    }
    REQUIRE(terrain != nullptr);
    CHECK(terrain->worldMap.size.x == 320.0f);
    CHECK(terrain->worldMap.seed == 7);
    CHECK(!terrain->worldMap.layers.empty());   // a flat plane makes every slope rule a no-op

    // The ecology is the composer's output, placed by the one authoritative placer.
    CHECK(terrain->ecology.layers.size() == worlds[0].composed.layers.size());
    CHECK(!terrain->ecology.layers.empty());

    SECTION("the plan becomes CompositionData, not a second description of the same idea") {
        const auto& data = engine.composition()->composition();
        CHECK(data.focalPoints.size() == worlds[0].composed.plan.focal.size());
        CHECK(data.exclusions.size() == worlds[0].composed.plan.voids.size());
        REQUIRE(!data.focalPoints.empty());
        CHECK(data.focalPoints[0].name == "hero");   // the most important asset in the library
    }
}

TEST_CASE("Generating twice replaces the ecology rather than doubling it", "[app][worldbuilder]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);

    const auto first = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(first, 10s));
    auto a = builder.collect();
    REQUIRE(a.size() == 1);
    REQUIRE(app::installWorld(engine, a[0]).has_value());

    std::size_t afterFirst = 0;
    for (const auto& node : engine.composition()->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            afterFirst = node->ecology.layers.size();
        }
    }
    REQUIRE(afterFirst > 0);

    const auto second = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(second, 10s));
    auto b = builder.collect();
    REQUIRE(b.size() == 1);
    REQUIRE(app::installWorld(engine, b[0]).has_value());

    std::size_t terrains = 0;
    std::size_t afterSecond = 0;
    for (const auto& node : engine.composition()->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            ++terrains;
            afterSecond = node->ecology.layers.size();
        }
    }
    CHECK(terrains == 1);            // it reused the terrain rather than adding another
    CHECK(afterSecond == afterFirst);// and replaced the ecology rather than appending to it
}

TEST_CASE("The same recipe and seed generate the same world", "[app][worldbuilder]") {
    // The seed has to matter, and the result has to be reproducible, or a saved world is not a
    // world -- it is a screenshot of one.
    app::JobSystem jobs(2);
    app::WorldBuilder builder(jobs);
    const auto a = builder.generate(recipe(), library());
    const auto b = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(a, 10s));
    REQUIRE(jobs.waitFor(b, 10s));
    auto worlds = builder.collect();
    REQUIRE(worlds.size() == 2);
    REQUIRE(worlds[0].composed.layers.size() == worlds[1].composed.layers.size());
    for (std::size_t i = 0; i < worlds[0].composed.layers.size(); ++i) {
        CHECK(worlds[0].composed.layers[i].name == worlds[1].composed.layers[i].name);
    }
    REQUIRE(!worlds[0].composed.plan.focal.empty());
    CHECK(worlds[0].composed.plan.focal[0].center == worlds[1].composed.plan.focal[0].center);

    SECTION("and a different seed generates a different one") {
        auto other = recipe();
        other.seed = 99;
        const auto c = builder.generate(other, library());
        REQUIRE(jobs.waitFor(c, 10s));
        auto more = builder.collect();
        REQUIRE(more.size() == 1);
        CHECK(more[0].composed.plan.focal[0].center != worlds[0].composed.plan.focal[0].center);
    }
}

TEST_CASE("A bad recipe fails the job with a reason, and installs nothing", "[app][worldbuilder]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);

    auto broken = recipe();
    broken.extent = -5.0f;
    const auto id = builder.generate(broken, library());
    REQUIRE(jobs.waitFor(id, 10s));
    app::JobStatus status;
    REQUIRE(jobs.status(id, status));
    CHECK(status.state == app::JobState::Failed);
    CHECK(status.error.find("extent") != std::string::npos);
    CHECK(builder.collect().empty());   // nothing to install, so nothing was installed
}

TEST_CASE("Installing without a composition says so instead of crashing", "[app][worldbuilder]") {
    app::Engine engine(app::EngineMode::Offline);
    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);
    const auto id = builder.generate(recipe(), library());
    REQUIRE(jobs.waitFor(id, 10s));
    auto worlds = builder.collect();
    REQUIRE(worlds.size() == 1);
    auto installed = app::installWorld(engine, worlds[0]);
    REQUIRE(!installed.has_value());
    CHECK(installed.error().message.find("composition") != std::string::npos);
}
