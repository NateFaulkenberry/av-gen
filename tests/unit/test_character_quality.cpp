// The character quality analyzer (ADR-826): per-entity motion and behaviour metrics over a run.
//
// Every arm here has a control beside it that must read the opposite, because a meter that reads
// zero pops on a clean run proves nothing until it has been seen to read one on a dirty run -- and
// the converse (ADR-182, the testing doc's "a green suite has lied" list). The arms:
//
//   wander       a real `EntityWorld` with a wandering body: it travels, and it does not pop
//   pop          the same world with a body the DIRECTOR teleports for one step: one pop, of the
//                distance it was thrown; and the same run without the throw reads none
//   arithmetic   hand-made samples through the plain-struct door: stuck time, slip, churn,
//                oscillation, director and airborne seconds, each against a control
//   schema       the JSON carries every documented field per entity and no aggregate score

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <span>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

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

// One body on one node, built the way `test_entity_seek.cpp` builds its worlds: no GPU, no
// composition, just the parameters an entity binds its transform to.
struct OneBody {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    entity::CharacterQualityRecorder recorder;

    explicit OneBody(std::vector<entity::BehaviorDesc> behaviors) {
        params.add(v3("nodes/body/position", glm::vec3(0.0f), -1e4f, 1e4f));
        params.add(v3("nodes/body/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
        params.add(v3("nodes/body/scale", glm::vec3(1.0f), 0.001f, 100.0f));
        entity::EntityDesc d;
        d.name = "body";
        d.node = "body";
        d.seed = 4242;
        d.cullDistance = 0.0f; // the LOD band must not decide a quality test
        d.behaviors = std::move(behaviors);
        world.setEntities({std::move(d)}, 7u);
        entity::NodeBinding b;
        b.node = "body";
        b.exists = true;
        b.transformPrefix = "nodes/body/";
        world.setBindings({b});
        world.registerParameters(params);
        world.bind(params);
    }

    entity::Entity& body() { return *world.entities().front(); }

    // The application's convention: frame 0 at t = 0 with a zero delta, then full steps.
    void step(int frame) {
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(frame) * kStep;
        u.dt = frame == 0 ? 0.0 : kStep;
        u.frameIndex = static_cast<std::uint64_t>(frame);
        u.bus = &bus;
        u.distanceDetail = false;
        world.update(u, params);
        recorder.record(world, u.time, u.dt);
    }
};

const entity::CharacterQuality& only(const entity::CharacterQualityReport& r) {
    REQUIRE(r.characters.size() == 1);
    return r.characters.front();
}

// Runs a director-held body for 120 frames, optionally throwing it `throwMetres` along +X for one
// step at frame 60 and then letting go. Travel persists after a director lets go (a director says
// where a body IS, and `travel` keeps it), so the throw is exactly one discontinuous step.
entity::CharacterQualityReport directorRun(float throwMetres) {
    OneBody w({});
    for (int i = 0; i < 120; ++i) {
        if (i == 60 && throwMetres > 0.0f) {
            entity::DirectorMotion m;
            m.active = true;
            m.position = w.body().state().position() + glm::vec3(throwMetres, 0.0f, 0.0f);
            w.body().setDirectorMotion(m);
        } else if (i == 61) {
            w.body().setDirectorMotion(entity::DirectorMotion{});
        }
        w.step(i);
    }
    return w.recorder.report();
}

entity::CharacterSample sample(const char* name, glm::vec3 at, float intent, entity::Activity a) {
    entity::CharacterSample s;
    s.name = name;
    s.position = at;
    s.intendedSpeed = intent;
    s.activity = a;
    return s;
}

} // namespace

TEST_CASE("A wandering body travels and does not pop", "[motion][quality]") {
    OneBody w({behavior("wander", {{"speed", 1.4}, {"radius", 20.0}, {"pauseMin", 0.2}, {"pauseMax", 0.6}})});
    for (int i = 0; i < 20 * 60; ++i) {
        w.step(i);
    }
    const auto report = w.recorder.report();
    const auto& q = only(report);
    CHECK(report.frames == 1200);
    CHECK(report.seconds == Approx(1199.0 * kStep));
    CHECK(q.motion.travelMetres > 5.0);
    CHECK(q.motion.rootPops == 0);
    CHECK(q.motion.largestPopMetres == 0.0);
    CHECK(q.motion.maxSpeed > 0.0);
    CHECK(q.motion.maxSpeed < q.motion.popThresholdSpeed);
    // Default gait: run 4 m/s, times three.
    CHECK(q.motion.popThresholdSpeed == Approx(12.0));
}

TEST_CASE("A director teleport is one root pop of the distance thrown", "[motion][quality]") {
    const auto clean = directorRun(0.0f);
    CHECK(only(clean).motion.rootPops == 0);

    const auto thrown = directorRun(30.0f);
    const auto& q = only(thrown);
    CHECK(q.motion.rootPops == 1);
    CHECK(q.motion.largestPopMetres == Approx(30.0).margin(1e-3));
    CHECK(q.motion.maxSpeed == Approx(30.0 / kStep).epsilon(1e-4));
    // The throw enters and leaves in one step each, so two velocity discontinuities.
    CHECK(q.motion.velocityDiscontinuities == 2);
    CHECK(q.motion.travelMetres == Approx(30.0).margin(1e-3));

    // The threshold itself, pinned from both sides: 3 x the default 4 m/s run is 12 m/s, which is
    // 0.2 m in a 60 Hz step. A 30 m throw passes any threshold up to 150x too loose, so without
    // these two arms a limit that had drifted by a factor of a hundred still read green (it did,
    // when this test was mutated: ADR-826).
    CHECK(only(directorRun(0.15f)).motion.rootPops == 0);
    CHECK(only(directorRun(0.25f)).motion.rootPops == 1);
}

TEST_CASE("The recorder's arithmetic over hand-made samples", "[motion][quality]") {
    using entity::Activity;
    entity::CharacterQualityRecorder rec;
    // Three bodies over 61 frames (frame 0 is the baseline, then 60 steps = 1 s):
    //   stuck   wants 2 m/s and goes nowhere, walking on the spot
    //   mover   wants 2 m/s and covers 3.2 m/s in a walk authored at 1.6: slip 2, every frame
    //   flick   stands still, flicks Idle->Walk->Idle inside a second, then Walk once more at the
    //           end, and is under the director and airborne for the second half
    for (int i = 0; i <= 60; ++i) {
        const double t = static_cast<double>(i) * kStep;
        const double dt = i == 0 ? 0.0 : kStep;
        std::vector<entity::CharacterSample> s;
        s.push_back(sample("stuck", glm::vec3(1.0f, 0.0f, 1.0f), 2.0f, Activity::Walk));
        s.push_back(sample("mover", glm::vec3(static_cast<float>(3.2 * t), 0.0f, 0.0f), 2.0f, Activity::Walk));
        Activity a = Activity::Idle;
        if (i >= 10 && i < 20) {
            a = Activity::Walk;
        } else if (i >= 55) {
            a = Activity::Walk;
        }
        auto f = sample("flick", glm::vec3(0.0f), 0.0f, a);
        f.director = i > 30;
        f.grounded = i <= 30;
        s.push_back(f);
        rec.record(s, t, dt);
    }
    const auto r = rec.report();
    REQUIRE(r.characters.size() == 3);
    CHECK(r.entityCount == 3);
    const auto& stuck = r.characters[0];
    const auto& mover = r.characters[1];
    const auto& flick = r.characters[2];

    CHECK(stuck.behaviour.stuckSeconds == Approx(1.0));
    CHECK(mover.behaviour.stuckSeconds == 0.0);
    CHECK(flick.behaviour.stuckSeconds == 0.0); // intends nothing, so standing still is not stuck

    CHECK(mover.motion.travelMetres == Approx(3.2).epsilon(1e-4));
    CHECK(mover.motion.movingFrames == 60);
    CHECK(mover.motion.slipOutOfBandFraction == Approx(1.0));
    CHECK(mover.motion.worstSlip == Approx(2.0).epsilon(1e-3));
    CHECK(stuck.motion.movingFrames == 0); // walking on the spot is not a moving frame
    CHECK(stuck.motion.worstSlip == 1.0);

    // Idle->Walk at 10, Walk->Idle at 20 (a return inside 1 s: one oscillation), Idle->Walk at 55
    // (Walk->Idle->Walk, 35 frames after Walk was left, still inside a second: a second one).
    // Three changes, two oscillations.
    CHECK(flick.behaviour.activityChanges == 3);
    CHECK(flick.behaviour.oscillations == 2);
    CHECK(flick.behaviour.activityChangesPerMinute == Approx(180.0));
    CHECK(flick.behaviour.idleFraction == Approx(45.0 / 61.0));
    CHECK(stuck.behaviour.activityChanges == 0);
    CHECK(stuck.behaviour.oscillations == 0);
    CHECK(stuck.behaviour.idleFraction == 0.0);

    CHECK(flick.behaviour.directorSeconds == Approx(30.0 * kStep));
    CHECK(flick.behaviour.airborneSeconds == Approx(30.0 * kStep));
    CHECK(stuck.behaviour.directorSeconds == 0.0);
    CHECK(stuck.behaviour.airborneSeconds == 0.0);
}

TEST_CASE("An A->B->A slower than the window is not an oscillation", "[motion][quality]") {
    using entity::Activity;
    entity::CharacterQualityRecorder rec;
    // Walk from 0.5 s to 2.0 s: the return to Idle comes 1.5 s after leaving it.
    for (int i = 0; i <= 180; ++i) {
        const double t = static_cast<double>(i) * kStep;
        const Activity a = (i >= 30 && i < 120) ? Activity::Walk : Activity::Idle;
        const entity::CharacterSample s = sample("slow", glm::vec3(0.0f), 0.0f, a);
        rec.record(std::span<const entity::CharacterSample>(&s, 1), t, i == 0 ? 0.0 : kStep);
    }
    const auto r = rec.report();
    CHECK(r.characters.front().behaviour.activityChanges == 2);
    CHECK(r.characters.front().behaviour.oscillations == 0);
}

namespace {

void collectKeys(const nlohmann::json& j, std::vector<std::string>& out) {
    if (j.is_object()) {
        for (const auto& [k, v] : j.items()) {
            out.push_back(k);
            collectKeys(v, out);
        }
    } else if (j.is_array()) {
        for (const auto& v : j) {
            collectKeys(v, out);
        }
    }
}

} // namespace

TEST_CASE("The quality JSON has every documented field and no aggregate score", "[motion][quality]") {
    const auto report = directorRun(30.0f);
    const nlohmann::json j = entity::toJson(report);

    REQUIRE(j.contains("scene"));
    for (const char* k : {"frames", "seconds", "entityCount"}) {
        CHECK(j["scene"].contains(k));
    }
    REQUIRE(j.contains("machineDependent"));
    CHECK(j["machineDependent"].contains("wallClockMsPerFrame"));
    REQUIRE(j.contains("entities"));
    REQUIRE(j["entities"].size() == 1);
    const auto& e = j["entities"][0];
    CHECK(e["name"] == "body");
    CHECK(e["motion"]["rootPops"]["count"] == 1);
    for (const char* k : {"travelMetres", "maxSpeed", "rootPops", "velocityDiscontinuities", "footSlip"}) {
        CHECK(e["motion"].contains(k));
    }
    for (const char* k : {"count", "largestMetres", "thresholdSpeed"}) {
        CHECK(e["motion"]["rootPops"].contains(k));
    }
    for (const char* k : {"count", "largestAccel"}) {
        CHECK(e["motion"]["velocityDiscontinuities"].contains(k));
    }
    for (const char* k : {"movingFrames", "outOfBandFrames", "outOfBandFraction", "worst"}) {
        CHECK(e["motion"]["footSlip"].contains(k));
    }
    for (const char* k : {"stuckSeconds", "idleFraction", "activityChanges", "activityChangesPerMinute",
                          "oscillations", "directorSeconds", "airborneSeconds"}) {
        CHECK(e["behaviour"].contains(k));
    }

    // Individual metrics, never a combined one (ADR-826). Any key naming a score, a grade or a
    // total anywhere in the document is the aggregate this analyzer exists not to have.
    std::vector<std::string> keys;
    collectKeys(j, keys);
    for (const std::string& k : keys) {
        std::string lower = k;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        INFO(k);
        CHECK(lower.find("score") == std::string::npos);
        CHECK(lower.find("grade") == std::string::npos);
        CHECK(lower.find("overall") == std::string::npos);
    }
}
