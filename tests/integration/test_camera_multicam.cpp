// Multiple cameras through the engine (ADR-245).
//
// `tests/unit/test_camera_rig.cpp` checks the collection, the director and the maths in isolation.
// This file checks the three things that can only be wrong once they are joined to the engine:
//
//   * an authored camera's channels are **ordinary parameters**, so the timeline animates a camera
//     with no camera-specific animation machinery anywhere;
//   * the picture actually changes at a cut, and an *inactive* camera cannot change it;
//   * the collection survives a save and a load, at both document levels -- the scene file it lives
//     in and the project that loads that scene (ADR-225).
//
// Every case is written so that the plausible wrong implementation fails it. The control arms are
// recorded in docs/decisions/ADR-245.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path testDir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path writeSilence(const std::filesystem::path& dir, const char* name, double seconds) {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::silence(static_cast<std::size_t>(rate * seconds));
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = dir / name;
    REQUIRE(file.writeWav(wav).has_value());
    return wav;
}

// A one-node scene with the main camera in free mode, which is what a directed project looks like.
std::filesystem::path writeScene(const std::filesystem::path& dir, const char* name) {
    const auto path = dir / name;
    std::ofstream out(path);
    out << R"({
        "format": "avgen-scene", "version": 1, "name": "stage",
        "camera": { "mode": 1, "position": [0, 2, 10], "target": [0, 1, 0], "fov": 50 },
        "nodes": [ { "name": "orb", "kind": "orb" } ]
    })";
    return path;
}

void runUntil(app::Engine& engine, FixedStepClock& clock, double seconds) {
    for (;;) {
        const auto time = engine.tick(clock);
        engine.update(time);
        if (time.renderTime >= seconds - 1e-9) {
            return;
        }
    }
}

scene::CameraDirection valleyAndMain(app::Engine& engine, scene::CameraId& wideOut) {
    scene::CameraDirection d = engine.composition()->cameraDirection();
    scene::CameraRig wide;
    wide.name = "Valley Wide";
    wide.position = glm::vec3(100.0f, 60.0f, 100.0f);
    wide.target = glm::vec3(0.0f, 5.0f, 0.0f);
    wide.fovDegrees = 62.0f;
    wide.focalLength = 24.0f;
    wideOut = d.nextId;
    d.addCamera(std::move(wide));
    return d;
}

} // namespace

TEST_CASE("A scene with no camera collection renders through the camera it always had",
          "[integration][multicam]") {
    // The backward-compatibility claim, checked rather than asserted in a comment. A composition
    // that has never heard of ADR-245 must place its camera with the code that was there before,
    // report the main camera as active, and write a scene document with no camera block in it.
    const auto dir = testDir("avgen_multicam_compat");
    const auto wav = writeSilence(dir, "silence.wav", 4.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());
    REQUIRE(engine.composition() != nullptr);

    FixedStepClock clock(60.0);
    runUntil(engine, clock, 0.5);

    const scene::Camera& cam = engine.composition()->scene().camera;
    CHECK_THAT(cam.position.x, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(cam.position.z, WithinAbs(10.0f, 1e-4f));
    const auto active = engine.activeCamera();
    CHECK(active.camera == scene::kMainCamera);
    CHECK(active.reason == scene::ActiveCameraReason::Default);
    CHECK_FALSE(engine.composition()->cameraDirection().directing());
    // And the document is unchanged: a scene file untouched by this system carries no new key.
    CHECK_FALSE(engine.composition()->toJson().contains("cameraDirection"));
    // No authored camera means no `cameras/` parameters. An inactive camera is cheap, but a camera
    // that does not exist should cost literally nothing.
    for (const params::IParameter* p : engine.params().ordered()) {
        CHECK_FALSE(p->path().starts_with("cameras/"));
    }
}

TEST_CASE("An authored camera is a parameter block the timeline can key", "[integration][multicam]") {
    const auto dir = testDir("avgen_multicam_params");
    const auto wav = writeSilence(dir, "silence.wav", 12.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());

    scene::CameraId wide = 0;
    REQUIRE(engine.setCameraDirection(valleyAndMain(engine, wide)).has_value());

    // The entire "camera animation system": the camera's channels are parameters, so the timeline
    // that already exists keys them. If this assertion needed a new subsystem, the design is wrong.
    REQUIRE(engine.params().find("cameras/valleywide/position") != nullptr);
    REQUIRE(engine.params().find("cameras/valleywide/target") != nullptr);
    REQUIRE(engine.params().find("cameras/valleywide/fov") != nullptr);
    REQUIRE(engine.params().find("cameras/valleywide/focalLength") != nullptr);

    CHECK_FALSE(engine.composition()->cameraIsAnimated(wide, engine.timeline()));
    params::Track track;
    track.target = "cameras/valleywide/position";
    track.addKey({.time = 0.0, .value = {100.0f, 60.0f, 100.0f}, .interp = params::KeyInterp::Linear});
    track.addKey({.time = 4.0, .value = {140.0f, 60.0f, 100.0f}, .interp = params::KeyInterp::Linear});
    engine.timeline().addTrack(track);
    engine.rebind();
    // "Static" and "animated" are not modes and not stored flags: they are a question about the
    // timeline, which is why they can never fall out of step with it.
    CHECK(engine.composition()->cameraIsAnimated(wide, engine.timeline()));
    CHECK_FALSE(engine.composition()->cameraIsAnimated(scene::kMainCamera, engine.timeline()));

    // Put the camera on screen for the whole piece and check that the keys reach the picture.
    scene::CameraDirection d = engine.composition()->cameraDirection();
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 0.0, .endSeconds = 12.0});
    REQUIRE(engine.setCameraDirection(std::move(d)).has_value());
    engine.rebind();

    FixedStepClock clock(50.0);
    runUntil(engine, clock, 2.0);
    CHECK(engine.activeCamera().camera == wide);
    CHECK_THAT(engine.composition()->scene().camera.position.x, WithinAbs(120.0f, 0.5f));
    runUntil(engine, clock, 4.0);
    CHECK_THAT(engine.composition()->scene().camera.position.x, WithinAbs(140.0f, 0.5f));
}

TEST_CASE("A cut changes the picture, and an inactive camera cannot", "[integration][multicam]") {
    const auto dir = testDir("avgen_multicam_cut");
    const auto wav = writeSilence(dir, "silence.wav", 12.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());
    scene::CameraId wide = 0;
    scene::CameraDirection d = valleyAndMain(engine, wide);
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 3.0, .endSeconds = 6.0});
    REQUIRE(engine.setCameraDirection(std::move(d)).has_value());

    FixedStepClock clock(50.0);
    runUntil(engine, clock, 2.0);
    CHECK(engine.activeCamera().camera == scene::kMainCamera);
    CHECK_THAT(engine.composition()->scene().camera.position.z, WithinAbs(10.0f, 1e-3f));

    // While the main camera has the frame, moving the authored one must move nothing. This is the
    // "inactive cameras are cheap" claim expressed as behaviour rather than as a timing: a camera
    // that is not on screen is not evaluated at all, so its channels cannot reach the picture.
    auto* wideParam = engine.params().find("cameras/valleywide/position");
    REQUIRE(wideParam != nullptr);
    wideParam->setBaseComponent(0, -900.0f);
    runUntil(engine, clock, 2.5);
    CHECK_THAT(engine.composition()->scene().camera.position.x, WithinAbs(0.0f, 1e-3f));

    runUntil(engine, clock, 4.0);
    CHECK(engine.activeCamera().camera == wide);
    CHECK(engine.activeCamera().reason == scene::ActiveCameraReason::Shot);
    CHECK_THAT(engine.composition()->scene().camera.position.x, WithinAbs(-900.0f, 1.0f));
    // The camera's own field of view, not the main camera's.
    CHECK_THAT(glm::degrees(engine.composition()->scene().camera.fovYRadians), WithinAbs(62.0f, 0.1f));
    // And its optical identity reached the physical lens (ADR-037), which is what makes depth of
    // field agree with the picture rather than only the framing.
    CHECK_THAT(engine.lens().focalLength, WithinAbs(24.0f, 1e-3f));

    runUntil(engine, clock, 7.0);
    CHECK(engine.activeCamera().camera == scene::kMainCamera);
    CHECK_THAT(engine.composition()->scene().camera.position.z, WithinAbs(10.0f, 1e-3f));
}

TEST_CASE("A blend moves the picture between two cameras", "[integration][multicam]") {
    const auto dir = testDir("avgen_multicam_blend");
    const auto wav = writeSilence(dir, "silence.wav", 12.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());
    scene::CameraId wide = 0;
    scene::CameraDirection d = valleyAndMain(engine, wide);
    d.shots.push_back(scene::CameraShot{.camera = wide,
                                        .startSeconds = 4.0,
                                        .endSeconds = 10.0,
                                        .transition = scene::ShotTransition::Blend,
                                        .blendSeconds = 2.0});
    REQUIRE(engine.setCameraDirection(std::move(d)).has_value());

    FixedStepClock clock(50.0);
    runUntil(engine, clock, 3.5);
    const float before = engine.composition()->scene().camera.position.x;
    runUntil(engine, clock, 5.0);
    const float half = engine.composition()->scene().camera.position.x;
    runUntil(engine, clock, 7.0);
    const float after = engine.composition()->scene().camera.position.x;

    CHECK_THAT(before, WithinAbs(0.0f, 1e-3f));    // the main camera
    CHECK_THAT(half, WithinAbs(50.0f, 1.0f));      // halfway to the authored one
    CHECK_THAT(after, WithinAbs(100.0f, 1e-3f));   // arrived
    // A blend that ends is a blend that costs nothing afterwards.
    CHECK_FALSE(engine.activeCamera().blending());
}

TEST_CASE("A camera collection survives the scene document and the project", "[integration][multicam]") {
    // ADR-225, at both levels. The cameras live in the scene file (a shot names a camera by an id
    // that only means something inside the world it was composed for); the project reaches them by
    // loading that scene.
    const auto dir = testDir("avgen_multicam_roundtrip");
    const auto wav = writeSilence(dir, "silence.wav", 12.0);
    const auto scenePath = writeScene(dir, "stage.json");

    scene::CameraId wide = 0;
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(wav).has_value());
        REQUIRE(engine.loadFile(scenePath).has_value());
        scene::CameraDirection d = valleyAndMain(engine, wide);
        scene::CameraRig ufo;
        ufo.name = "UFO Watch";
        ufo.eventScenario = "abduction";
        ufo.eventLeadSeconds = 0.35;
        ufo.priority = 5;
        ufo.autoDirectorEligible = true;
        d.addCamera(std::move(ufo));
        d.shots.push_back(scene::CameraShot{.camera = wide,
                                            .startSeconds = 1.0,
                                            .endSeconds = 5.0,
                                            .transition = scene::ShotTransition::Blend,
                                            .blendSeconds = 0.75,
                                            .label = "opening"});
        REQUIRE(engine.setCameraDirection(std::move(d)).has_value());
        // Move the camera through its parameter, which is where a viewport drag would put it.
        engine.params().find("cameras/valleywide/position")->setBaseComponent(1, 77.0f);
        REQUIRE(engine.saveComposition(scenePath).has_value());
        REQUIRE(engine.saveProject(dir / "piece.json").has_value());
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(dir / "piece.json").has_value());
        REQUIRE(engine.composition() != nullptr);
        const scene::CameraDirection& d = engine.composition()->cameraDirection();
        REQUIRE(d.cameras.size() == 3);
        REQUIRE(d.shots.size() == 1);
        const scene::CameraRig* valley = d.find(wide);
        REQUIRE(valley != nullptr);
        CHECK(valley->name == "Valley Wide");
        CHECK(valley->focalLength == 24.0f);
        const scene::CameraRig* ufo = d.findByName("UFO Watch");
        REQUIRE(ufo != nullptr);
        CHECK(ufo->eventScenario == "abduction");
        CHECK(ufo->eventLeadSeconds == 0.35);
        CHECK(ufo->priority == 5);
        CHECK(d.shots.front().transition == scene::ShotTransition::Blend);
        CHECK(d.shots.front().label == "opening");
        // The channels come back registered, and carrying the value that was saved -- a camera
        // whose parameters reappear at their defaults has not been kept, it has been re-created.
        auto* pos = engine.params().find("cameras/valleywide/position");
        REQUIRE(pos != nullptr);
        CHECK_THAT(pos->baseComponent(1), WithinAbs(77.0, 1e-3));
    }
}

TEST_CASE("A deleted camera takes its parameters and its automation with it",
          "[integration][multicam]") {
    // ADR-242: a target nobody reads is not a feature. Parameters left behind would be written into
    // the project for ever and tracks left behind would sit unbound, driving nothing.
    const auto dir = testDir("avgen_multicam_delete");
    const auto wav = writeSilence(dir, "silence.wav", 8.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());
    scene::CameraId wide = 0;
    REQUIRE(engine.setCameraDirection(valleyAndMain(engine, wide)).has_value());
    params::Track track;
    track.target = "cameras/valleywide/fov";
    track.addKey({.time = 0.0, .value = {40.0f}});
    engine.timeline().addTrack(track);
    engine.rebind();
    REQUIRE(engine.params().find("cameras/valleywide/fov") != nullptr);
    REQUIRE(engine.timeline().findTrack("cameras/valleywide/fov") != nullptr);

    scene::CameraDirection d = engine.composition()->cameraDirection();
    REQUIRE(d.removeCamera(wide));
    REQUIRE(engine.setCameraDirection(std::move(d)).has_value());

    CHECK(engine.params().find("cameras/valleywide/fov") == nullptr);
    CHECK(engine.params().find("cameras/valleywide/position") == nullptr);
    CHECK(engine.timeline().findTrack("cameras/valleywide/fov") == nullptr);
    CHECK(engine.timeline().unboundTargets().empty());
    // The main camera is still there and still has the frame.
    CHECK(engine.activeCamera().camera == scene::kMainCamera);
}

TEST_CASE("An invalid camera collection is refused whole", "[integration][multicam]") {
    const auto dir = testDir("avgen_multicam_refuse");
    const auto wav = writeSilence(dir, "silence.wav", 4.0);
    const auto scenePath = writeScene(dir, "stage.json");

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadFile(scenePath).has_value());
    scene::CameraId wide = 0;
    REQUIRE(engine.setCameraDirection(valleyAndMain(engine, wide)).has_value());

    scene::CameraDirection bad = engine.composition()->cameraDirection();
    bad.shots.push_back(scene::CameraShot{.camera = 4242, .startSeconds = 0.0, .endSeconds = 1.0});
    CHECK_FALSE(engine.setCameraDirection(bad).has_value());
    // Nothing changed on the refusal, the parameters included.
    CHECK(engine.composition()->cameraDirection().shots.empty());
    CHECK(engine.params().find("cameras/valleywide/position") != nullptr);
}

// ---- what inactive cameras cost (multicam spec section 24) ------------------------------------
//
// Hidden by default (`[.bench]`) because it is a measurement, not an assertion about behaviour. Run
// it with `tools/gpu-lock.sh ./build/release/tests/avgen_tests "[cambench]"`; ADR-170 is why, even
// for a CPU-only measurement -- a number taken on a contended machine is not evidence.
//
// The structural claim it accompanies, which does not depend on a clock at all: an authored camera
// costs exactly seven parameters, and the number of cameras *evaluated* per frame is one, or two
// while a blend is running, whatever the collection holds.
TEST_CASE("Inactive cameras cost their parameters and nothing else", "[.bench][cambench][multicam]") {
    const auto dir = testDir("avgen_multicam_bench");
    const auto wav = writeSilence(dir, "silence.wav", 4.0);
    const auto scenePath = writeScene(dir, "stage.json");

    for (const int count : {1, 5, 10, 25, 50}) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(wav).has_value());
        REQUIRE(engine.loadFile(scenePath).has_value());
        const std::size_t paramsBefore = engine.params().size();
        scene::CameraDirection d = engine.composition()->cameraDirection();
        for (int i = 0; i < count; ++i) {
            scene::CameraRig rig;
            rig.name = "Camera " + std::to_string(i);
            rig.position = glm::vec3(static_cast<float>(i) * 10.0f, 20.0f, 60.0f);
            d.addCamera(std::move(rig));
        }
        REQUIRE(engine.setCameraDirection(std::move(d)).has_value());
        const std::size_t added = engine.params().size() - paramsBefore;
        CHECK(added == static_cast<std::size_t>(count) * 7);

        FixedStepClock clock(60.0);
        runUntil(engine, clock, 0.5); // warm
        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 600;
        for (int f = 0; f < kFrames; ++f) {
            const auto time = engine.tick(clock);
            engine.update(time);
        }
        const double micros =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
            kFrames;
        WARN("cameras=" << count << " parameters=+" << added << " update=" << micros << " us/frame");
    }
}
