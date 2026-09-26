// A continuous take owns the frame (ADR-892).
//
// ADR-891 made the Auto-director's own take join across its shot boundaries, and left ADR-245's
// camera precedence alone: a locked authored shot beats an event camera, which beats the director.
// On the multicam film that still gave 18 camera changes in Continuous mode -- the authored Valley
// Wide shot at 0-7 s and 26-31 s, and the UFO Watch event camera at each of the ten abductions.
//
// The owner's ruling (25 Sep): in Continuous mode the continuous take owns the frame. No authored
// shot and no event camera takes it. The authored shots are not deleted -- they are ignored while a
// continuous take is in force -- so re-directing in Edited sequence or Song Mode brings back exactly
// the film ADR-245 describes, and the second case below proves that it does.
//
// The measure is ADR-891's, widened from "the director's own take" to the whole film: at 30 fps,
// every consecutive pair of frames turns the view less than 20 degrees and moves the eye less than
// 5 m, and no frame is claimed by a shot or an event.

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
#include <map>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::size_t authoredCameraShots(const app::Engine& engine) {
    const auto& shots = engine.composition()->cameraDirection().shots;
    return static_cast<std::size_t>(std::count_if(shots.begin(), shots.end(), [](const scene::CameraShot& s) {
        return s.origin == scene::CameraShot::Origin::Authored;
    }));
}

struct FilmReading {
    std::size_t frames = 0;
    std::size_t cameraChanges = 0;                  // the live camera differs from the frame before
    std::map<std::string, std::size_t> claimFrames; // "<camera> (<reason>)" -> frames it held
    std::size_t eventFrames = 0;                    // frames in which the director could see an event
    std::vector<std::string> snaps;                 // frame pairs over the turn or move limit
};

// Plays the whole film through the Engine at 30 fps and reads every frame.
FilmReading playTheFilm(app::Engine& engine) {
    constexpr double kFps = 30.0;
    constexpr float kMaxTurnDegrees = 20.0f;
    constexpr float kMaxJumpMetres = 5.0f;
    const auto frames = static_cast<std::uint64_t>(engine.durationSeconds() * kFps);
    FilmReading out;
    scene::CameraId previousCamera = scene::kNoCamera;
    glm::vec3 previousEye{0.0f};
    glm::vec3 previousDir{0.0f, 0.0f, 1.0f};
    for (std::uint64_t f = 0; f <= frames; ++f) {
        const double t = static_cast<double>(f) / kFps;
        engine.update(FrameTime{t, f == 0 ? 0.0 : 1.0 / kFps, f});
        const scene::ActiveCameraState active = engine.composition()->activeCamera();
        const glm::vec3 eye = engine.scene().camera.position;
        const glm::vec3 dir = glm::normalize(engine.scene().camera.target - eye);
        ++out.frames;
        ++out.claimFrames[fmt::format("{} ({})", active.name, scene::activeCameraReasonName(active.reason))];
        const auto spans = engine.composition()->cameraEventSpans();
        if (std::ranges::any_of(spans, [&](const scene::CameraEventSpan& s) {
                return s.startSeconds <= t && (s.endSeconds <= s.startSeconds || t <= s.endSeconds);
            })) {
            ++out.eventFrames;
        }
        if (f > 0) {
            if (active.camera != previousCamera) {
                ++out.cameraChanges;
            }
            const float turn = glm::degrees(std::acos(std::clamp(glm::dot(dir, previousDir), -1.0f, 1.0f)));
            const float jump = glm::length(eye - previousEye);
            if (turn > kMaxTurnDegrees || jump > kMaxJumpMetres) {
                out.snaps.push_back(fmt::format("{:.2f} s: turned {:.1f} deg, moved {:.2f} m on {}", t, turn,
                                                jump, active.name));
            }
        }
        previousCamera = active.camera;
        previousEye = eye;
        previousDir = dir;
    }
    return out;
}

std::size_t framesHeldBy(const FilmReading& r, const std::string& claim) {
    const auto it = r.claimFrames.find(claim);
    return it == r.claimFrames.end() ? 0 : it->second;
}

std::string describe(const FilmReading& r) {
    std::string s = fmt::format("frames {}, camera changes {}, event frames {}", r.frames, r.cameraChanges,
                                r.eventFrames);
    for (const auto& [claim, n] : r.claimFrames) {
        s += fmt::format("\n  {}: {} frames", claim, n);
    }
    for (std::size_t i = 0; i < r.snaps.size() && i < 25; ++i) {
        s += "\n  snap " + r.snaps[i];
    }
    return s;
}

// The owner's route, as ADR-891's test takes it: the project is saved in Song Mode and directed
// there, and then the panel's radio button re-directs in `mode`.
void directAfterSong(app::Engine& engine, app::DirectorMode mode) {
    app::AutoDirectorSettings settings = engine.autoDirector();
    REQUIRE(settings.mode == app::DirectorMode::Song);
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    settings.mode = mode;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    engine.autoDirector() = settings;
}

} // namespace

TEST_CASE("Continuous shot owns the frame on the multicam film: no authored or event camera takes it",
          "[director][camera][continuous][adr892][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(testsupport::glowmereBenchmarkProject()).has_value());
    const std::size_t authored = authoredCameraShots(engine);
    REQUIRE(authored == 3); // Valley Wide twice and the hero shot, all locked

    directAfterSong(engine, app::DirectorMode::ContinuousShot);
    // Ignored, not deleted: the author's shots are still on the track.
    CHECK(authoredCameraShots(engine) == authored);

    const FilmReading film = playTheFilm(engine);
    INFO(describe(film));
    // The film is the length it was, and its abductions still ran -- a measure over a film with no
    // events in it would prove nothing about the event camera.
    CHECK(film.frames > 6000);
    CHECK(film.eventFrames > 300);
    CHECK(film.cameraChanges == 0);
    // Every frame is the director's take: the main camera, by default, never a shot or an event.
    CHECK(film.claimFrames.size() == 1);
    CHECK(film.claimFrames.begin()->first.ends_with("(default)"));
    CHECK(film.snaps.empty());
}

TEST_CASE("Edited sequence keeps ADR-245's precedence on the same film: the authored and event cameras cut",
          "[director][camera][continuous][adr892][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(testsupport::glowmereBenchmarkProject()).has_value());
    directAfterSong(engine, app::DirectorMode::EditedSequence);

    const FilmReading film = playTheFilm(engine);
    INFO(describe(film));
    CHECK(framesHeldBy(film, "Valley Wide (shot)") > 250); // 0-7 s and 26-31 s, less the blend
    CHECK(framesHeldBy(film, "UFO Watch (event)") > 300);
    // ADR-891 counted 18 changes from these two cameras; the director's edit adds none of its own
    // (an Edited cut stays on the main camera).
    CHECK(film.cameraChanges >= 18);
}

TEST_CASE("the continuous take's ownership of the frame is a fact about the cut: set, parked, saved, cleared",
          "[director][camera][continuous][adr892][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(testsupport::glowmereBenchmarkProject()).has_value());
    scene::Composition& comp = *engine.composition();
    // A project saved before ADR-892 -- this one, in Song Mode -- owns nothing.
    CHECK_FALSE(comp.continuousTake());

    app::AutoDirectorSettings settings = engine.autoDirector();
    settings.mode = app::DirectorMode::ContinuousShot;
    REQUIRE(app::directEngine(engine, comp.heroes(), settings).has_value());
    CHECK(engine.composition()->continuousTake());

    // A scrub into an abduction lands on the take, not on UFO Watch: the answer is a function of
    // the cut, with nothing a play accumulates and a seek could miss.
    engine.seekSeconds(16.0);
    engine.update(FrameTime{16.0, 0.0, 0});
    CHECK(engine.composition()->activeCamera().camera == scene::kMainCamera);
    CHECK(engine.composition()->activeCamera().reason == scene::ActiveCameraReason::Default);

    // It rides in the project, or a render of the saved film would cut where the window did not.
    testsupport::ScratchDir dir("continuous_owns_frame");
    {
        auto trip = testsupport::saveAndReload(engine, dir / "continuous.json");
        REQUIRE(trip.has_value());
        CHECK(trip->saved.value("cameraContinuousTake", false));
        CHECK(trip->reloaded->composition()->continuousTake());
    }

    // Handing the camera back parks it with the rest of the cut; Resume brings it back.
    app::DirectorState state;
    app::releaseDirectedCamera(engine, state);
    CHECK_FALSE(engine.composition()->continuousTake());
    CHECK(engine.parkedCut().continuousTake);
    {
        auto trip = testsupport::saveAndReload(engine, dir / "parked.json");
        REQUIRE(trip.has_value());
        CHECK_FALSE(trip->reloaded->composition()->continuousTake());
        CHECK(trip->reloaded->parkedCut().continuousTake);
    }
    REQUIRE(app::resumeDirectedCamera(engine, state).has_value());
    CHECK(engine.composition()->continuousTake());

    // Directing in another mode clears it, and the saved file says nothing about it.
    settings.mode = app::DirectorMode::EditedSequence;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes(), settings).has_value());
    CHECK_FALSE(engine.composition()->continuousTake());
    {
        auto trip = testsupport::saveAndReload(engine, dir / "edited.json");
        REQUIRE(trip.has_value());
        CHECK_FALSE(trip->saved.contains("cameraContinuousTake"));
        CHECK_FALSE(trip->reloaded->composition()->continuousTake());
    }
}
