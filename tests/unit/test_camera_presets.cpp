// What each camera preset does to the camera, measured rather than read.
//
// Written to settle a specific disagreement. A source audit concluded that the **Follow** preset
// moves the camera; the owner tested Follow in the running application and reported the camera
// holding still while its aim tracked the actor. A code reading and a person watching the screen
// cannot both be right, and re-reading the code is not how you find out which.
//
// Two levels, because they answer different questions:
//
//   * the **pure function** -- `cameraFromPreset` then `Shot::cameraAt(t)`, with no engine at all.
//     This is what the preset computes.
//   * the **running engine** -- a real project, a real bake, a real seek, reading
//     `scene().camera`. This is what the application does.
//
// If the two disagree, the code being read is stale relative to what runs, and that is the finding.
// If they agree, the explanation for the observation is somewhere else and these numbers say where.

#include "app/cinematic.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>

#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path project() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "night-shift.json";
}

// How far the camera travels across a preset's shot, in world units, against a subject of radius 2.
float presetTravel(seq::CameraPreset preset) {
    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 2.0f;
    subject.name = "hero";
    const seq::ShotCamera cam = seq::cameraFromPreset(preset, subject);
    return glm::length(cam.move.cameraAt(1.0f) - cam.move.cameraAt(0.0f));
}

} // namespace

// ---- the pure function -------------------------------------------------------------------------

TEST_CASE("Two presets hold the camera still and the rest move it", "[camera][preset]") {
    // The two that are compositions rather than moves. An isometric view that drifts stops being
    // isometric, and a top-down plan that slides stops being a plan.
    CHECK_THAT(presetTravel(seq::CameraPreset::Isometric), Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK_THAT(presetTravel(seq::CameraPreset::TopDown), Catch::Matchers::WithinAbs(0.0, 1e-4));

    // And the five that are moves. These are the measurements, not targets -- if a preset's values
    // are retuned these numbers move with them, and that is the test doing its job rather than
    // failing.
    CHECK(presetTravel(seq::CameraPreset::Close) > 1.0f);
    CHECK(presetTravel(seq::CameraPreset::Wide) > 1.0f);
    CHECK(presetTravel(seq::CameraPreset::Tracking) > 1.0f);
    CHECK(presetTravel(seq::CameraPreset::Reveal) > 1.0f);

    // **The disputed one, and now a third member of the still group.** Follow used to swing 0.9 ->
    // 0.55 -- a 20 degree arc, 3.48 m on a 2 m subject -- and nobody noticed, because with
    // `lookAtActor` set the aim is dragged across the world by the actor while the eye creeps, at a
    // constant distance and a constant height that hide it. The measured ratio was 11.5 to 1.
    //
    // The contract is that Follow's eye is authored and static, so the swing is now zero by default
    // and lives on as the shot inspector's `drift`. See
    // docs/investigations/follow-chase-discrepancy.md.
    CHECK_THAT(presetTravel(seq::CameraPreset::Follow), Catch::Matchers::WithinAbs(0.0, 1e-4));
}

TEST_CASE("Drift is what moves a held camera, and it is per shot", "[camera][preset]") {
    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 2.0f;

    seq::ShotCamera cam = seq::cameraFromPreset(seq::CameraPreset::Follow, subject);
    REQUIRE_THAT(glm::length(cam.move.cameraAt(1.0f) - cam.move.cameraAt(0.0f)),
                 Catch::Matchers::WithinAbs(0.0, 1e-4));

    // The control writes the *difference* onto the azimuth pair, leaving the start angle alone --
    // so "where the camera stands" and "how far it travels" stay separate decisions.
    const float start = cam.move.startAzimuth;
    cam.move.endAzimuth = cam.move.startAzimuth + glm::radians(20.0f);
    CHECK_THAT(cam.move.startAzimuth, Catch::Matchers::WithinAbs(start, 1e-6));

    // 20 degrees at 5 radii on a 2 m subject is the arc Follow used to ship with, so this is also
    // the check that the old behaviour is still reachable rather than deleted.
    const float travel = glm::length(cam.move.cameraAt(1.0f) - cam.move.cameraAt(0.0f));
    INFO("20 degrees of drift moves the eye " << travel << " m");
    CHECK(travel > 3.0f);
    CHECK(travel < 4.0f);

    // Still an arc: same height, same distance. Drift changes how far around, never how far away.
    const glm::vec3 a = cam.move.cameraAt(0.0f);
    const glm::vec3 b = cam.move.cameraAt(1.0f);
    CHECK_THAT(a.y, Catch::Matchers::WithinAbs(b.y, 1e-3));
    CHECK_THAT(glm::length(a - subject.position),
               Catch::Matchers::WithinAbs(glm::length(b - subject.position), 1e-3));
}

TEST_CASE("A moving preset's move is an arc, not an approach", "[camera][preset]") {
    // Tracking rather than Follow, now that Follow holds still: the property being checked is that
    // the azimuth presets circle the subject rather than closing on it, and Tracking is the one
    // whose whole point is that travel.
    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 2.0f;
    const seq::ShotCamera cam = seq::cameraFromPreset(seq::CameraPreset::Tracking, subject);
    const glm::vec3 a = cam.move.cameraAt(0.0f);
    const glm::vec3 b = cam.move.cameraAt(1.0f);

    CHECK_THAT(a.y, Catch::Matchers::WithinAbs(b.y, 1e-3));
    CHECK_THAT(glm::length(a - subject.position),
               Catch::Matchers::WithinAbs(glm::length(b - subject.position), 1e-3));
    CHECK(glm::length(b - a) > 1.0f);
}

// ---- the running engine ------------------------------------------------------------------------

TEST_CASE("The baked camera is the preset's camera, through a real seek", "[camera][preset]") {
    if (!std::filesystem::exists(project())) {
        SKIP("night-shift is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    REQUIRE_FALSE(comp->nodes().empty());
    const std::string node = comp->nodes().front()->name;

    // One shot on the Follow preset, and an actor walking forty metres across it -- which is the
    // case the owner was watching, and the one where the two motions are most easily confused.
    seq::Sequence piece;
    piece.name = "probe";
    seq::Actor actor;
    actor.id = "walker";
    actor.node = node;
    actor.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {-20.0f, 0.0f, 0.0f}});
    actor.keys.push_back(seq::ActorKey{.timeSeconds = 4.0, .position = {20.0f, 0.0f, 0.0f}});
    piece.actors.push_back(actor);

    app::FocalTarget subject;
    subject.position = {-20.0f, 0.0f, 0.0f}; // where the walker stands at the cut
    subject.radius = 2.0f;
    subject.name = "walker";

    seq::Shot shot;
    shot.name = "probe";
    shot.startSeconds = 0.0;
    shot.durationSeconds = 4.0;
    shot.camera = seq::cameraFromPreset(seq::CameraPreset::Follow, subject);
    shot.camera.lookAtActor = "walker"; // exactly what the panel sets when you pick a performer
    shot.camera.lookAtWeight = 1.0f;
    piece.shots.push_back(shot);

    auto installed = engine.setSequence(piece);
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());

    INFO("install: " << installed->trackCount << " track(s), " << installed->keyCount << " key(s)");
    REQUIRE(installed->trackCount > 0);

    // **Played, not seeked.** `Engine::seekSeconds` moves the clock; it does not re-derive the
    // scene. The parameters are applied and `scene().camera` is rebuilt inside `Engine::update`,
    // so a test that seeks and reads measures the frame it was already on -- which is how the first
    // version of this test "found" that the engine disagreed with the preset. It did not; the test
    // never advanced a frame. Recorded because it is exactly the mistake this file exists to catch
    // somebody else making.
    FixedStepClock clock(60.0);
    engine.seekSeconds(0.0);
    std::vector<glm::vec3> eyes;
    std::vector<glm::vec3> aims;
    for (int frame = 0; frame <= 240; ++frame) {
        engine.update(engine.tick(clock));
        if (frame % 60 == 0) {
            eyes.push_back(comp->scene().camera.position);
            aims.push_back(comp->scene().camera.target);
        }
    }
    REQUIRE(eyes.size() == 5);

    const float eyeTravel = glm::length(eyes.back() - eyes.front());
    const float aimTravel = glm::length(aims.back() - aims.front());
    INFO("eye travelled " << eyeTravel << " m, aim travelled " << aimTravel << " m");

    // **The contract, end to end.** Follow's eye is authored and static: it does not move because
    // the actor moved, and it does not move at all. This is the assertion that would have caught the
    // original discrepancy, and it runs through a real bake and 240 real frames rather than over the
    // pure function -- so it also proves `Sequence::install` does not introduce motion of its own.
    CHECK_THAT(eyeTravel, Catch::Matchers::WithinAbs(0.0, 1e-3));

    // And the aim goes the whole forty metres with the walker, which is the half of Follow that is
    // supposed to move. Without this the test above would pass on a camera that does nothing at all.
    CHECK(aimTravel > 30.0f);
}

TEST_CASE("Drift survives a bake, and it is the only thing that moves Follow's eye",
          "[camera][preset]") {
    if (!std::filesystem::exists(project())) {
        SKIP("night-shift is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    REQUIRE_FALSE(comp->nodes().empty());

    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 2.0f;

    // The same shot twice, differing only in drift. Two arms rather than one, because "the eye
    // moved" means nothing without an arm in which it did not (ADR-182) -- and the zero arm is the
    // control that says the motion came from drift and not from the bake.
    const auto eyeTravelWithDrift = [&](float degrees) {
        seq::Sequence piece;
        piece.name = "drift-probe";
        seq::Shot shot;
        shot.name = "probe";
        shot.startSeconds = 0.0;
        shot.durationSeconds = 4.0;
        shot.camera = seq::cameraFromPreset(seq::CameraPreset::Follow, subject);
        shot.camera.move.endAzimuth = shot.camera.move.startAzimuth + glm::radians(degrees);
        piece.shots.push_back(shot);
        REQUIRE(engine.setSequence(piece).has_value());

        FixedStepClock clock(60.0);
        engine.seekSeconds(0.0);
        glm::vec3 first{0.0f}, last{0.0f};
        for (int frame = 0; frame <= 240; ++frame) {
            engine.update(engine.tick(clock));
            if (frame == 0) {
                first = comp->scene().camera.position;
            }
            last = comp->scene().camera.position;
        }
        return glm::length(last - first);
    };

    const float still = eyeTravelWithDrift(0.0f);
    const float drifted = eyeTravelWithDrift(20.0f);
    INFO("0 deg -> " << still << " m, 20 deg -> " << drifted << " m");
    CHECK_THAT(still, Catch::Matchers::WithinAbs(0.0, 1e-3));
    CHECK(drifted > 3.0f);
}
