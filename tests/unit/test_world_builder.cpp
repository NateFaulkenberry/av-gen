// Generate World (ADR-066). This is the integration test the whole phase is about: a recipe and an
// asset library go in, a job runs, and the engine's composition comes out with an ecology on a
// terrain node and a composition the density filters can actually read.

#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "app/world_builder.hpp"
#include "support/gltf_fixture.hpp"
#include "ui/world_edit.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <unistd.h>

using namespace avgen;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

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

// The plan's heroes used to be seen only by `installWorld` and by the camera director's fallback to
// the panel that still held the plan: the composition's own hero list stayed empty. A generated
// world therefore had nothing starred in the editor, saved no heroes with its scene, and lost them
// the moment that panel let go -- while `--direct` still worked, which is what kept it hidden.
//
// This needs a library of files that exist, unlike the fixture above: a hero is declared only if its
// node was actually placed, and a node whose glTF cannot be loaded is not placed.
TEST_CASE("A generated world declares the heroes it placed", "[app][worldbuilder][heroes]") {
    const fs::path dir = fs::temp_directory_path() /
                         ("avgen_world_heroes_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto triangle = testsupport::writeTriangleGlb("world_heroes");
    for (const char* name : {"a.glb", "b.glb", "c.glb", "d.glb", "e.glb"}) {
        fs::copy_file(triangle, dir / name, fs::copy_options::overwrite_existing);
    }
    fs::remove(triangle);
    const auto doc = nlohmann::json::parse(R"({"assets":[
      {"name":"hero","category":"flora","file":"a.glb","visualImportance":0.95,"preferredScale":12.0},
      {"name":"bush","category":"flora","file":"b.glb","visualImportance":0.3,"preferredScale":2.0},
      {"name":"fern","category":"flora","file":"c.glb","visualImportance":0.15,"preferredScale":0.6},
      {"name":"cap","category":"fungi","file":"d.glb","visualImportance":0.4,"preferredScale":0.5},
      {"name":"rock","category":"rock","file":"e.glb","visualImportance":0.25,"preferredScale":3.0}
    ]})");
    auto lib = assets::AssetLibrary::fromJson(doc, dir.string());
    REQUIRE(lib.has_value());

    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    app::JobSystem jobs(1);
    app::WorldBuilder builder(jobs);
    const auto id = builder.generate(recipe(), std::move(*lib));
    REQUIRE(jobs.waitFor(id, 30s));
    auto worlds = builder.collect();
    REQUIRE(worlds.size() == 1);
    REQUIRE(!worlds[0].composed.plan.heroes.empty());
    REQUIRE(app::installWorld(engine, worlds[0]).has_value());

    const std::vector<world::HeroPoint>& declared = engine.composition()->heroes();
    REQUIRE(!declared.empty());
    for (const world::HeroPoint& hero : declared) {
        INFO(hero.name);
        // Each stands on a node that is really in the scene, and the editor's star finds it.
        const scene::CompositionNode* node = engine.composition()->findNode(hero.name);
        REQUIRE(node != nullptr);
        CHECK(ui::nodeIsHero(*engine.composition(), hero.name));
        // At the height the node was placed at, not the one the planner guessed: the installer
        // drops a hero onto the terrain, and a hero declared in the air above its object would
        // stand the camera off from a point in the sky.
        CHECK_THAT(hero.position.y, Catch::Matchers::WithinAbs(node->transform.position.y, 1e-4));
        CHECK(hero.validate().has_value());
    }
    // Ranked, which is what `briefFromHeroes` reads: the first is the subject.
    for (std::size_t i = 1; i < declared.size(); ++i) {
        CHECK(declared[i - 1].importance >= declared[i].importance);
    }

    // Generating again replaces the declaration rather than stacking a second copy of every hero.
    const std::size_t once = declared.size();
    const auto again = builder.generate(recipe(), assets::AssetLibrary(worlds[0].library));
    REQUIRE(jobs.waitFor(again, 30s));
    auto second = builder.collect();
    REQUIRE(second.size() == 1);
    REQUIRE(app::installWorld(engine, second[0]).has_value());
    CHECK(engine.composition()->heroes().size() == once);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
