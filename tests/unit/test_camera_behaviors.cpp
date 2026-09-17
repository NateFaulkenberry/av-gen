// Chase, Orbit and POV: the three camera presets that are a *relationship to a performer* rather
// than a path through the world.
//
// The mandate's section 22 asks for static and moving subjects, playback against scrub, serialisation
// round-trips and collision. What it asks for most insistently is determinism, so that is what most
// of this file is: for each behaviour, evaluating frame N from a clean state must match evaluating
// frames 0..N.
//
// That property is not defended by a filter or a reset here -- it is structural. A behaviour is
// resolved at **bake** against `Actor::positionAt` and `::headingAt`, both pure functions of time,
// and what comes out is an ordinary key track. There is no state to reset at a cut because there is
// no state. These tests exist to keep it that way.

#include "app/cinematic.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "seq/layers.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr double kShotSeconds = 4.0;

std::filesystem::path project() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "night-shift.json";
}

// A performer walking +X, facing the way they are going.
seq::Actor walker(std::string id = "walker") {
    seq::Actor a;
    a.id = std::move(id);
    a.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {-20.0f, 0.0f, 0.0f}});
    a.keys.push_back(seq::ActorKey{.timeSeconds = kShotSeconds, .position = {20.0f, 0.0f, 0.0f}});
    return a;
}

// A performer standing perfectly still, which is the case Orbit has to work on and Chase has to not
// fall over on.
seq::Actor statue(std::string id = "statue") {
    seq::Actor a;
    a.id = std::move(id);
    a.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {0.0f, 0.0f, 0.0f}});
    a.keys.push_back(seq::ActorKey{.timeSeconds = kShotSeconds, .position = {0.0f, 0.0f, 0.0f}});
    return a;
}

seq::Sequence pieceWith(seq::CameraPreset preset, const seq::Actor& actor) {
    seq::Sequence piece;
    piece.name = "behaviour";
    piece.actors.push_back(actor);
    app::FocalTarget subject;
    subject.position = actor.positionAt(0.0);
    subject.radius = 2.0f;
    subject.name = actor.id;
    seq::Shot shot;
    shot.name = "probe";
    shot.startSeconds = 0.0;
    shot.durationSeconds = kShotSeconds;
    shot.camera = seq::cameraFromPreset(preset, subject);
    shot.camera.behavior.actor = actor.id;
    piece.shots.push_back(shot);
    return piece;
}

} // namespace

// ---- the evaluator, pure --------------------------------------------------------------------

TEST_CASE("A chase sits where the offset says, in the performer's frame", "[camera][behavior]") {
    seq::CameraBehavior b;
    b.kind = seq::CameraBehaviorKind::Chase;
    b.offset = {0.0f, 2.0f, -4.0f}; // four behind, two above
    b.actorSpace = true;
    b.aim = seq::CameraAim::Subject;
    b.aimOffset = {0.0f, 0.0f, 0.0f};

    // Facing +Z (heading 0): "behind" is -Z.
    const seq::ActorPose facingZ{.position = {0.0f, 0.0f, 0.0f}, .headingDegrees = 0.0f};
    const seq::BehaviorPose a = seq::cameraPoseFor(b, 0.0f, facingZ, facingZ);
    CHECK_THAT(a.eye.x, Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK_THAT(a.eye.y, Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK_THAT(a.eye.z, Catch::Matchers::WithinAbs(-4.0, 1e-4));

    // Turned to face +X (heading 90): "behind" is now -X, and the height is unchanged. This is the
    // whole point of an actor-local offset, and the assertion that would fail if the axis convention
    // were wrong in either direction.
    const seq::ActorPose facingX{.position = {0.0f, 0.0f, 0.0f}, .headingDegrees = 90.0f};
    const seq::BehaviorPose c = seq::cameraPoseFor(b, 0.0f, facingX, facingX);
    CHECK_THAT(c.eye.x, Catch::Matchers::WithinAbs(-4.0, 1e-4));
    CHECK_THAT(c.eye.y, Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK_THAT(c.eye.z, Catch::Matchers::WithinAbs(0.0, 1e-4));

    // And in world space it does not turn with them, which is the other half of the switch.
    b.actorSpace = false;
    const seq::BehaviorPose w = seq::cameraPoseFor(b, 0.0f, facingX, facingX);
    CHECK_THAT(w.eye.z, Catch::Matchers::WithinAbs(-4.0, 1e-4));
}

TEST_CASE("Lateral is the performer's right", "[camera][behavior]") {
    seq::CameraBehavior b;
    b.kind = seq::CameraBehaviorKind::Chase;
    b.offset = {1.0f, 0.0f, 0.0f}; // one metre to their right, nothing else
    const seq::ActorPose facingZ{.position = {0.0f, 0.0f, 0.0f}, .headingDegrees = 0.0f};
    const seq::BehaviorPose p = seq::cameraPoseFor(b, 0.0f, facingZ, facingZ);
    // Facing +Z, right is +X. Getting this backwards is the single easiest mistake in the file and
    // it is invisible until somebody watches a chase sit on the wrong shoulder.
    CHECK_THAT(p.eye.x, Catch::Matchers::WithinAbs(1.0, 1e-4));
}

TEST_CASE("An orbit circles a subject that never moves", "[camera][behavior]") {
    seq::CameraBehavior b;
    b.kind = seq::CameraBehaviorKind::Orbit;
    b.radius = 10.0f;
    b.height = 3.0f;
    b.startDegrees = 0.0f;
    b.endDegrees = 180.0f;
    b.easeInOut = false; // the arithmetic, not the easing
    b.aim = seq::CameraAim::Subject;
    b.aimOffset = {0.0f, 0.0f, 0.0f};

    const seq::ActorPose still{.position = {0.0f, 0.0f, 0.0f}, .headingDegrees = 0.0f};
    const seq::BehaviorPose a = seq::cameraPoseFor(b, 0.0f, still, still);
    const seq::BehaviorPose m = seq::cameraPoseFor(b, 0.5f, still, still);
    const seq::BehaviorPose z = seq::cameraPoseFor(b, 1.0f, still, still);

    // 0 degrees is +Z, 90 is +X, 180 is -Z -- the same convention the performer's heading uses.
    CHECK_THAT(a.eye.z, Catch::Matchers::WithinAbs(10.0, 1e-3));
    CHECK_THAT(m.eye.x, Catch::Matchers::WithinAbs(10.0, 1e-3));
    CHECK_THAT(z.eye.z, Catch::Matchers::WithinAbs(-10.0, 1e-3));

    // The radius and the height are held all the way round: an orbit that drifts in or up is a
    // spiral, and the two read completely differently.
    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        const seq::BehaviorPose p = seq::cameraPoseFor(b, t, still, still);
        CHECK_THAT(glm::length(glm::vec2(p.eye.x, p.eye.z)),
                   Catch::Matchers::WithinAbs(10.0, 1e-3));
        CHECK_THAT(p.eye.y, Catch::Matchers::WithinAbs(3.0, 1e-3));
        // And it is always looking at the subject.
        CHECK_THAT(glm::length(p.target), Catch::Matchers::WithinAbs(0.0, 1e-3));
    }
}

TEST_CASE("POV is at the eyes and looks where they are going", "[camera][behavior]") {
    seq::CameraBehavior b;
    b.kind = seq::CameraBehaviorKind::Pov;
    b.eyeOffset = {0.0f, 1.7f, 0.0f};
    b.aim = seq::CameraAim::Travel;
    b.aimOffset = {0.0f, 0.0f, 0.0f};

    const seq::ActorPose facingX{.position = {5.0f, 0.0f, 0.0f}, .headingDegrees = 90.0f};
    const seq::BehaviorPose p = seq::cameraPoseFor(b, 0.5f, facingX, facingX);

    // The eye is on them, at eye height.
    CHECK_THAT(p.eye.x, Catch::Matchers::WithinAbs(5.0, 1e-4));
    CHECK_THAT(p.eye.y, Catch::Matchers::WithinAbs(1.7, 1e-4));

    // And the target is further along +X, so the view looks down their line of travel rather than
    // at their own head -- which is what aiming a POV camera at its own subject would do.
    CHECK(p.target.x > p.eye.x + 1.0f);
    CHECK_THAT(p.target.z, Catch::Matchers::WithinAbs(0.0, 1e-3));
}

// ---- through the engine: a real bake, real frames ---------------------------------------------

TEST_CASE("A chase travels with the performer and holds its offset", "[camera][behavior]") {
    if (!std::filesystem::exists(project())) {
        SKIP("night-shift is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    seq::Sequence piece = pieceWith(seq::CameraPreset::Chase, walker());
    piece.shots.front().camera.behavior.lagSeconds = 0.0; // the offset, not the trail
    piece.shots.front().camera.behavior.clearance = 0.0f; // and not the ground either
    REQUIRE(piece.validate().has_value());
    REQUIRE(engine.setSequence(piece).has_value());

    FixedStepClock clock(60.0);
    engine.seekSeconds(0.0);
    std::vector<glm::vec3> eyes;
    for (int frame = 0; frame <= 240; ++frame) {
        engine.update(engine.tick(clock));
        if (frame % 60 == 0) {
            eyes.push_back(comp->scene().camera.position);
        }
    }
    REQUIRE(eyes.size() == 5);

    // **The camera moved with them.** This is the assertion that separates Chase from Follow: the
    // performer covers 40 m and so does the eye.
    const float travel = glm::length(eyes.back() - eyes.front());
    INFO("the chase eye travelled " << travel << " m while the performer covered 40 m");
    CHECK(travel > 30.0f);

    // And it stayed behind and above them the whole way, which is the half that makes it a chase
    // rather than merely a camera that also moves.
    const seq::Actor& a = piece.actors.front();
    for (std::size_t i = 0; i < eyes.size(); ++i) {
        const double t = static_cast<double>(i) * (kShotSeconds / 4.0);
        const glm::vec3 them = a.positionAt(std::min(t, kShotSeconds));
        CHECK_THAT(eyes[i].y - them.y, Catch::Matchers::WithinAbs(2.0, 0.35));
        // They walk +X facing +X, so the camera sits at smaller x by roughly the offset.
        CHECK(eyes[i].x < them.x);
    }
}

TEST_CASE("An orbit moves while its subject does not", "[camera][behavior]") {
    if (!std::filesystem::exists(project())) {
        SKIP("night-shift is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    seq::Sequence piece = pieceWith(seq::CameraPreset::Orbit, statue());
    seq::CameraBehavior& b = piece.shots.front().camera.behavior;
    b.radius = 10.0f;
    b.height = 3.0f;
    b.startDegrees = 0.0f;
    b.endDegrees = 180.0f;
    b.clearance = 0.0f;
    REQUIRE(engine.setSequence(piece).has_value());

    FixedStepClock clock(60.0);
    engine.seekSeconds(0.0);
    std::vector<glm::vec3> eyes;
    for (int frame = 0; frame <= 240; ++frame) {
        engine.update(engine.tick(clock));
        if (frame % 120 == 0) {
            eyes.push_back(comp->scene().camera.position);
        }
    }
    // **The subject never moved and the camera went half way round it.** That is the property that
    // distinguishes Orbit from everything else here, and the one a static-subject test exists for.
    CHECK(glm::length(eyes.back() - eyes.front()) > 15.0f);
    for (const glm::vec3& e : eyes) {
        CHECK_THAT(glm::length(glm::vec2(e.x, e.z)), Catch::Matchers::WithinAbs(10.0, 0.6));
    }
}

// ---- determinism: the mandate's section 14 ----------------------------------------------------

TEST_CASE("Playing to a frame and seeking to it give the same camera", "[camera][behavior]") {
    if (!std::filesystem::exists(project())) {
        SKIP("night-shift is not present");
    }
    for (const auto preset :
         {seq::CameraPreset::Chase, seq::CameraPreset::Orbit, seq::CameraPreset::Pov}) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(project()).has_value());
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        seq::Sequence piece = pieceWith(preset, walker());
        REQUIRE(engine.setSequence(piece).has_value());

        // **Offline, the clock is the authority** -- `Engine::tick` says so in as many words, and
        // `seekSeconds` does not move what `update` evaluates. So "evaluate frame N directly" is
        // done the way the render job does it: build the `FrameTime` and call `update`. That is a
        // stricter reading of the mandate's requirement than seeking would have been, because it
        // shares no state at all with the played arm beyond the baked track.
        // Arm A: play every frame from the start, and note where the clock actually landed. Taking
        // the time from the engine rather than computing it is deliberate -- the first version
        // assumed 180 ticks reached 3.0 s and they reach 2.983, so the two arms were a frame apart
        // and the 0.21 m that produced looked exactly like a behaviour with state in it.
        FixedStepClock played(60.0);
        engine.seekSeconds(0.0);
        for (int frame = 0; frame < 180; ++frame) {
            engine.update(engine.tick(played));
        }
        const double at = engine.timelineClock().seconds;
        const glm::vec3 playedEye = comp->scene().camera.position;
        const glm::vec3 playedAim = comp->scene().camera.target;
        REQUIRE(at > 1.0); // the arm ran at all

        // Arm B: one frame at that same instant, arrived at from the far end of the piece, so
        // nothing about the approach is shared with arm A.
        engine.update(FrameTime{.renderTime = kShotSeconds, .deltaTime = 1.0 / 60.0, .frameIndex = 1});
        engine.update(FrameTime{.renderTime = at, .deltaTime = 1.0 / 60.0, .frameIndex = 2});
        const glm::vec3 seekedEye = comp->scene().camera.position;
        const glm::vec3 seekedAim = comp->scene().camera.target;

        INFO("preset " << seq::cameraPresetName(preset) << ": played ("
                       << playedEye.x << ", " << playedEye.y << ", " << playedEye.z << ") seeked ("
                       << seekedEye.x << ", " << seekedEye.y << ", " << seekedEye.z << ")");
        // A key track is a pure function of time, so this is **exact**, not close. The tolerance is
        // float arithmetic, not a tolerance on the behaviour -- if a behaviour ever grows a filter
        // or an accumulator, this is the test that fails, and it should.
        CHECK_THAT(glm::length(seekedEye - playedEye), Catch::Matchers::WithinAbs(0.0, 1e-3));
        CHECK_THAT(glm::length(seekedAim - playedAim), Catch::Matchers::WithinAbs(0.0, 1e-3));
    }
}

// ---- serialisation: the mandate's section 20 --------------------------------------------------

TEST_CASE("A behaviour survives a round trip", "[camera][behavior]") {
    for (const auto preset :
         {seq::CameraPreset::Chase, seq::CameraPreset::Orbit, seq::CameraPreset::Pov}) {
        seq::Sequence piece = pieceWith(preset, walker());
        seq::CameraBehavior& b = piece.shots.front().camera.behavior;
        // Values that are not the defaults, or the test passes on a struct nobody wrote to.
        b.offset = {0.5f, 2.5f, -6.0f};
        b.lagSeconds = 0.4;
        b.radius = 12.5f;
        b.startDegrees = -30.0f;
        b.endDegrees = 200.0f;
        b.eyeOffset = {0.1f, 1.62f, 0.2f};
        b.aim = seq::CameraAim::Custom;
        b.aimPoint = {3.0f, 4.0f, 5.0f};
        b.lookAheadSeconds = 0.3;
        b.clearance = 1.25f;

        const auto parsed = seq::Sequence::fromJson(piece.toJson());
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->shots.size() == 1);
        const seq::ShotCamera& back = parsed->shots.front().camera;
        INFO("preset " << seq::cameraPresetName(preset));
        CHECK(back.kind == seq::CameraKind::Behavior);
        CHECK(back.behavior.kind == b.kind);
        CHECK(back.behavior.actor == b.actor);
        CHECK(back.behavior.aim == seq::CameraAim::Custom);
        CHECK_THAT(back.behavior.aimPoint.x, Catch::Matchers::WithinAbs(3.0, 1e-4));
        CHECK_THAT(back.behavior.lookAheadSeconds, Catch::Matchers::WithinAbs(0.3, 1e-9));
        CHECK_THAT(back.behavior.clearance, Catch::Matchers::WithinAbs(1.25, 1e-4));
        // Only the fields the behaviour in hand reads are written, so only those are checked: a
        // chase's orbit radius is not a fact about the chase.
        if (b.kind == seq::CameraBehaviorKind::Chase) {
            CHECK_THAT(back.behavior.offset.z, Catch::Matchers::WithinAbs(-6.0, 1e-4));
            CHECK_THAT(back.behavior.lagSeconds, Catch::Matchers::WithinAbs(0.4, 1e-9));
        } else if (b.kind == seq::CameraBehaviorKind::Orbit) {
            CHECK_THAT(back.behavior.radius, Catch::Matchers::WithinAbs(12.5, 1e-4));
            CHECK_THAT(back.behavior.endDegrees, Catch::Matchers::WithinAbs(200.0, 1e-4));
        } else {
            CHECK_THAT(back.behavior.eyeOffset.y, Catch::Matchers::WithinAbs(1.62, 1e-4));
        }
    }
}

TEST_CASE("A behaviour that names nothing is refused rather than rendered", "[camera][behavior]") {
    seq::Sequence piece = pieceWith(seq::CameraPreset::Chase, walker());
    piece.shots.front().camera.behavior.actor.clear();
    // A chase with no performer would sit at the world origin looking at the world origin -- a shot
    // that renders perfectly and means nothing, which is the failure this refusal exists to stop.
    CHECK_FALSE(piece.validate().has_value());

    piece.shots.front().camera.behavior.actor = "somebody-else";
    CHECK_FALSE(piece.validate().has_value());

    piece.shots.front().camera.behavior.actor = "walker";
    CHECK(piece.validate().has_value());

    piece.shots.front().camera.behavior.lagSeconds = -1.0;
    CHECK_FALSE(piece.validate().has_value());
}

// ---- clearance: the mandate's section 8 --------------------------------------------------------

TEST_CASE("Clearance lifts the camera over the ground, and only upwards", "[camera][behavior]") {
    seq::Sequence piece = pieceWith(seq::CameraPreset::Chase, walker());
    seq::CameraBehavior& b = piece.shots.front().camera.behavior;
    b.offset = {0.0f, 0.5f, -4.0f}; // deliberately low, so a ground at +5 must lift it
    b.lagSeconds = 0.0;
    b.clearance = 1.0f;

    seq::NullLayerSink sink;

    // Two arms, because "the camera was lifted" means nothing without an arm in which it was not.
    seq::BakeOptions flat;
    flat.groundHeightAt = [](float, float) { return 0.0f; };
    const auto low = piece.bake(sink, flat);
    REQUIRE(low.has_value());

    seq::BakeOptions plateau;
    plateau.groundHeightAt = [](float, float) { return 5.0f; };
    const auto high = piece.bake(sink, plateau);
    REQUIRE(high.has_value());

    // `key3` writes one track with component -1 and a three-element value, so the height is
    // `value[1]` and not a track of its own.
    const auto cameraY = [](const seq::BakeResult& r) {
        float lowest = 1e9f;
        for (const auto& track : r.timeline.at("tracks")) {
            if (track.at("target").get<std::string>() != "camera/position") {
                continue;
            }
            for (const auto& key : track.at("keys")) {
                const auto& v = key.at("value");
                REQUIRE(v.is_array());
                REQUIRE(v.size() == 3);
                lowest = std::min(lowest, v[1].get<float>());
            }
        }
        return lowest;
    };

    const float flatY = cameraY(*low);
    const float liftedY = cameraY(*high);
    INFO("ground 0 -> camera y " << flatY << ";  ground 5 -> camera y " << liftedY);
    // The offset puts the eye 0.5 m up, which is below the 1 m clearance, so even the flat arm is
    // lifted -- to exactly 1. What separates the arms is that the plateau lifts it to 6, and the
    // flat arm is the control that says the lift came from the ground query rather than from the
    // offset.
    CHECK_THAT(flatY, Catch::Matchers::WithinAbs(1.0, 0.05));
    CHECK_THAT(liftedY, Catch::Matchers::WithinAbs(6.0, 0.05));

    // And with no ground to measure against, the bake says so rather than silently doing nothing.
    const auto blind = piece.bake(sink, seq::BakeOptions{});
    REQUIRE(blind.has_value());
    bool warned = false;
    for (const std::string& w : blind->warnings) {
        warned = warned || w.find("clearance") != std::string::npos;
    }
    CHECK(warned);
}

// ---- the visual fixture, checked numerically ---------------------------------------------------
//
// `examples/camera/behaviors` is the permanent scene the mandate's section 23 asks for: six shots,
// six seconds each, one per behaviour, so a render puts the differences side by side in time.
//
// A scene meant to make differences *visually* obvious is worth nothing if the differences are not
// there, and nobody watches a fixture on every commit. So the distinctions it exists to show are
// asserted here, in the terms a viewer would use: this camera does not move, that one travels with
// the performer, that one circles a subject that stands still, that one is standing where they are.

TEST_CASE("The camera-behaviour fixture shows what it claims to", "[camera][behavior][fixture]") {
    const auto fixture = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "camera" /
                         "behaviors.json";
    if (!std::filesystem::exists(fixture)) {
        SKIP("the camera behaviour fixture is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(fixture);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    const seq::Sequence& piece = engine.sequence();
    REQUIRE(piece.shots.size() == 6);
    const seq::Actor* walker = piece.actorNamed("walker");
    REQUIRE(walker != nullptr);

    // Where the camera and the performer are at a given second, through the real evaluation path.
    struct Sample {
        glm::vec3 eye{0.0f};
        glm::vec3 aim{0.0f};
        glm::vec3 them{0.0f};
    };
    const auto at = [&](double seconds) {
        engine.update(FrameTime{.renderTime = seconds, .deltaTime = 1.0 / 30.0, .frameIndex = 1});
        return Sample{.eye = comp->scene().camera.position,
                      .aim = comp->scene().camera.target,
                      .them = walker->positionAt(seconds)};
    };
    // Sampled inside each shot rather than at its edges: a cut lands one key a millisecond early
    // (see `kCutSeconds`), and a sample on a boundary would be asking which side of it won.
    const auto span = [&](std::size_t shot) {
        const seq::Shot& s = piece.shots[shot];
        return std::pair{at(s.startSeconds + 0.4), at(s.endSeconds() - 0.4)};
    };

    // 1. FOLLOW -- the eye is authored and static while the performer walks away from it.
    {
        const auto [a, b] = span(0);
        const float eye = glm::length(b.eye - a.eye);
        const float them = glm::length(b.them - a.them);
        INFO("follow: eye moved " << eye << " m, the performer moved " << them << " m");
        CHECK(them > 8.0f);                 // they really did walk; the shot is not vacuous
        CHECK_THAT(eye, Catch::Matchers::WithinAbs(0.0, 0.05));
        // And the aim went with them, or a camera that does nothing would pass the line above.
        CHECK(glm::length(b.aim - a.aim) > 5.0f);
    }

    // 2. CHASE -- the eye travels with them, and stays behind and above.
    {
        const auto [a, b] = span(1);
        const float eye = glm::length(b.eye - a.eye);
        const float them = glm::length(b.them - a.them);
        INFO("chase: eye moved " << eye << " m, the performer moved " << them << " m");
        CHECK(them > 4.0f);
        CHECK(eye > them * 0.5f);   // it went with them rather than watching them go
        CHECK(a.eye.y > a.them.y);  // and from above
        CHECK(b.eye.y > b.them.y);
    }

    // 3. ORBIT -- the camera circles. The performer is walking during this shot, so what is asserted
    //    is the property that makes an orbit an orbit: the distance to the subject is held while the
    //    direction to it changes.
    {
        const auto [a, b] = span(2);
        const float da = glm::length(glm::vec2(a.eye.x - a.them.x, a.eye.z - a.them.z));
        const float db = glm::length(glm::vec2(b.eye.x - b.them.x, b.eye.z - b.them.z));
        INFO("orbit: radius " << da << " m -> " << db << " m");
        CHECK_THAT(da, Catch::Matchers::WithinAbs(9.0, 0.6));
        CHECK_THAT(db, Catch::Matchers::WithinAbs(9.0, 0.6));
        // Gone round: the bearing to the subject has changed a long way.
        const glm::vec2 va = glm::normalize(glm::vec2(a.eye.x - a.them.x, a.eye.z - a.them.z));
        const glm::vec2 vb = glm::normalize(glm::vec2(b.eye.x - b.them.x, b.eye.z - b.them.z));
        CHECK(glm::dot(va, vb) < 0.6f);
    }

    // 4. POV -- the camera is standing where the performer is.
    {
        const auto [a, b] = span(3);
        const float gapA = glm::length(glm::vec2(a.eye.x - a.them.x, a.eye.z - a.them.z));
        const float gapB = glm::length(glm::vec2(b.eye.x - b.them.x, b.eye.z - b.them.z));
        INFO("pov: the eye is " << gapA << " m / " << gapB << " m from the performer, horizontally");
        CHECK(gapA < 0.5f);
        CHECK(gapB < 0.5f);
        CHECK(a.eye.y > a.them.y); // at their eyes, not their feet
        // Looking outward rather than at themselves: the aim is metres away, not on the eye.
        CHECK(glm::length(b.aim - b.eye) > 3.0f);
    }

    // 5 and 6 are moves rather than behaviours, and they are here so the fixture cannot quietly lose
    // them: Tracking travels, and Reveal ends further away than it began.
    {
        const auto [a, b] = span(4);
        CHECK(glm::length(b.eye - a.eye) > 2.0f);
    }
    {
        const auto [a, b] = span(5);
        const float near = glm::length(a.eye - a.them);
        const float far = glm::length(b.eye - b.them);
        INFO("reveal: " << near << " m -> " << far << " m");
        CHECK(far > near * 1.5f);
    }
}
