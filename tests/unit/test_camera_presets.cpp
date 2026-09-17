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

    // **The disputed one.** Follow is `ShotKind::Track` with the azimuth swinging 0.9 -> 0.55 at a
    // fixed 5 radii and a fixed elevation: a ~20 degree arc around the subject at constant height
    // and constant distance. It is a move, and the arc is 3.48 m on a 2 m subject.
    //
    // This is asserted rather than described because it is the fact the disagreement turned on. If
    // somebody later redefines Follow as a fixed-eye preset, this is the test that should fail and
    // be deliberately rewritten, rather than the change landing unnoticed.
    CHECK(presetTravel(seq::CameraPreset::Follow) > 1.0f);
}

TEST_CASE("Follow's move is an arc, not an approach", "[camera][preset]") {
    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 2.0f;
    const seq::ShotCamera cam = seq::cameraFromPreset(seq::CameraPreset::Follow, subject);
    const glm::vec3 a = cam.move.cameraAt(0.0f);
    const glm::vec3 b = cam.move.cameraAt(1.0f);

    // Same height and same distance from the subject at both ends: the camera circles rather than
    // closing in. This is what makes Follow read as "staying with" the subject -- and it is also
    // why a viewer watching the subject rather than the background sees very little.
    CHECK_THAT(a.y, Catch::Matchers::WithinAbs(b.y, 1e-3));
    CHECK_THAT(glm::length(a - subject.position),
               Catch::Matchers::WithinAbs(glm::length(b - subject.position), 1e-3));
    CHECK(glm::length(b - a) > 1.0f);
    // The aim barely moves, because the subject does not. In the app it is `lookAtActor` that makes
    // the aim swing -- see the engine test below.
    CHECK(glm::length(cam.move.targetAt(1.0f) - cam.move.targetAt(0.0f)) < 1.0f);
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

    // **The engine agrees with the pure function.** `Sequence::install` bakes `cameraAt(t)` straight
    // into `camera/position` keys, so there is no step between the preset and the screen that could
    // discard the arc. The code being read is not stale.
    CHECK(eyeTravel > 1.0f);

    // **And this is why it looks like it holds still.** The aim is dragged across forty metres by
    // the walker while the eye travels a few. The ratio is the whole explanation: a viewer watching
    // the actor sees an enormous pan and a small arc, and reads the pair as a stationary camera
    // turning to follow. Both observations are of the same frames.
    CHECK(aimTravel > eyeTravel * 3.0f);
}
