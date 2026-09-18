// The deliverable is rendered from a project *document* (ADR-264: a scene file is not the state
// that runs). `Application::startRenderFromUi` saves the session to a project and hands that file
// to a second, offline `Engine` -- so anything the session holds and the document does not is
// absent from every frame anybody exports, and present in the window the whole time.
//
// ADR-207's world effects and ADR-230's atmospheric effects are held by the `Composition`, and a
// project whose scene came from a file saves that scene *by reference*. An effect added through the
// World Effects panel therefore reached the composition the window draws and no document the render
// reads. These tests are about that one boundary: the save the render loads through.
//
// Every case has a control arm that must pass before and after the fix, because a probe that cannot
// see an effect that IS there proves nothing about one that is not (ADR-182).

#include "app/engine.hpp"
#include "world/atmospherics.hpp"
#include "world/effects.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() /
                         ("avgen_project_worldfx_" + std::to_string(getpid()) + "_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

world::AtmosphericEffect aurora(std::string name) {
    world::AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = world::AtmosphereKind::Aurora;
    e.activation = world::Activation::Always;
    return e;
}

world::WorldEffect pulse(std::string name) {
    world::WorldEffect e;
    e.name = std::move(name);
    e.source.kind = world::SourceKind::World;
    e.propagation.kind = world::PropagationKind::RadialWave;
    e.activation = world::Activation::Always;
    return e;
}

std::vector<std::string> namesOf(const std::vector<world::AtmosphericEffect>& effects) {
    std::vector<std::string> names;
    for (const auto& e : effects) names.push_back(e.name);
    return names;
}

std::vector<std::string> namesOf(const std::vector<world::WorldEffect>& effects) {
    std::vector<std::string> names;
    for (const auto& e : effects) names.push_back(e.name);
    return names;
}

} // namespace

// The whole reproduction, in the shape the panel makes it: a scene file authors one effect, the
// session adds a second, the project is saved, and the render's engine loads that project.
TEST_CASE("An atmospheric effect added in the session reaches the project the render loads",
          "[integration][project][atmospherics]") {
    const fs::path dir = scratch("atmos");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.setAtmosphericEffects({aurora("Authored In The Scene")}).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    // The control. Nothing was edited after the scene was written, so the reference in the project
    // is enough and the reloaded engine must find the effect. This arm passes before the fix and
    // after it; without it, an arm that finds nothing proves nothing.
    REQUIRE(session.saveProject(dir / "untouched.json").has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(dir / "untouched.json").has_value());
        CHECK(namesOf(render.atmosphericEffects()) ==
              std::vector<std::string>{"Authored In The Scene"});
    }

    // The arm. `Engine::setAtmosphericEffects` is the exact call the World Effects panel makes when
    // somebody adds an aurora, and the scene file is not written again -- as it is not when somebody
    // presses Render.
    REQUIRE(session
                .setAtmosphericEffects({aurora("Authored In The Scene"), aurora("Added In Session")})
                .has_value());
    REQUIRE(session.saveProject(dir / "edited.json").has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(dir / "edited.json").has_value());
        CHECK(namesOf(render.atmosphericEffects()) ==
              std::vector<std::string>{"Authored In The Scene", "Added In Session"});
        // And the parameters that make every number on it automatable exist, which is the other
        // half of the same failure: the project already carried `atmos/<name>/...` values, and
        // before the fix they were applied to nothing because the effect that owns them was gone.
        CHECK(render.params().find("atmos/Added In Session/intensity") != nullptr);
    }
}

// ADR-207's family, on the same terms. The two lists are separate all the way down and are lost by
// the identical mechanism, which is why the fix is one place and the test is two cases.
TEST_CASE("A world effect added in the session reaches the project the render loads",
          "[integration][project][worldfx]") {
    const fs::path dir = scratch("worldfx");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.setWorldEffects({pulse("Authored In The Scene")}).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    REQUIRE(session.saveProject(dir / "untouched.json").has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(dir / "untouched.json").has_value());
        CHECK(namesOf(render.worldEffects()) == std::vector<std::string>{"Authored In The Scene"});
    }

    REQUIRE(session.setWorldEffects({pulse("Authored In The Scene"), pulse("Added In Session")})
                .has_value());
    REQUIRE(session.saveProject(dir / "edited.json").has_value());
    {
        app::Engine render(app::EngineMode::Offline);
        REQUIRE(render.loadProject(dir / "edited.json").has_value());
        CHECK(namesOf(render.worldEffects()) ==
              std::vector<std::string>{"Authored In The Scene", "Added In Session"});
        CHECK(render.params().find("worldfx/Added In Session/intensity") != nullptr);
    }
}

// A deletion is an edit too, and the direction that a "write it only when there is something to
// say" rule gets wrong: an empty list *is* something to say when the file it overrides is not.
TEST_CASE("An effect deleted in the session stays deleted in the project the render loads",
          "[integration][project][worldfx]") {
    const fs::path dir = scratch("deleted");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.setAtmosphericEffects({aurora("Doomed")}).has_value());
    REQUIRE(session.setWorldEffects({pulse("Doomed Too")}).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());
    REQUIRE(session.setAtmosphericEffects({}).has_value());
    REQUIRE(session.setWorldEffects({}).has_value());
    REQUIRE(session.saveProject(dir / "emptied.json").has_value());

    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(dir / "emptied.json").has_value());
    CHECK(render.atmosphericEffects().empty());
    CHECK(render.worldEffects().empty());
}

// The rule the mirror is written under: a project that only ever opened a scene and rendered it
// keeps the file it had. A float that went through the engine and a number somebody typed are the
// same authored value and must not read as an edit -- so the comparison normalises both sides.
TEST_CASE("A project that edited no effect writes no copy of them",
          "[integration][project][worldfx]") {
    const fs::path dir = scratch("stable");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.setAtmosphericEffects({aurora("Authored")}).has_value());
    REQUIRE(session.setWorldEffects({pulse("Authored")}).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    app::Engine reopened(app::EngineMode::Offline);
    REQUIRE(reopened.loadComposition(dir / "scene.json").has_value());
    REQUIRE(reopened.saveProject(dir / "reopened.json").has_value());
    std::ifstream in(dir / "reopened.json");
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    REQUIRE(doc.is_object());
    CHECK_FALSE(doc.contains("atmosphericEffects"));
    CHECK_FALSE(doc.contains("worldEffects"));
}

// The second consequence of the same defect, and the one that is data loss rather than a missing
// picture: a value whose parameter is unknown at load is refused, so it is also absent from the
// parameter set at save. A project saved from a session that added an aurora carried 47 of that
// aurora's numbers and nothing that owned them -- so the next load warned 47 times and the next
// save wrote the file back 47 values shorter. Two round trips and the settings were gone.
//
// Counted rather than sampled, because "the aurora looks the same" is exactly the observation this
// family of defect survives.
TEST_CASE("A project round trip loses no parameter of an effect the session added",
          "[integration][project][atmospherics]") {
    const fs::path dir = scratch("noloss");
    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());
    REQUIRE(session.setAtmosphericEffects({aurora("Valley Aurora")}).has_value());
    REQUIRE(session.saveProject(dir / "once.json").has_value());

    const auto parametersOf = [](const fs::path& file) {
        std::ifstream in(file);
        const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        REQUIRE(doc.is_object());
        REQUIRE(doc.contains("parameters"));
        std::vector<std::string> paths;
        for (const auto& [path, value] : doc["parameters"].items()) {
            paths.push_back(path);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    };

    const std::vector<std::string> once = parametersOf(dir / "once.json");
    // The control: the aurora's own numbers are in the file at all. An assertion that nothing was
    // lost is vacuously true over an empty set.
    const auto atmosCount = [](const std::vector<std::string>& paths) {
        return std::count_if(paths.begin(), paths.end(),
                             [](const std::string& p) { return p.starts_with("atmos/"); });
    };
    CHECK(atmosCount(once) > 20);

    app::Engine reopened(app::EngineMode::Offline);
    REQUIRE(reopened.loadProject(dir / "once.json").has_value());
    REQUIRE(reopened.saveProject(dir / "twice.json").has_value());
    const std::vector<std::string> twice = parametersOf(dir / "twice.json");

    std::vector<std::string> lost;
    std::set_difference(once.begin(), once.end(), twice.begin(), twice.end(), std::back_inserter(lost));
    INFO("lost " << lost.size() << " parameter(s), first: " << (lost.empty() ? std::string{} : lost.front()));
    CHECK(lost.empty());
    CHECK(atmosCount(twice) == atmosCount(once));
}
