// A Continuous shot is one uninterrupted take, on the film the owner reported it cutting (ADR-891).
//
// The report, 25 Sep: "if I change glowmere-valley-2-multicam to Continuous shot in the Auto-director
// I am getting edits instead of a continuous shot." Played through the Engine, the film cut from
// three sources:
//
//   1. The Auto-director's own take. Its follow offset (ADR-158: how far a shot's hero has walked
//      since the cut) was dropped to zero at every shot boundary, because a boundary was assumed
//      to be a cut. In a continuous take it is a join, and the aim swung 25-81 degrees in one frame
//      with the eye standing still, seven times in the film (11.83 s, 23.67 s, 112.27 s ...).
//   2. Song Mode's camera track. Choosing Continuous shot after Song re-baked the framing and left
//      Song's `Directed` camera shots on the track, locked, so the film kept cutting between cameras
//      on Song's schedule.
//   3. The authored camera track (Valley Wide 0-7 s and 26-31 s) and the UFO Watch event camera,
//      which takes the frame at each of the ten abductions. ADR-891 left ADR-245's precedence
//      alone, so this test measures only the director's own take. ADR-892 (the owner's answer)
//      then gave a continuous take the whole frame; `test_continuous_owns_frame.cpp` measures that.
//
// The measure: play the whole film at 30 fps. Across every pair of consecutive frames in which the
// main camera (the Auto-director's) is live in both, not blending, and under the same claim, the
// view direction turns less than 20 degrees and the eye moves less than 5 m. The largest smooth
// turn left in the take is 14.3 degrees a frame (a baked pan at 61.1 s); the smallest snap the fix
// removed was 25.2 degrees.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"

#include "support/project_assets.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::size_t directedCameraShots(const app::Engine& engine) {
    const auto& shots = engine.composition()->cameraDirection().shots;
    return static_cast<std::size_t>(std::count_if(shots.begin(), shots.end(), [](const scene::CameraShot& s) {
        return s.origin == scene::CameraShot::Origin::Directed;
    }));
}

struct Snap {
    double seconds = 0.0;
    float turnDegrees = 0.0f;
    float jumpMetres = 0.0f;
};

// Every frame-to-frame discontinuity in the Auto-director's own take (see the header for the measure).
std::vector<Snap> snapsInTheDirectorsTake(app::Engine& engine, std::size_t& framesCompared) {
    constexpr double kFps = 30.0;
    constexpr float kMaxTurnDegrees = 20.0f;
    constexpr float kMaxJumpMetres = 5.0f;
    const auto frames = static_cast<std::uint64_t>(engine.durationSeconds() * kFps);
    std::vector<Snap> snaps;
    bool havePrevious = false;
    scene::ActiveCameraState previous;
    glm::vec3 previousEye{0.0f};
    glm::vec3 previousDir{0.0f, 0.0f, 1.0f};
    for (std::uint64_t f = 0; f <= frames; ++f) {
        engine.update(FrameTime{static_cast<double>(f) / kFps, f == 0 ? 0.0 : 1.0 / kFps, f});
        const scene::ActiveCameraState active = engine.composition()->activeCamera();
        const glm::vec3 eye = engine.scene().camera.position;
        const glm::vec3 dir = glm::normalize(engine.scene().camera.target - eye);
        const bool directorsTake = active.camera == scene::kMainCamera && !active.blending();
        if (havePrevious && directorsTake && previous.camera == scene::kMainCamera && !previous.blending() &&
            previous.reason == active.reason && previous.sinceSeconds == active.sinceSeconds) {
            ++framesCompared;
            const float turn = glm::degrees(std::acos(std::clamp(glm::dot(dir, previousDir), -1.0f, 1.0f)));
            const float jump = glm::length(eye - previousEye);
            if (turn > kMaxTurnDegrees || jump > kMaxJumpMetres) {
                snaps.push_back(Snap{static_cast<double>(f) / kFps, turn, jump});
            }
        }
        havePrevious = true;
        previous = active;
        previousEye = eye;
        previousDir = dir;
    }
    return snaps;
}

} // namespace

TEST_CASE("Continuous shot on the multicam film is one take: no Song cuts left, no snaps in the director's take",
          "[director][camera][continuous][adr891][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(testsupport::glowmereBenchmarkProject()).has_value());

    // The owner's route: the project is saved in Song Mode, directed, and then Continuous shot is
    // chosen. The panel's radio button sets the mode on the settings and re-directs with them
    // (`Application::directCameraFromTrack` -> `directEngine`), which is what this calls.
    app::AutoDirectorSettings settings = engine.autoDirector();
    REQUIRE(settings.mode == app::DirectorMode::Song);
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    REQUIRE(directedCameraShots(engine) > 0); // the control: Song did cut between cameras

    settings.mode = app::DirectorMode::ContinuousShot;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    CHECK(directedCameraShots(engine) == 0);

    std::size_t compared = 0;
    const std::vector<Snap> snaps = snapsInTheDirectorsTake(engine, compared);
    std::string listed;
    for (const Snap& s : snaps) {
        listed += fmt::format("\n  {:.2f} s: turned {:.1f} deg, moved {:.2f} m in one frame", s.seconds,
                              s.turnDegrees, s.jumpMetres);
    }
    INFO("frame pairs compared in the director's take: " << compared << "; snaps:" << listed);
    // At ADR-891 about half the film was the director's (3,168 of 6,788 frame pairs), the rest
    // claimed by the authored shots and UFO Watch; since ADR-892 it is all of it. A measure over
    // nothing would prove nothing.
    CHECK(compared > 2500);
    CHECK(snaps.empty());
}

TEST_CASE("only a continuous take joins its follow offsets; an edited sequence still cuts them",
          "[director][camera][continuous][adr891][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(testsupport::glowmereBenchmarkProject()).has_value());
    app::AutoDirectorSettings settings = engine.autoDirector();

    const auto joins = [&] {
        std::size_t n = 0;
        for (const scene::AimFollow& a : engine.composition()->aimFollow()) {
            n += (a.joinInSeconds > 0.0 ? 1 : 0) + (a.joinOutSeconds > 0.0 ? 1 : 0);
        }
        return n;
    };
    settings.mode = app::DirectorMode::EditedSequence;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    REQUIRE_FALSE(engine.composition()->aimFollow().empty());
    CHECK(joins() == 0);

    settings.mode = app::DirectorMode::ContinuousShot;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    CHECK(joins() > 0);

    // The joins ride in the project, or a render of the saved film would snap where playback did not.
    testsupport::ScratchDir dir("continuous_joins");
    auto trip = testsupport::saveAndReload(engine, dir / "joined.json");
    REQUIRE(trip.has_value());
    CHECK(trip->reloaded->composition()->aimFollow() == engine.composition()->aimFollow());
}
