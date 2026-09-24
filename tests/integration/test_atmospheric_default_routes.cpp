// The default audio routes, and the fact that until ADR-392 nothing called them.
//
// ADR-230's family states that audio reaches an effect as an ordinary modulation route and never as
// a hook, and `engine.cpp` says in a comment that `defaultEffectRoutes` "is what implements
// it". It implemented nothing: the function had no caller anywhere in `src/`, so adding an aurora
// from the World Effects panel produced an aurora that answered nothing.
//
// These cases are about the wiring, not about the table -- `tests/unit/test_effect_conformance.cpp`
// owns the question of whether the routes name real paths. Here the questions are: does adding an
// effect attach them, does adding it twice stack them, does the right kind get the right ones, and
// does a route survive the save the render loads (ADR-350, ADR-264).

#include "app/engine.hpp"
#include "world/effects/effect_params.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_conformance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
namespace conf = avgen::world::conformance;

namespace {

fs::path scratch(const char* name) {
    const fs::path dir =
        fs::temp_directory_path() / ("avgen_atmos_routes_" + std::to_string(getpid()) + "_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::vector<std::string> targetsUnder(app::Engine& engine, const std::string& prefix) {
    std::vector<std::string> out;
    for (const params::ModRoute& r : engine.modulator().routes()) {
        if (r.target.starts_with(prefix)) {
            out.push_back(r.target);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("adding an atmospheric effect attaches its default audio routes",
          "[integration][atmospherics][modulation]") {
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const std::string name = std::string("Added ") + world::effectKindName(kind);
        INFO("kind: " << world::effectKindName(kind));

        app::Engine engine(app::EngineMode::Offline);
        engine.newComposition();
        const std::string prefix = world::effectParameterPrefix(name);

        // The control: before the effect exists, there is nothing to attach and nothing is
        // attached. Without this arm, "routes appeared" would not distinguish the wiring from a
        // function that adds routes to anything it is handed (ADR-182).
        CHECK(engine.addDefaultAtmosphericRoutes(name) == 0);
        CHECK(targetsUnder(engine, prefix).empty());

        REQUIRE(engine.setAtmosphericEffects({conf::probeEffect(kind, name)}).has_value());
        const std::size_t added = engine.addDefaultAtmosphericRoutes(name);
        CHECK(added > 0);

        const std::vector<std::string> targets = targetsUnder(engine, prefix);
        CHECK(targets.size() == added);
        // Every one of them resolves, which is the property the vortex did not have: it was handed
        // the comet's three targets and none of them was a path a vortex registers.
        for (const std::string& t : targets) {
            INFO(t);
            CHECK(engine.params().find(t) != nullptr);
        }

        // Idempotent. Pressing the button twice, or adding a second effect and coming back, must
        // not stack a second set on top of the first.
        CHECK(engine.addDefaultAtmosphericRoutes(name) == 0);
        CHECK(targetsUnder(engine, prefix) == targets);
    }
}

TEST_CASE("each kind gets its own routes and not another kind's",
          "[integration][atmospherics][modulation]") {
    // The defect this whole change came from, stated as an assertion: a vortex used to fall into
    // the comet's `else` arm. If it ever does again, its target set is a comet's.
    std::vector<std::vector<std::string>> leafSets;
    for (const world::EffectKind kind : conf::kEffectKinds) {
        const std::string name = "Probe";
        app::Engine engine(app::EngineMode::Offline);
        engine.newComposition();
        REQUIRE(engine.setAtmosphericEffects({conf::probeEffect(kind, name)}).has_value());
        REQUIRE(engine.addDefaultAtmosphericRoutes(name) > 0);

        const std::string prefix = world::effectParameterPrefix(name);
        std::vector<std::string> leaves;
        for (const std::string& t : targetsUnder(engine, prefix)) {
            leaves.push_back(t.substr(prefix.size()));
        }
        INFO("kind: " << world::effectKindName(kind));
        CHECK_FALSE(leaves.empty());
        leafSets.push_back(std::move(leaves));
    }
    // Pairwise distinct: no two kinds are handed the same set of leaves.
    for (std::size_t i = 0; i < leafSets.size(); ++i) {
        for (std::size_t j = i + 1; j < leafSets.size(); ++j) {
            INFO(world::effectKindName(conf::kEffectKinds[i])
                 << " vs " << world::effectKindName(conf::kEffectKinds[j]));
            CHECK(leafSets[i] != leafSets[j]);
        }
    }
}

TEST_CASE("a default route survives the project the render loads",
          "[integration][atmospherics][modulation][project]") {
    // ADR-350's prescribed round trip, and ADR-264's boundary: a route the session holds and the
    // document does not is absent from every frame anybody exports. A route attached by a button
    // is worth nothing if the save drops it.
    const fs::path dir = scratch("roundtrip");
    std::vector<std::string> before;
    const std::string name = "Sky";
    const std::string prefix = world::effectParameterPrefix(name);

    {
        app::Engine session(app::EngineMode::Offline);
        session.newComposition();
        REQUIRE(session.setAtmosphericEffects({world::glowmereAurora(name)}).has_value());
        REQUIRE(session.addDefaultAtmosphericRoutes(name) > 0);
        before = targetsUnder(session, prefix);
        REQUIRE_FALSE(before.empty());
        REQUIRE(session.saveComposition(dir / "scene.json").has_value());
        REQUIRE(session.saveProject(dir / "project.json").has_value());
    }
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(dir / "project.json").has_value());
        const std::vector<std::string> after = targetsUnder(render, prefix);
        CHECK(after == before);
        for (const std::string& t : after) {
            INFO(t);
            CHECK(render.params().find(t) != nullptr);
        }

        // Save again from the loaded engine and require the routes to still be there -- the second
        // save is the half of ADR-350's recipe that catches a reader with no writer.
        REQUIRE(render.saveProject(dir / "project2.json").has_value());
        app::Engine again(app::EngineMode::Offline);
        REQUIRE(again.loadProject(dir / "project2.json").has_value());
        CHECK(targetsUnder(again, prefix) == before);
    }

    fs::remove_all(dir);
}
