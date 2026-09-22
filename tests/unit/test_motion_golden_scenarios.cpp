// Phase C §48: golden motion tests. Deterministic scenarios on the golden corpus (§79), with the
// expected broad behaviour recorded and checked within tolerances.
//
// §48's own example is the first scenario: walk north, turn east, stop. The body is driven the way
// the entity drives it. A motion controller integrates the body's velocity and facing toward each
// request under its limits, and the matcher is told how the body is moving now (`bodyVelocity`,
// `bodyFacing`), so its query trajectory is predicted from there (§25). "Broad behaviour" means
// which kind of motion plays in each stretch, not exact samples. The spec asks for exactly that:
// "do not require exact floating-point equality for every pose unless appropriate".

#include "entity/match_motion_provider.hpp"
#include "entity/motion_controller.hpp"
#include "scene/motion_database.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <map>
#include <string>
#include <vector>

using namespace avgen;

namespace {

struct Leg {
    float until = 0.0f;       // seconds
    glm::vec3 velocity{0.0f}; // desired, world
};

struct Frame {
    float time = 0.0f;
    std::string clip;
    float speed = 0.0f;
    float yaw = 0.0f;
    float sampleSpeed = 0.0f; // how fast the chosen sample itself moves (raw root velocity)
};

std::vector<Frame> drive(const std::vector<Leg>& legs, bool predict = true) {
    static const scene::MotionPack pack = testsupport::goldenPack();
    static const scene::MotionDatabase db = [] {
        auto built = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
        REQUIRE(built.has_value());
        return std::move(*built);
    }();
    entity::MatchMotionProvider matcher;
    matcher.setDatabase(&db);
    matcher.setClips(&pack.animation);
    entity::MatchSettings settings;
    settings.predictTrajectory = predict;
    matcher.setSettings(settings);

    entity::MotionState body;
    body.started = true;
    entity::MotionMemory memory;
    std::vector<Frame> out;
    const float dt = 1.0f / 60.0f;
    std::size_t leg = 0;
    for (int f = 0;; ++f) {
        const float t = static_cast<float>(f) * dt;
        while (leg < legs.size() && t >= legs[leg].until) {
            ++leg;
        }
        if (leg >= legs.size()) {
            break;
        }
        entity::MotionRequest request;
        request.desiredVelocity = legs[leg].velocity;
        const float len = glm::length(legs[leg].velocity);
        request.desiredFacing = len > 1e-4f ? legs[leg].velocity / len : body.facing;
        request.bodyFacing = body.facing;
        request.bodyVelocity = body.velocity;
        request.bodyVelocityKnown = true;
        entity::MotionMemory next;
        const entity::MotionResult r = matcher.advance(request, memory, static_cast<double>(t), dt, next);
        REQUIRE(r.ok());
        memory = next;
        entity::MotionState stepped;
        (void)entity::stepMotion(request, body, settings.limits, dt, stepped);
        body = stepped;
        const auto layout = scene::motionFeatureLayout(db.config);
        std::size_t rv = 0;
        while (layout[rv] != scene::MotionFeatureGroup::RootVelocity) {
            ++rv;
        }
        const float* feat = db.featuresFor(memory.selection);
        const float vx = (feat[rv] / db.scale[rv]) + db.mean[rv];
        const float vz = (feat[rv + 2] / db.scale[rv + 2]) + db.mean[rv + 2];
        out.push_back({t, std::string(r.content), glm::length(body.velocity), std::atan2(body.facing.x, body.facing.z),
                       std::sqrt((vx * vx) + (vz * vz))});
    }
    return out;
}

// What played, by clip, over [from, to).
std::map<std::string, int> between(const std::vector<Frame>& frames, float from, float to) {
    std::map<std::string, int> out;
    for (const Frame& f : frames) {
        if (f.time >= from && f.time < to) {
            ++out[f.clip];
        }
    }
    return out;
}

std::string summary(const std::map<std::string, int>& counts) {
    std::string s;
    for (const auto& [clip, n] : counts) {
        s += fmt::format("{} {}  ", clip, n);
    }
    return s;
}

int count(const std::map<std::string, int>& counts, std::initializer_list<const char*> clips) {
    int n = 0;
    for (const char* c : clips) {
        const auto it = counts.find(c);
        n += it == counts.end() ? 0 : it->second;
    }
    return n;
}

const glm::vec3 kNorth(0.0f, 0.0f, 1.0f);
const glm::vec3 kEast(1.0f, 0.0f, 0.0f);

} // namespace

TEST_CASE("§48 walk north, turn east, stop", "[golden][matching][phaseC]") {
    const std::vector<Frame> frames = drive({{1.5f, kNorth * testsupport::kGoldenWalk},
                                             {3.0f, kEast * testsupport::kGoldenWalk},
                                             {4.5f, glm::vec3(0.0f)}});
    const auto setOff = between(frames, 0.0f, 0.5f);
    const auto cruise = between(frames, 1.0f, 1.5f);
    const auto turning = between(frames, 1.5f, 2.5f);
    const auto stopping = between(frames, 3.0f, 3.8f);
    const auto stood = between(frames, 4.1f, 4.5f);
    WARN(fmt::format("set off: {}\ncruise: {}\nturning: {}\nstopping: {}\nstood: {}\nfinal facing {:.2f} rad",
                     summary(setOff), summary(cruise), summary(turning), summary(stopping), summary(stood),
                     frames.back().yaw));
    // Setting off from rest is a start (or already a walk), never a run, reverse or strafe.
    CHECK(count(setOff, {"Start", "Walk", "Idle"}) == static_cast<int>(30));
    CHECK(count(setOff, {"Start"}) > 0);
    // Cruising north is forward walking.
    CHECK(count(cruise, {"Walk", "Start", "Stop"}) == 30);
    // Turning east from north: +X is the body's left, so a left turn plays, and never a right one.
    CHECK(count(turning, {"TurnLeft"}) > 0);
    CHECK(count(turning, {"TurnRight"}) == 0);
    // Asked to stop: a stop, then standing.
    CHECK(count(stopping, {"Stop", "Idle"}) > 0);
    CHECK(count(stood, {"Idle", "Stop"}) == static_cast<int>(frames.size() > 0 ? 24 : 0));
    // And the body ended facing east.
    CHECK(std::abs(frames.back().yaw - 1.5707963f) < 0.1f);
}

TEST_CASE("§48 the scenarios are deterministic", "[golden][matching][determinism][phaseC]") {
    const std::vector<Leg> legs = {{1.0f, kNorth * testsupport::kGoldenWalk},
                                   {2.0f, -kEast * testsupport::kGoldenRun},
                                   {3.0f, glm::vec3(0.0f)}};
    const std::vector<Frame> a = drive(legs);
    const std::vector<Frame> b = drive(legs);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].clip == b[i].clip);
    }
}

TEST_CASE("§48/§25 the predicted trajectory is what lets a start and a turn be chosen",
          "[golden][matching][phaseC]") {
    // The control for everything above: the same scenario with the query built the old way, as
    // the asked-for velocity held. A body told to walk while standing then looks like a body
    // already walking, so it gets walking motion, and a turn looks like a sidestep, so no turn is
    // chosen (measured: 0 left-turn frames). If this produced the same answers, the prediction
    // would be doing nothing.
    const std::vector<Leg> legs = {{1.5f, kNorth * testsupport::kGoldenWalk},
                                   {3.0f, kEast * testsupport::kGoldenWalk}};
    const auto predicted = drive(legs, true);
    const auto held = drive(legs, false);
    // The start is judged by how fast the chosen motion is moving while the body sets off, because
    // the end of `Start` is a walk, and a held query finds it there. Over the first 0.3 s the body
    // is barely moving: a predicted query picks slow motion, a held one picks walking motion.
    const auto meanSampleSpeed = [](const std::vector<Frame>& frames, float from, float to) {
        float sum = 0.0f;
        int n = 0;
        for (const Frame& f : frames) {
            if (f.time >= from && f.time < to) {
                sum += f.sampleSpeed;
                ++n;
            }
        }
        return n > 0 ? sum / static_cast<float>(n) : 0.0f;
    };
    const float setOffP = meanSampleSpeed(predicted, 0.0f, 0.3f);
    const float setOffH = meanSampleSpeed(held, 0.0f, 0.3f);
    const auto turnsP = count(between(predicted, 1.5f, 2.5f), {"TurnLeft"});
    const auto turnsH = count(between(held, 1.5f, 2.5f), {"TurnLeft"});
    WARN(fmt::format("setting off, the chosen motion moves at {:.2f} m/s predicted and {:.2f} m/s held; "
                     "left-turn frames {} predicted and {} held",
                     setOffP, setOffH, turnsP, turnsH));
    // Not a half: under the default limits (6 m/s^2) the body itself reaches a walk in 0.2 s, so the
    // predicted query legitimately moves on to walking motion within the window. Measured: 0.66
    // against 1.18 m/s.
    CHECK(setOffP < 0.75f * setOffH);
    CHECK(turnsP > turnsH);
}
