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
//   patterns     ADR-910: reversals out of a stop, A->B->A revisits, pivot yaw against a walked
//                circle's radius, standing on and facing up a hillside -- each against a control

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <functional>
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
                          "oscillations", "directorSeconds", "airborneSeconds", "stillFraction",
                          "longestStillSeconds", "stops"}) {
        CHECK(e["behaviour"].contains(k));
    }
    // ADR-910's patterns, where a reader of the file will look for them.
    for (const char* k : {"count", "measured", "reversals", "turnsOver90", "revisits"}) {
        CHECK(e["behaviour"]["stops"].contains(k));
    }
    REQUIRE(e["motion"].contains("turning"));
    for (const char* k : {"yawDegrees", "pivotYawDegrees", "pivotYawFraction", "turnSamples",
                          "turnRadiusMedian", "turnRadiusP10"}) {
        CHECK(e["motion"]["turning"].contains(k));
    }
    REQUIRE(e.contains("ground"));
    for (const char* k : {"stillSeconds", "slopeMeanDegrees", "slopeMaxDegrees", "steepSeconds",
                          "facingUphillSeconds"}) {
        CHECK(e["ground"].contains(k));
    }
    for (const char* k : {"stillSpeed", "stopSeconds", "pivotSpeed", "reversalDegrees", "headingMetres",
                          "revisitRadius", "turnSpeedMin", "turnRateMinDegrees", "steepDegrees",
                          "uphillDegrees"}) {
        CHECK(j["thresholds"].contains(k));
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

// =================================================================================================
// ADR-910: the patterns a viewer reads as a mechanism rather than a creature
// =================================================================================================
//
// Hand-built bodies through the plain-struct door, each with a control that must read the opposite
// (ADR-182). The paths are written as functions of time, sampled at the application's 60 Hz with
// its zero-length first frame, so the arithmetic under test is the recorder's and nothing else's.

namespace {

// A scripted body: where it is and which way it faces at time t, and what it stands on.
struct Pose {
    glm::vec2 at{0.0f};
    float yaw = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool grounded = false;
};

entity::CharacterQualityReport script(const char* name, double seconds,
                                      const std::function<Pose(double)>& path) {
    entity::CharacterQualityRecorder rec;
    const int frames = static_cast<int>(std::llround(seconds / kStep));
    for (int i = 0; i <= frames; ++i) {
        const double t = static_cast<double>(i) * kStep;
        const Pose p = path(t);
        entity::CharacterSample s;
        s.name = name;
        s.position = glm::vec3(p.at.x, 0.0f, p.at.y);
        s.yaw = p.yaw;
        s.groundNormal = p.normal;
        s.hasGround = p.grounded;
        rec.record(std::span<const entity::CharacterSample>(&s, 1), t, i == 0 ? 0.0 : kStep);
    }
    return rec.report();
}

constexpr float kHalfPi = 1.5707963f;

// Walk `a` -> `b` at `speed`, facing the way it walks.
Pose walking(glm::vec2 a, glm::vec2 b, double speed, double t) {
    const glm::vec2 d = b - a;
    const double len = glm::length(d);
    const double s = std::min(1.0, speed * t / len);
    return Pose{a + d * static_cast<float>(s), std::atan2(d.x, d.y)};
}

// Walk east 9 m at 1.5 m/s, stand three seconds turning to `outYaw`, then walk 9 m that way.
Pose outAndOnward(double t, float outYaw) {
    const double walk = 6.0; // 9 m at 1.5 m/s
    if (t <= walk) {
        return walking({0.0f, 0.0f}, {9.0f, 0.0f}, 1.5, t);
    }
    const glm::vec2 stop(9.0f, 0.0f);
    if (t <= walk + 3.0) {
        // Standing, turning toward the way out over the first second of the stand.
        const float k = static_cast<float>(std::min(1.0, t - walk));
        return Pose{stop, kHalfPi + (outYaw - kHalfPi) * k};
    }
    const glm::vec2 out(std::sin(outYaw), std::cos(outYaw));
    return Pose{stop + out * static_cast<float>(1.5 * (t - walk - 3.0)), outYaw};
}

} // namespace

TEST_CASE("walking out of a stop the way it came is a reversal; carrying on is not",
          "[motion][quality][adr910]") {
    // East is +x, which is yaw +pi/2 (forward = (sin yaw, cos yaw)).
    const auto back = script("back", 16.0, [](double t) { return outAndOnward(t, -kHalfPi); });
    const auto& b = only(back);
    CHECK(b.behaviour.stops == 1);
    CHECK(b.behaviour.measuredStops == 1);
    CHECK(b.behaviour.reversals == 1);
    CHECK(b.behaviour.turnsOver90 == 1);
    // Three seconds standing, to the frame: the stand is 180 steps whose speed is zero.
    CHECK(b.behaviour.longestStillSeconds == Approx(3.0).margin(kStep * 1.5));

    // The control: the same stop, then on eastward. The stop is identical; the way out is not.
    const auto onward = script("onward", 16.0, [](double t) { return outAndOnward(t, kHalfPi); });
    const auto& o = only(onward);
    CHECK(o.behaviour.stops == 1);
    CHECK(o.behaviour.measuredStops == 1);
    CHECK(o.behaviour.reversals == 0);
    CHECK(o.behaviour.turnsOver90 == 0);
    CHECK(o.behaviour.longestStillSeconds == Approx(b.behaviour.longestStillSeconds));

    // And the threshold between them: out at 120 degrees from the way in is a turn over 90 and not a
    // reversal; the recorder's 150 is the line, pinned from both sides.
    const float at120 = kHalfPi + 2.0943951f; // 120 degrees round from east
    const auto sharp = script("sharp", 16.0, [&](double t) { return outAndOnward(t, at120); });
    CHECK(only(sharp).behaviour.turnsOver90 == 1);
    CHECK(only(sharp).behaviour.reversals == 0);
    const float at160 = kHalfPi + 2.7925268f; // 160 degrees round
    const auto nearly = script("nearly", 16.0, [&](double t) { return outAndOnward(t, at160); });
    CHECK(only(nearly).behaviour.reversals == 1);
}

TEST_CASE("a stop back where the body stood two stops ago is an A->B->A revisit",
          "[motion][quality][adr910]") {
    // Three legs with a two-second stand after each: A -> B -> back to A, and A -> B -> on to C.
    const auto legs = [](glm::vec2 a, glm::vec2 b, glm::vec2 c, double t) {
        const double walk = 10.0 / 2.0; // every leg is 10 m at 2 m/s
        const double hold = 2.0;
        const glm::vec2 points[] = {a, b, c};
        for (int leg = 0; leg < 2; ++leg) {
            const double start = static_cast<double>(leg) * (walk + hold) + hold;
            if (t < start) {
                return Pose{points[leg], 0.0f};
            }
            if (t < start + walk) {
                return walking(points[leg], points[leg + 1], 2.0, t - start);
            }
        }
        return Pose{c, 0.0f};
    };
    const glm::vec2 A(0.0f, 0.0f);
    const glm::vec2 B(10.0f, 0.0f);
    const auto aba = script("aba", 20.0, [&](double t) { return legs(A, B, A + glm::vec2(0.5f, 0.0f), t); });
    CHECK(only(aba).behaviour.stops == 3);
    CHECK(only(aba).behaviour.revisits == 1);

    const auto abc = script("abc", 20.0, [&](double t) { return legs(A, B, B + glm::vec2(10.0f, 0.0f), t); });
    CHECK(only(abc).behaviour.stops == 3);
    CHECK(only(abc).behaviour.revisits == 0); // the control: the third stop is 20 m from the first
}

namespace {

// A tour of `points` at 2 m/s, standing two seconds at each (the first included), facing the way it
// walks: the stops the recorder counts are the stands, and the way into and out of each is the leg.
Pose tour(const std::vector<glm::vec2>& points, double t) {
    constexpr double kHold = 2.0;
    constexpr double kSpeed = 2.0;
    double clock = 0.0;
    for (std::size_t leg = 0; leg + 1 < points.size(); ++leg) {
        if (t < clock + kHold) {
            return Pose{points[leg], 0.0f};
        }
        clock += kHold;
        const double walk = static_cast<double>(glm::length(points[leg + 1] - points[leg])) / kSpeed;
        if (t < clock + walk) {
            return walking(points[leg], points[leg + 1], kSpeed, t - clock);
        }
        clock += walk;
    }
    return Pose{points.back(), 0.0f};
}

} // namespace

TEST_CASE("walking back and forth between two places is pacing; a round of places is not",
          "[motion][quality][adr933]") {
    // ADR-933. GV3's ember walked to a river bank and back, again and again: stops walked out of back
    // the way they were walked into, one after another. The run is the pattern; one on its own is a
    // change of mind.
    const glm::vec2 A(0.0f, 0.0f);
    const glm::vec2 B(10.0f, 0.0f);
    const glm::vec2 C(-10.0f, 3.0f);
    // A -> B -> A -> B -> A, then on to C. The stops at B (7 s), A (14 s) and B (21 s) are walked out
    // of the way they were walked into; the one at A at 28 s is walked on out of, 17 degrees round.
    const auto pacing = script("pacer", 40.0, [&](double t) { return tour({A, B, A, B, A, C}, t); });
    const auto& p = only(pacing);
    CHECK(p.behaviour.reversals == 3);
    CHECK(p.behaviour.longestPacing == 3);
    CHECK(p.behaviour.longestPacingFrom == Approx(7.0).margin(0.05));
    CHECK(p.behaviour.longestPacingSeconds == Approx(14.0).margin(0.05));

    // Turned back at 120 degrees each time -- what a turnaround measures when the body walks through
    // it (ADR-908: ember's measured 116-129) -- is pacing too, though it is never a reversal.
    const glm::vec2 T(5.0f, 8.6602540f);
    const auto curved = script("curved", 40.0, [&](double t) { return tour({A, B, T, A, B}, t); });
    CHECK(only(curved).behaviour.reversals == 0);
    CHECK(only(curved).behaviour.turnsOver90 == 3);
    CHECK(only(curved).behaviour.longestPacing == 3);

    // The control: the same legs round a pentagon. Every stop turns 72 degrees; none turns back.
    std::vector<glm::vec2> pentagon;
    for (int k = 0; k < 6; ++k) {
        const float a = 1.2566371f * static_cast<float>(k); // 72 degrees a side
        pentagon.push_back(pentagon.empty() ? A : pentagon.back() + glm::vec2(std::cos(a), std::sin(a)) * 10.0f);
    }
    const auto round = script("rounder", 50.0, [&](double t) { return tour(pentagon, t); });
    CHECK(only(round).behaviour.measuredStops >= 4);
    CHECK(only(round).behaviour.reversals == 0);
    CHECK(only(round).behaviour.longestPacing == 0);
    CHECK(only(round).behaviour.longestPacingSeconds == 0.0);

    // A turn back, a walk on, a turn back: two changes of mind with a real walk between them, not a run.
    const auto broken = script("broken", 40.0, [&](double t) { return tour({A, B, A, C, A, B}, t); });
    CHECK(only(broken).behaviour.reversals == 2);
    CHECK(only(broken).behaviour.longestPacing == 1);

    // And where a reader of the file will look for it.
    const nlohmann::json j = entity::toJson(pacing);
    const auto& stops = j["entities"][0]["behaviour"]["stops"];
    REQUIRE(stops.contains("pacing"));
    CHECK(stops["pacing"]["longest"] == 3);
    CHECK(stops["pacing"]["seconds"].get<double>() == Approx(14.0).margin(0.05));
    CHECK(stops["pacing"]["from"].get<double>() == Approx(7.0).margin(0.05));
}

TEST_CASE("turning on the spot is pivot yaw; turning on a circle measures its radius",
          "[motion][quality][adr910]") {
    // A body standing still and turning half a revolution over two seconds.
    const auto pivot = script("pivot", 3.0, [](double t) {
        return Pose{{0.0f, 0.0f}, static_cast<float>(3.14159265 * std::min(1.0, t / 2.0))};
    });
    const auto& p = only(pivot);
    CHECK(p.motion.yawDegrees == Approx(180.0).margin(0.01));
    CHECK(p.motion.pivotYawDegrees == Approx(180.0).margin(0.01));
    CHECK(p.motion.pivotYawFraction == Approx(1.0));
    CHECK(p.motion.turnSamples == 0); // it never moved, so it never drew a radius

    // A body walking a circle of radius 4 m at 2 m/s, facing along it.
    constexpr double kRadius = 4.0;
    constexpr double kSpeed = 2.0;
    const auto circle = script("circle", 12.0, [&](double t) {
        const double a = kSpeed * t / kRadius;
        const glm::vec2 at(static_cast<float>(kRadius * std::cos(a)), static_cast<float>(kRadius * std::sin(a)));
        // The tangent of (cos a, sin a) is (-sin a, cos a); as a yaw that is atan2(-sin a, cos a).
        return Pose{at, static_cast<float>(std::atan2(-std::sin(a), std::cos(a)))};
    });
    const auto& c = only(circle);
    CHECK(c.motion.pivotYawDegrees == 0.0);
    REQUIRE(c.motion.turnSamples > 600);
    // The chord of a step is a hair shorter than its arc, so the radius reads a hair low.
    CHECK(c.motion.turnRadiusMedian == Approx(kRadius).epsilon(1e-3));
    CHECK(c.motion.turnRadiusP10 == Approx(kRadius).epsilon(1e-3));
}

TEST_CASE("standing on a hillside facing up it is facing into the hill; across it is not",
          "[motion][quality][adr910]") {
    // A slope rising toward +x at 20 degrees: its normal leans toward -x.
    const float k = std::tan(20.0f / 57.2957795f);
    const glm::vec3 hill = glm::normalize(glm::vec3(-k, 1.0f, 0.0f));
    const auto stand = [&](float yaw, glm::vec3 normal) {
        return script("stand", 2.0, [=](double) { return Pose{{0.0f, 0.0f}, yaw, normal, true}; });
    };
    const auto up = stand(kHalfPi, hill); // facing +x, straight uphill
    CHECK(only(up).ground.stillSeconds == Approx(2.0));
    CHECK(only(up).ground.steepSeconds == Approx(2.0));
    CHECK(only(up).ground.facingUphillSeconds == Approx(2.0));
    CHECK(only(up).ground.slopeMaxDegrees == Approx(20.0).margin(1e-3));
    CHECK(only(up).ground.slopeMeanDegrees == Approx(20.0).margin(1e-3));

    const auto across = stand(0.0f, hill); // facing +z, along the contour
    CHECK(only(across).ground.steepSeconds == Approx(2.0));
    CHECK(only(across).ground.facingUphillSeconds == 0.0);

    const auto down = stand(-kHalfPi, hill); // facing -x, downhill
    CHECK(only(down).ground.facingUphillSeconds == 0.0);

    // Gentle ground is not steep, whichever way the body faces.
    const float g = std::tan(8.0f / 57.2957795f);
    const auto gentle = stand(kHalfPi, glm::normalize(glm::vec3(-g, 1.0f, 0.0f)));
    CHECK(only(gentle).ground.steepSeconds == 0.0);
    CHECK(only(gentle).ground.facingUphillSeconds == 0.0);
    CHECK(only(gentle).ground.slopeMaxDegrees == Approx(8.0).margin(1e-3));

    // No surface reported is no surface measured: not flat ground, nothing.
    const auto floating = script("floating", 2.0, [&](double) { return Pose{{0.0f, 0.0f}, kHalfPi, hill, false}; });
    CHECK(only(floating).ground.stillSeconds == 0.0);
    CHECK(only(floating).ground.steepSeconds == 0.0);
}
