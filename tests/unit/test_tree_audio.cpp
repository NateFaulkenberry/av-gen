#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/tree_audio.hpp"
#include "scene/tree_generator.hpp"
#include "scene/tree_scene.hpp"
#include "scene/tree_veins.hpp"
#include "search/candidate_search.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/format.h>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

struct Harness {
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    TreeParameters tree;
    TreeLook look;

    Harness() {
        // The five bands that exist, plus the events the routes use. Declared here rather than
        // taken from the analyser so the test states which vocabulary it depends on.
        for (const char* name : {"audio.bass", "audio.lowMid", "audio.mid", "audio.highMid", "audio.treble",
                                 "audio.rms", "audio.spectralFlux"}) {
            bus.declare(name);
        }
        bus.declare("audio.onset", 0.0f, 1.0f, true);
        for (const char* name : {"music.beat", "music.build", "music.break", "music.drop"}) {
            bus.declare(name, 0.0f, 1.0f, true);
        }
        tree = registerTreeParameters(params, "tree/hero/", look);
        for (params::ModRoute& r : defaultTreeRoutes("tree/hero/")) {
            modulator.addRoute(std::move(r));
        }
    }

    void frame(double dt = 1.0 / 60.0) {
        params.resetFinals();
        modulator.applyRoutes(bus, params, dt);
    }
    void set(const char* name, float value) {
        const auto id = bus.find(name);
        REQUIRE(id.has_value());
        bus.set(*id, value);
    }
};

} // namespace

TEST_CASE("every default route binds to a real signal and a real parameter", "[tree][audio]") {
    Harness h;
    // An unbound route is the failure mode that makes a scene silently not react, and the engine's
    // own rule is that a diagnostic must never be silent. bind() is what says so.
    const auto ok = h.modulator.bind(h.bus, h.params);
    INFO((ok.has_value() ? std::string{} : ok.error().message));
    REQUIRE(ok.has_value());
    CHECK(h.modulator.routes().size() >= 12);
    for (const params::ModRoute& r : h.modulator.routes()) {
        INFO("route " << r.source << " -> " << r.target);
        CHECK(r.sourceId != signals::kInvalidSignal);
        CHECK(r.targetParam != nullptr);
        // Every route adds to an authored rest value. A `replace` here would mean silence looked
        // different from the authored pose.
        CHECK(r.op == params::ModOp::Add);
    }
}

TEST_CASE("silence is exactly the authored pose", "[tree][audio]") {
    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    for (int i = 0; i < 120; ++i) {
        h.frame();
    }
    // Nothing on the bus, so every final must equal its base. This is the property that makes
    // quality gate 9 -- "the scene looks good with no audio playing" -- structural rather than
    // something to check by eye.
    CHECK_THAT(static_cast<double>(h.tree.windSpeed->value()),
               WithinAbs(static_cast<double>(h.tree.windSpeed->base()), 1e-6));
    CHECK_THAT(static_cast<double>(h.tree.foliageEmissive->value()),
               WithinAbs(static_cast<double>(h.look.foliageEmissiveIntensity), 1e-6));
    CHECK_THAT(static_cast<double>(h.tree.windImpulse->value()), WithinAbs(0.0, 1e-6));
    // And the tree still moves, because the wind's own terms are functions of time and know
    // nothing about whether anything is playing.
    TreeMotionInputs quiet = readTreeMotion(h.tree, 0.0);
    CHECK(quiet.windSpeed > 0.2f);
}

TEST_CASE("bass reaches the wind slowly, and treble reaches it fast", "[tree][audio]") {
    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    // Warm up at silence first. A ProcessorChain initialises its smoothing state from its FIRST
    // sample rather than ramping from zero -- which is the right behaviour, and means a route
    // stepped before it has ever run reports its full amount immediately. Measuring the attack
    // without warming up measures the initialisation instead.
    for (int i = 0; i < 30; ++i) {
        h.frame();
    }
    h.set("audio.bass", 1.0f);
    h.set("audio.treble", 1.0f);

    const float restWind = h.tree.windSpeed->base();
    const float restFlutter = h.tree.windFlutter->base();
    h.frame();
    const float windAfterOne = h.tree.windSpeed->value() - restWind;
    const float flutterAfterOne = h.tree.windFlutter->value() - restFlutter;
    // One frame in, the fast route has moved appreciably further along its journey than the slow
    // one. This is the attack asymmetry doing the work the brief asks of it: the trunk's driver
    // cannot lurch, whatever the music does.
    INFO(fmt::format("after one frame: wind +{:.5f}, flutter +{:.5f}", windAfterOne, flutterAfterOne));
    CHECK(flutterAfterOne > windAfterOne);

    for (int i = 0; i < 600; ++i) {
        h.frame();
    }
    const float windSettled = h.tree.windSpeed->value() - restWind;
    INFO(fmt::format("after ten seconds: wind +{:.4f} on a rest of {:.2f}", windSettled, restWind));
    CHECK(windSettled > 0.05f);
    // The house discipline: a continuous route's full-scale travel stays a modest fraction of its
    // target's rest value, so music enhances the wind rather than replacing it.
    CHECK(windSettled < restWind * 0.55f);
}

TEST_CASE("an onset is a shove, not a lean", "[tree][audio]") {
    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    const auto onset = h.bus.find("audio.onset");
    REQUIRE(onset.has_value());

    h.bus.setEvent(*onset, true, 1.0f);
    h.frame();
    const float peak = h.tree.windImpulse->value();
    h.bus.clearEvents();
    for (int i = 0; i < 60; ++i) {
        h.frame();
    }
    const float after = h.tree.windImpulse->value();
    INFO(fmt::format("impulse peaked at {:.4f}, one second later {:.4f}", peak, after));
    CHECK(peak > 0.02f);
    // It must decay. An onset that leaves a standing offset is an onset that accumulates into a
    // permanent lean over a four-minute piece.
    CHECK(after < peak * 0.35f);
}

TEST_CASE("audio moves the wind, and the wind moves the tree with inertia", "[tree][audio][animation]") {
    // The architectural claim, end to end: the signal never touches a transform. It moves the
    // spring's target, and the spring decides how the joint gets there -- so a step change in the
    // audio produces a ramp in the geometry, not a step.
    const search::Parameters params = search::sampleAt(treeSchema().parameters, kHeroCandidate);
    const auto treeParams = treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    const auto built = buildAnimatedTree(*treeParams);
    REQUIRE(built.has_value());

    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    TreeAnimator animator;
    animator.reset(built->rig);

    const auto meanBend = [&animator] {
        double sum = 0.0;
        for (const glm::vec2& b : animator.bend()) {
            sum += glm::length(b);
        }
        return sum / static_cast<double>(std::max<std::size_t>(animator.bend().size(), 1));
    };

    double time = 0.0;
    for (int i = 0; i < 180; ++i, time += 1.0 / 60.0) {
        h.frame();
        animator.step(built->rig, readTreeMotion(h.tree, time), 1.0f / 60.0f);
    }
    const double quiet = meanBend();

    // A hard step on the bus.
    h.set("audio.bass", 1.0f);
    h.set("audio.lowMid", 1.0f);
    h.frame();
    animator.step(built->rig, readTreeMotion(h.tree, time), 1.0f / 60.0f);
    const double oneFrameLater = meanBend();
    // One frame after the signal jumps to full scale, the geometry has barely moved. That is the
    // inertia, and it is what stops the tree looking like it is dancing.
    INFO(fmt::format("quiet {:.5f}, one frame after a full-scale step {:.5f}", quiet, oneFrameLater));
    CHECK(std::abs(oneFrameLater - quiet) < 0.002);

    for (int i = 0; i < 600; ++i, time += 1.0 / 60.0) {
        h.frame();
        animator.step(built->rig, readTreeMotion(h.tree, time), 1.0f / 60.0f);
    }
    const double loud = meanBend();
    INFO(fmt::format("after ten seconds of full-scale bass: {:.5f}", loud));
    CHECK(loud > quiet);
}

TEST_CASE("the look responds to audio without leaving the palette", "[tree][audio]") {
    const search::Parameters params = search::sampleAt(treeSchema().parameters, kHeroCandidate);
    const auto treeParams = treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    auto built = buildAnimatedTree(*treeParams);
    REQUIRE(built.has_value());

    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    h.frame();
    applyTreeLook(h.tree, h.look, built->scene);
    std::vector<glm::vec3> restColors;
    std::vector<float> restEmissive;
    for (const Entity& e : built->scene.entities) {
        restColors.push_back(e.material.emissiveColor);
        restEmissive.push_back(e.material.emissiveIntensity);
    }

    h.set("audio.rms", 1.0f);
    h.set("audio.mid", 1.0f);
    for (int i = 0; i < 600; ++i) {
        h.frame();
    }
    applyTreeLook(h.tree, h.look, built->scene);

    bool anyBrighter = false;
    for (std::size_t i = 0; i < built->scene.entities.size(); ++i) {
        const Entity& e = built->scene.entities[i];
        if (e.material.emissiveIntensity > restEmissive[i] + 1e-4f) {
            anyBrighter = true;
        }
        // The palette must not wander. A hue drift that can reach an arbitrary colour is a rainbow
        // waiting to happen, and section 24 rules that out explicitly.
        const glm::vec3 delta = e.material.emissiveColor - restColors[i];
        CHECK(glm::length(delta) < 0.35f);
    }
    CHECK(anyBrighter);
}

TEST_CASE("a program that asserts emission owns the whole contract", "[tree][veins]") {
    // The rule from tree_veins.hpp, checked rather than trusted. In this engine a material program
    // REPLACES emission (`matEmissive = vec4(program.emission, 1.0)`), so a part carrying both a
    // program and a material emissive has an authored value nothing will ever read -- which is how
    // an art-direction rule passes review and then quietly does nothing.
    const search::Parameters params = search::sampleAt(treeSchema().parameters, kHeroCandidate);
    const auto treeParams = treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    auto built = buildAnimatedTree(*treeParams);
    REQUIRE(built.has_value());

    const auto ok = verifyEmissionOwnership(built->scene);
    INFO((ok.has_value() ? std::string{} : ok.error().message));
    CHECK(ok.has_value());

    // And the branches really are on the program while the foliage really is not: the split is what
    // keeps phase 9's routes onto `Material::emissiveIntensity` meaningful for the canopy.
    int branchesOnProgram = 0;
    int foliageOffProgram = 0;
    for (const Entity& e : built->scene.entities) {
        if (e.name == "tree.trunk" || e.name == "tree.primary" || e.name == "tree.secondary") {
            CHECK(e.material.program == "tree.veins");
            ++branchesOnProgram;
        }
        if (e.name.rfind("tree.foliage", 0) == 0) {
            CHECK(e.material.program.empty());
            CHECK(e.material.emissiveIntensity > 0.0f);
            ++foliageOffProgram;
        }
    }
    CHECK(branchesOnProgram == 3);
    CHECK(foliageOffProgram == kFoliageTints);
}

TEST_CASE("the check catches the regression it exists for", "[tree][veins]") {
    // A negative test, because a guard nobody has seen fail is a guard nobody knows works.
    Scene scene;
    scene.materialPrograms.push_back(makeVeinProgram("veins", VeinSettings{}));
    MeshData mesh;
    mesh.vertices = {Vertex{{0, 0, 0}, {0, 1, 0}, {0, 0}}, Vertex{{1, 0, 0}, {0, 1, 0}, {1, 0}},
                     Vertex{{0, 0, 1}, {0, 1, 0}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    Entity& e = scene.addEntity("branch", scene.addMesh(std::move(mesh)));
    e.material.program = "veins";
    e.material.emissiveIntensity = 0.3f; // the mistake
    CHECK_FALSE(verifyEmissionOwnership(scene).has_value());

    e.material.emissiveIntensity = 0.0f;
    CHECK(verifyEmissionOwnership(scene).has_value());

    // A program the scene does not carry is also a failure, and for the same reason: it resolves to
    // nothing and the surface silently keeps its authored material.
    e.material.program = "nope";
    CHECK_FALSE(verifyEmissionOwnership(scene).has_value());
}

TEST_CASE("audio reaches the vein program, not a dead material lane", "[tree][veins][audio]") {
    const search::Parameters params = search::sampleAt(treeSchema().parameters, kHeroCandidate);
    const auto treeParams = treeParamsFrom(params);
    REQUIRE(treeParams.has_value());
    auto built = buildAnimatedTree(*treeParams);
    REQUIRE(built.has_value());
    REQUIRE_FALSE(built->scene.materialPrograms.empty());

    Harness h;
    REQUIRE(h.modulator.bind(h.bus, h.params).has_value());
    for (int i = 0; i < 30; ++i) {
        h.frame();
    }
    applyTreeLook(h.tree, h.look, built->scene);
    const float rest = built->scene.materialPrograms[0].emissionIntensity;
    CHECK(rest > 0.0f);

    const auto beat = h.bus.find("music.beat");
    REQUIRE(beat.has_value());
    h.bus.setEvent(*beat, true, 1.0f);
    h.frame();
    applyTreeLook(h.tree, h.look, built->scene);
    const float pulsed = built->scene.materialPrograms[0].emissionIntensity;
    INFO(fmt::format("vein gain rest {:.4f}, on a beat {:.4f}", rest, pulsed));
    CHECK(pulsed > rest);
    // And the branch materials stayed at zero throughout: the route moved the program, not a lane
    // the shader discards.
    for (const Entity& e : built->scene.entities) {
        if (!e.material.program.empty()) {
            CHECK(e.material.emissiveIntensity == 0.0f);
        }
    }
}
