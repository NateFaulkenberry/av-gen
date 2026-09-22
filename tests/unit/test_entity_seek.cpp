// What a timeline click replays, and whether it still lands on the frame a play would have drawn
// (ADR-273).
//
// ADR-267 established the thing these tests exist to protect: the entity replay already matches a
// play, to 0.000022 m over eight explorers at thirty seconds, and nobody had ever compared the two.
// So every arm here that says a seek is *cheaper* has an arm beside it saying the answer did not
// move, and every arm that says the answer did not move has a control that must say it did --
// because a seek that lands somewhere else is not a faster seek, it is a wrong one (ADR-182).
//
// The navigating case -- explore, a real grid, a real obstacle field -- is measured by
// tools/charai_probe.cpp's determinism section rather than here: it costs seconds per arm, and a
// unit suite that takes seconds per arm is a unit suite nobody runs.

#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr double kStep = 1.0 / 60.0;

params::ParamDesc<glm::vec3> v3(std::string path, glm::vec3 def, float lo, float hi) {
    return params::ParamDesc<glm::vec3>{
        .path = std::move(path), .defaultValue = def, .hardMin = glm::vec3(lo), .hardMax = glm::vec3(hi)};
}

entity::BehaviorDesc behavior(const char* kind, nlohmann::json settings = nlohmann::json::object()) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

// A world of `count` bodies, each carrying the same behaviour list, each on its own node.
struct World {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;

    World(int count, const std::vector<entity::BehaviorDesc>& behaviors,
          const std::vector<entity::ActionDesc>& actions = {}) {
        std::vector<entity::EntityDesc> descs;
        std::vector<entity::NodeBinding> bindings;
        for (int i = 0; i < count; ++i) {
            const std::string node = "body" + std::to_string(i);
            params.add(v3("nodes/" + node + "/position", glm::vec3(0.0f), -1e4f, 1e4f));
            params.add(v3("nodes/" + node + "/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
            params.add(v3("nodes/" + node + "/scale", glm::vec3(1.0f), 0.001f, 100.0f));
            entity::EntityDesc d;
            d.name = node;
            d.node = node;
            d.seed = static_cast<std::uint32_t>(1000 + i);
            d.behaviors = behaviors;
            d.actions = actions;
            descs.push_back(std::move(d));
            entity::NodeBinding b;
            b.node = node;
            b.exists = true;
            b.transformPrefix = "nodes/" + node + "/";
            b.anchor = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 0.0f);
            bindings.push_back(std::move(b));
        }
        world.setEntities(std::move(descs), 7u);
        world.setBindings(bindings);
        world.registerParameters(params);
        world.bind(params);
    }

    // The sequence of instants a play from zero actually produces: `step`, `2*step`, ... up to and
    // including `seconds`. Not a frame at t = 0 -- there is no dt into it, and a seek has no such
    // step either, so including one here would compare two different sequences.
    void playTo(double seconds, double step = kStep) {
        const auto frames = static_cast<int>(seconds / step);
        for (int i = 1; i <= frames; ++i) {
            params.resetFinals();
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) * step;
            u.dt = step;
            u.frameIndex = static_cast<std::uint64_t>(i);
            u.bus = &bus;
            u.distanceDetail = false; // behaviour LOD is a function of the camera; this is not
            world.update(u, params);
        }
    }

    void seekTo(double seconds, entity::SeekBudget budget = {}, double step = kStep) {
        world.seek(seconds, &params, &bus, step, budget);
    }

    // The frame *after*, which is the only thing anybody sees.
    //
    // A seek deliberately writes nothing to the parameter set: the behaviours' offsets -- hover's
    // rise, bank's lean, spin's angle -- are folded onto the node transform by the next ordinary
    // update, out of state the seek left behind. So comparing positions straight off the entity
    // compares the half of the answer navigation writes and none of the half everything else does,
    // and an arm built on it cannot fail for any behaviour that only produces an offset. Running one
    // ordinary frame on top and reading the node transforms asks the question in the terms the
    // renderer answers it in.
    [[nodiscard]] std::vector<glm::vec3> drawnAfter(double target) {
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = target + kStep;
        u.dt = kStep;
        u.bus = &bus;
        u.distanceDetail = false;
        world.update(u, params);
        std::vector<glm::vec3> out;
        for (const auto& e : world.entities()) {
            const std::string prefix = "nodes/" + e->desc().node + "/";
            out.push_back(params.findAs<glm::vec3>(prefix + "position")->value());
            out.push_back(params.findAs<glm::vec3>(prefix + "rotation")->value());
        }
        return out;
    }
};

double worst(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    REQUIRE(a.size() == b.size());
    double d = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        d = std::max(d, static_cast<double>(glm::length(a[i] - b[i])));
    }
    return d;
}

// The craft profile: everything in the vocabulary that moves without a navigator. `bank` and `spin`
// are accumulations and `drift` differences against the previous step, so this is deliberately not
// a population of the easy case.
std::vector<entity::BehaviorDesc> craft() {
    return {behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}}),
            behavior("drift", {{"radius", 2.0}, {"rate", 0.2}}),
            behavior("bank", {{"degrees", 8.0}}),
            behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}, {"damping", 1.5}})};
}

// ADR-700 retired the window as the product; these arms are about the window's own classification
// and budget, so they ask for it by name -- it is kept as a control, not as dead code.
const entity::SeekBudget kWindow{.mode = entity::SeekMode::Window};

} // namespace

TEST_CASE("a scrub lands where the play landed", "[entity][seek][determinism]") {
    // The claim, and the one ADR-267 measured at 0.000022 m before the replay's final step was
    // moved onto the target itself. It is now zero, which is a stronger statement and a more
    // fragile one, so the controls below matter more than the claim does.
    World played(4, craft());
    played.playTo(20.0);
    World scrubbed(4, craft());
    scrubbed.seekTo(20.0);
    const std::vector<glm::vec3> reference = scrubbed.drawnAfter(20.0);
    CHECK(worst(played.drawnAfter(20.0), reference) == 0.0);

    SECTION("and it is the same scrub every time") {
        World again(4, craft());
        again.seekTo(20.0);
        CHECK(worst(again.drawnAfter(20.0), reference) == 0.0);
    }

    SECTION("control: a play at a different rate must NOT agree") {
        // If this reads 0 the comparison above is comparing something that does not integrate, and
        // the claim is worth nothing.
        World slow(4, craft());
        slow.playTo(20.0, 1.0 / 30.0);
        CHECK(worst(slow.drawnAfter(20.0), reference) > 1e-4);
    }

    SECTION("control: a different second must NOT agree") {
        World elsewhere(4, craft());
        elsewhere.seekTo(19.0);
        CHECK(worst(elsewhere.drawnAfter(19.0), reference) > 1e-4);
    }
}

TEST_CASE("a behaviour that is a function of the clock is not replayed", "[entity][seek]") {
    // `hover` is `slowNoise(t)` and nothing else, so 5,399 of a ninety-second scrub's 5,400 steps
    // produce a number the last one overwrites. The saving and the proof that it is free are the
    // two halves of this test and neither means anything alone.
    World hovering(1, {behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}})});
    hovering.seekTo(60.0, kWindow);
    const entity::EntityWorld::SeekWork work = hovering.world.lastSeekWork();
    CHECK(work.steps == 3600);
    CHECK(work.fullBodySteps == 3600);
    CHECK(work.shallowBodies == 1);
    CHECK(work.deepBodies == 0);
    CHECK(work.bodySteps == 1); // one step, not three thousand six hundred

    World hoveringPlayed(1, {behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}})});
    hoveringPlayed.playTo(60.0);
    CHECK(worst(hovering.drawnAfter(60.0), hoveringPlayed.drawnAfter(60.0)) == 0.0);

    SECTION("drift needs the step before, and takes exactly that") {
        World drifting(1, {behavior("drift", {{"radius", 2.0}, {"rate", 0.2}})});
        drifting.seekTo(60.0, kWindow);
        CHECK(drifting.world.lastSeekWork().bodySteps == 2);
        // ...and the speed it publishes for `bank`, which is the whole reason it is two and not
        // one. Read before the extra frame, because that frame overwrites it.
        CHECK(drifting.world.entities().front()->state().speed > 0.0f);
        World driftingPlayed(1, {behavior("drift", {{"radius", 2.0}, {"rate", 0.2}})});
        driftingPlayed.playTo(60.0);
        CHECK(drifting.world.entities().front()->state().speed ==
              driftingPlayed.world.entities().front()->state().speed);
        CHECK(worst(drifting.drawnAfter(60.0), driftingPlayed.drawnAfter(60.0)) == 0.0);
    }

    SECTION("control: an accumulating behaviour is replayed in full, and has to be") {
        World spinning(1, {behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}, {"damping", 1.5}})});
        spinning.seekTo(60.0, kWindow);
        CHECK(spinning.world.lastSeekWork().deepBodies == 1);
        CHECK(spinning.world.lastSeekWork().bodySteps == 3600);
        // And the shortcut would be wrong for it: one step of window is a different frame, which is
        // the reason `spin` is not allowed the shortcut `hover` gets.
        World clipped(1, {behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}, {"damping", 1.5}})});
        clipped.seekTo(60.0, entity::SeekBudget{.maxSeconds = kStep, .mode = entity::SeekMode::Window});
        CHECK(worst(clipped.drawnAfter(60.0), spinning.drawnAfter(60.0)) > 1e-4);
    }

    SECTION("control: an entity under orders is replayed in full whatever its behaviours say") {
        entity::ActionDesc wait;
        wait.kind = entity::ActionKind::Wait;
        wait.duration = 4.0;
        World ordered(1, {behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}})}, {wait});
        ordered.seekTo(60.0, kWindow);
        CHECK(ordered.world.lastSeekWork().deepBodies == 1);
        CHECK(ordered.world.lastSeekWork().bodySteps == 3600);
    }
}

TEST_CASE("the replay window is a budget in body-steps", "[entity][seek]") {
    // The ninety-second literal it replaces was in the wrong currency: it bought 8,100 body-steps in
    // a small scene and 1,350,000 in the cast the character-AI plan wants, which is the same click
    // costing 161 s instead of 1 s (ADR-267). A budget in the unit the cost is paid in is what makes
    // a bigger scene keep less history rather than take longer.
    const entity::SeekBudget capped{.maxSeconds = 90.0, .maxBodySteps = 1200, .mode = entity::SeekMode::Window};
    World ten(10, {behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}})});
    ten.seekTo(60.0, capped);
    CHECK(ten.world.lastSeekWork().steps == 120); // 1200 / 10
    CHECK(ten.world.lastSeekWork().budgetBound);

    SECTION("twice the cast, half the history, the same work") {
        World twenty(20, {behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}})});
        twenty.seekTo(60.0, capped);
        CHECK(twenty.world.lastSeekWork().steps == 60);
        CHECK(twenty.world.lastSeekWork().bodySteps == ten.world.lastSeekWork().bodySteps);
    }

    SECTION("shallow bodies are not charged against it") {
        // Ten accumulating bodies and ten that are functions of the clock: the budget is spent on
        // the ten that need it, and the other ten do not shorten anybody's history.
        std::vector<entity::BehaviorDesc> mixed = {behavior("hover", {{"amplitude", 1.0}})};
        World hovers(10, mixed);
        hovers.seekTo(60.0, capped);
        CHECK(hovers.world.lastSeekWork().steps == 3600); // nothing deep: no ceiling to hit
        CHECK_FALSE(hovers.world.lastSeekWork().budgetBound);
    }

    SECTION("control: no ceiling is no ceiling") {
        World uncapped(10, {behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}})});
        uncapped.seekTo(60.0, entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = 0, .mode = entity::SeekMode::Window});
        CHECK(uncapped.world.lastSeekWork().steps == 3600);
        CHECK_FALSE(uncapped.world.lastSeekWork().budgetBound);
    }
}

TEST_CASE("a scrub replays the action tier", "[entity][seek][determinism]") {
    // ADR-091's Cinematic Action tier was the one tier a scrub could not reproduce: `reset()` put
    // the authored list back and then nothing integrated it, so a character eight seconds into a
    // routine scrubbed to a character that had not started it.
    const auto routine = [] {
        std::vector<entity::ActionDesc> out;
        for (int i = 0; i < 4; ++i) {
            entity::ActionDesc a;
            a.kind = entity::ActionKind::Wait;
            a.name = "wait" + std::to_string(i);
            a.duration = 2.0;
            out.push_back(a);
        }
        return out;
    }();
    World played(1, {}, routine);
    played.playTo(5.0);
    const std::size_t afterPlay = played.world.entities().front()->actions().pending();

    World scrubbed(1, {}, routine);
    scrubbed.seekTo(5.0);
    CHECK(scrubbed.world.entities().front()->actions().pending() == afterPlay);
    // Two waits of two seconds have finished and the third is running: 2 of the 4 are left.
    CHECK(afterPlay == 2);

    SECTION("control: a scrub to zero must NOT have started it") {
        World atZero(1, {}, routine);
        atZero.seekTo(0.0);
        CHECK(atZero.world.entities().front()->actions().pending() == routine.size());
    }
}
