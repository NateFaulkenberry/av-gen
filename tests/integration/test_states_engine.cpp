// Scene states and world macros (ADR-031): states morph presets with easing, triggers fire from
// signals/beats/macros, OSC reaches states, world macros expand to ordinary routes, and both
// round-trip through projects.

#include "app/engine.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {
void makeStates(app::Engine& engine) {
    auto* scale = engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    scale->setBaseComponent(0, 1.0f);
    engine.storePreset("dormant");
    scale->setBaseComponent(0, 3.0f);
    engine.storePreset("awake");
    scale->setBaseComponent(0, 1.0f);
    app::SceneState dormant;
    dormant.name = "Dormant";
    dormant.preset = "dormant";
    app::SceneState awake;
    awake.name = "Awakening";
    awake.preset = "awake";
    awake.transition.seconds = 1.0;
    awake.transition.easing = app::TransitionEasing::Linear;
    app::StateTrigger trig;
    trig.kind = app::TriggerKind::Signal;
    trig.signal = "control.go";
    trig.threshold = 0.5f;
    awake.triggers.push_back(trig);
    engine.states().states = {dormant, awake};
    engine.states().initial = "Dormant";
    engine.states().reset(engine.params(), engine.presets());
}
} // namespace

TEST_CASE("States morph presets over time with easing and expose progress signals", "[integration][states]") {
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    makeStates(engine);
    CHECK(engine.states().current() == "Dormant");
    auto* scale = engine.params().find("orb/scale");
    CHECK_THAT(scale->baseComponent(0), WithinAbs(1.0, 1e-5));

    REQUIRE(engine.goToState("Awakening"));
    CHECK(engine.states().transitioning());
    for (int i = 0; i < 30; ++i) { // half a second at 60 fps
        engine.update(engine.tick(clock));
    }
    const float mid = scale->baseComponent(0);
    CHECK(mid > 1.5f);
    CHECK(mid < 2.5f);
    CHECK(engine.states().progress() > 0.3f);
    CHECK(engine.states().progress() < 0.7f);
    const auto progressId = engine.signals().find("state.progress");
    REQUIRE(progressId.has_value());
    CHECK_THAT(static_cast<double>(engine.signals().value(*progressId)), WithinAbs(static_cast<double>(engine.states().progress()), 1e-5));
    for (int i = 0; i < 40; ++i) {
        engine.update(engine.tick(clock));
    }
    CHECK_FALSE(engine.states().transitioning());
    CHECK(engine.states().current() == "Awakening");
    CHECK_THAT(scale->baseComponent(0), WithinAbs(3.0, 1e-5));
    CHECK(engine.states().currentIndex() == 1);
}

TEST_CASE("Signal triggers and OSC start state transitions", "[integration][states]") {
    app::Engine engine(app::EngineMode::Offline);
    control::ControlMap map;
    map.oscEnabled = false;
    map.midiEnabled = false;
    engine.control().setMap(map);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    makeStates(engine);
    // A rising crossing of control.go fires the Awakening trigger.
    engine.controlSource().set("go", 1.0f);
    engine.update(engine.tick(clock));
    engine.update(engine.tick(clock));
    CHECK((engine.states().transitioning() || engine.states().current() == "Awakening"));
    CHECK(engine.states().pending() == "Awakening");
    // Back to Dormant instantly through OSC.
    control::OscMessage go;
    go.address = "/avgen/state/go";
    go.args = {std::string("Dormant"), 1.0f};
    engine.control().injectOsc(go);
    engine.update(engine.tick(clock));
    CHECK(engine.states().current() == "Dormant");
    CHECK_FALSE(engine.states().transitioning());
    control::OscMessage bad;
    bad.address = "/avgen/state/Nope";
    engine.control().injectOsc(bad);
    engine.update(engine.tick(clock));
    CHECK(engine.states().current() == "Dormant");
}

TEST_CASE("World macros expand to routes and round-trip with states through projects", "[integration][states]") {
    const auto dir = fs::temp_directory_path() / "avgen_states_project";
    fs::create_directories(dir);
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    makeStates(engine);
    app::WorldMacro energy;
    energy.name = "energy";
    energy.label = "WORLD ENERGY";
    energy.defaultValue = 0.0f;
    energy.targets.push_back({.path = "orb/scale", .min = 0.0f, .max = 2.0f});
    energy.targets.push_back({.path = "orb/emissive", .min = 0.0f, .max = 4.0f, .curve = params::CurveType::Power, .curveAmount = 2.0f});
    engine.setWorldMacro(energy);
    REQUIRE(engine.params().find("macros/energy") != nullptr);
    std::size_t generated = 0;
    for (const auto& r : engine.modulator().routes()) {
        generated += app::WorldMacro::isGenerated(r, "energy") ? 1 : 0;
    }
    CHECK(generated == 2);
    // Turning the knob adds min..max to the base through the ordinary modulation path.
    engine.params().find("macros/energy")->setBaseComponent(0, 1.0f);
    engine.update(engine.tick(clock));
    engine.update(engine.tick(clock));
    CHECK_THAT(engine.params().find("orb/scale")->finalComponent(0), WithinAbs(1.0 + 2.0, 1e-3));
    // Re-applying does not duplicate routes; removing drops them.
    engine.setWorldMacro(energy);
    generated = 0;
    for (const auto& r : engine.modulator().routes()) {
        generated += app::WorldMacro::isGenerated(r, "energy") ? 1 : 0;
    }
    CHECK(generated == 2);

    const auto project = dir / "states.json";
    REQUIRE(engine.saveProject(project).has_value());
    app::Engine other(app::EngineMode::Offline);
    REQUIRE(other.loadProject(project).has_value());
    REQUIRE(other.states().states.size() == 2);
    CHECK(other.states().initial == "Dormant");
    CHECK(other.states().states[1].triggers.size() == 1);
    CHECK(other.states().states[1].triggers[0].signal == "control.go");
    CHECK(other.states().current() == "Dormant");
    REQUIRE(other.worldMacros().size() == 1);
    CHECK(other.worldMacros()[0].label == "WORLD ENERGY");
    CHECK(other.worldMacros()[0].targets[1].curve == params::CurveType::Power);
    generated = 0;
    for (const auto& r : other.modulator().routes()) {
        generated += app::WorldMacro::isGenerated(r, "energy") ? 1 : 0;
    }
    CHECK(generated == 2);
    CHECK(other.removeWorldMacro("energy"));
    generated = 0;
    for (const auto& r : other.modulator().routes()) {
        generated += app::WorldMacro::isGenerated(r, "energy") ? 1 : 0;
    }
    CHECK(generated == 0);
    fs::remove_all(dir);
}

TEST_CASE("A director installs artistic knobs and a look changes only the look", "[integration][director]") {
    const auto dir = fs::temp_directory_path() / "avgen_director_engine";
    fs::create_directories(dir);
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));

    app::WorldDirector director;
    director.name = "orb";
    app::DirectorMapping drama;
    drama.knob = app::DirectorKnob::Drama;
    drama.defaultValue = 0.25f;
    drama.targets.push_back({.path = "orb/scale", .min = 1.0f, .max = 3.0f});
    app::DirectorMapping glow;
    glow.knob = app::DirectorKnob::Glow;
    glow.defaultValue = 0.5f;
    glow.targets.push_back({.path = "orb/emissive", .min = 0.0f, .max = 4.0f});
    director.mappings = {drama, glow};
    engine.setDirector(director);

    // The knobs are ordinary macros, so they exist as parameters and as routes.
    REQUIRE(engine.params().find("macros/drama") != nullptr);
    REQUIRE(engine.params().find("macros/glow") != nullptr);
    std::size_t generated = 0;
    for (const auto& r : engine.modulator().routes()) {
        generated += (app::WorldMacro::isGenerated(r, "drama") || app::WorldMacro::isGenerated(r, "glow")) ? 1 : 0;
    }
    CHECK(generated == 2);

    engine.params().find("macros/drama")->setBaseComponent(0, 1.0f);
    engine.update(engine.tick(clock));
    engine.update(engine.tick(clock));
    CHECK(engine.params().find("orb/scale")->finalComponent(0) > 2.5f);

    // Replacing the director removes the knobs it owned.
    app::WorldDirector other;
    other.name = "other";
    app::DirectorMapping warmth;
    warmth.knob = app::DirectorKnob::Warmth;
    warmth.targets.push_back({.path = "orb/emissive", .min = 0.0f, .max = 1.0f});
    other.mappings = {warmth};
    engine.setDirector(other);
    CHECK(engine.params().find("macros/warmth") != nullptr);
    CHECK(engine.params().find("macros/drama") == nullptr);

    // Projects carry the director.
    const auto project = dir / "directed.json";
    REQUIRE(engine.saveProject(project).has_value());
    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(project).has_value());
    CHECK(loaded.director().mappings.size() == 1);
    CHECK(loaded.director().mappings[0].knob == app::DirectorKnob::Warmth);
    CHECK(loaded.params().find("macros/warmth") != nullptr);

    // A look applies visual values only and reports what a world lacks.
    app::LookPreset look;
    look.name = "Test";
    look.preset.name = "Test";
    look.preset.values["post/bloom/intensity"] = {0.8f};
    look.preset.values["scene/nonexistent"] = {1.0f};
    engine.setLooks({look});
    const app::LookApplyResult result = engine.applyLookByName("Test");
    CHECK(result.applied >= 1);
    CHECK(result.missing >= 1);
    CHECK(engine.applyLookByName("nope").applied == 0);
    fs::remove_all(dir);
}
