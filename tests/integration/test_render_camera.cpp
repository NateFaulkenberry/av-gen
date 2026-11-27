// ADR-2231: a named camera is a reproducible input. Before it, a lab case's `camera` was parsed, saved and applied by
// nothing (15 shipped overrides reproduced through the fixture's own camera) and `--live-profile --camera` recorded a
// name it never selected. These cases hold the three halves together: the value (`RenderCamera`, its flags), the
// engine applying it (`Engine::pinCamera`), and the lab case carrying it to both.
//
// Each engine case has a control: the same engine with no pin shows the director's camera, so a pin that did nothing
// cannot pass.

#include "app/engine.hpp"
#include "app/render_settings.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "labs/case.hpp"
#include "labs/lab.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

fs::path testDir(const char* name) {
    const auto dir = fs::temp_directory_path() / name;
    fs::create_directories(dir);
    return dir;
}

fs::path writeSilence(const fs::path& dir, double seconds) {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::silence(static_cast<std::size_t>(rate * seconds));
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = dir / "silence.wav";
    REQUIRE(file.writeWav(wav).has_value());
    return wav;
}

fs::path writeScene(const fs::path& dir) {
    const auto path = dir / "stage.json";
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

// The main camera plus "Valley Wide" at (100, 60, 100), and a shot giving the frame to the MAIN camera from 0 to 12 s:
// a pin has to beat the director, not merely fill an empty cut.
struct Stage {
    app::Engine engine{app::EngineMode::Offline};
    scene::CameraId wide = 0;
};

void build(Stage& s, const char* dirName) {
    const auto dir = testDir(dirName);
    REQUIRE(s.engine.loadAudio(writeSilence(dir, 12.0)).has_value());
    REQUIRE(s.engine.loadFile(writeScene(dir)).has_value());
    REQUIRE(s.engine.composition() != nullptr);
    scene::CameraDirection d = s.engine.composition()->cameraDirection();
    scene::CameraRig wide;
    wide.name = "Valley Wide";
    wide.position = glm::vec3(100.0f, 60.0f, 100.0f);
    wide.target = glm::vec3(0.0f, 5.0f, 0.0f);
    wide.fovDegrees = 62.0f;
    s.wide = d.nextId;
    d.addCamera(std::move(wide));
    d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera, .startSeconds = 0.0, .endSeconds = 12.0});
    REQUIRE(s.engine.setCameraDirection(std::move(d)).has_value());
}

} // namespace

TEST_CASE("render camera: the pose flag parses, refuses, describes and round-trips through its own flags",
          "[camera][adr2231]") {
    const auto p = app::parseCameraPose("1,2,3,0,1,0");
    REQUIRE(p.has_value());
    CHECK(p->kind == app::RenderCamera::Kind::Pose);
    CHECK(p->eye[2] == 3.0f);
    CHECK(p->aim[1] == 1.0f);
    CHECK(p->fovDegrees == 0.0f);
    const auto withFov = app::parseCameraPose("1,2,3,0,1,0,35");
    REQUIRE(withFov.has_value());
    CHECK(withFov->fovDegrees == 35.0f);
    // The flags it prints are the flags that parse back to it.
    const std::string flags = app::renderCameraFlags(*withFov);
    CHECK(flags == " --camera-pose 1,2,3,0,1,0,35");
    const auto again = app::parseCameraPose(flags.substr(std::string(" --camera-pose ").size()));
    REQUIRE(again.has_value());
    CHECK(*again == *withFov);

    CHECK_FALSE(app::parseCameraPose("1,2,3").has_value());
    CHECK_FALSE(app::parseCameraPose("1,2,3,4,5,x").has_value());
    CHECK_FALSE(app::parseCameraPose("1,2,3,1,2,3").has_value()); // no view
    CHECK_FALSE(app::parseCameraPose("1,2,3,0,0,0,180").has_value());

    CHECK(app::renderCameraFlags(app::RenderCamera{}).empty());
    CHECK(app::renderCameraFlags(app::RenderCamera::named("cam-overview")) == " --camera cam-overview");
    CHECK(app::renderCameraFlags(app::RenderCamera::named("Valley Wide")) == " --camera 'Valley Wide'");
    CHECK(app::describeRenderCamera(app::RenderCamera::named("Valley Wide")) == "camera 'Valley Wide'");
    CHECK(app::describeRenderCamera(app::RenderCamera{}) == "film");
    // Never a property of the project: a render block written with a pinned camera carries none.
    app::RenderSettings s;
    s.camera = app::RenderCamera::named("Valley Wide");
    CHECK(s.toJson().dump().find("Valley") == std::string::npos);
}

TEST_CASE("render camera: a named camera holds the frame against the director's shot, by name or slug",
          "[camera][adr2231][integration]") {
    Stage s;
    build(s, "avgen_render_camera_named");
    FixedStepClock clock(30.0);

    // The control: no pin, the shot gives the frame to the main camera.
    runUntil(s.engine, clock, 1.0);
    CHECK_THAT(s.engine.composition()->scene().camera.position.z, WithinAbs(10.0f, 1e-3f));

    REQUIRE(s.engine.pinCamera(app::RenderCamera::named("Valley Wide")).has_value());
    runUntil(s.engine, clock, 2.0);
    const scene::Camera& cam = s.engine.composition()->scene().camera;
    CHECK_THAT(cam.position.x, WithinAbs(100.0f, 1e-3f));
    CHECK_THAT(cam.position.y, WithinAbs(60.0f, 1e-3f));
    CHECK_THAT(glm::degrees(cam.fovYRadians), WithinAbs(62.0f, 0.1f));
    // The director's answer is unchanged: the pin is what the frame looks through, not a rewritten cut.
    CHECK(s.engine.activeCamera().camera == scene::kMainCamera);

    // A seek is the operation a render does most; the pin survives it.
    s.engine.seekSeconds(5.0);
    clock.restartAt(5.0);
    runUntil(s.engine, clock, 5.0);
    CHECK_THAT(s.engine.composition()->scene().camera.position.x, WithinAbs(100.0f, 1e-3f));

    // Film puts the director back.
    REQUIRE(s.engine.pinCamera(app::RenderCamera{}).has_value());
    runUntil(s.engine, clock, 5.5);
    CHECK_THAT(s.engine.composition()->scene().camera.position.z, WithinAbs(10.0f, 1e-3f));

    // By slug, the parameter-path name a script is likelier to have.
    REQUIRE(s.engine.pinCamera(app::RenderCamera::named("valleywide")).has_value());
    runUntil(s.engine, clock, 6.0);
    CHECK_THAT(s.engine.composition()->scene().camera.position.x, WithinAbs(100.0f, 1e-3f));
}

TEST_CASE("render camera: an unknown name is refused, lists the cameras and changes nothing", "[camera][adr2231]") {
    Stage s;
    build(s, "avgen_render_camera_unknown");
    const auto r = s.engine.pinCamera(app::RenderCamera::named("Nope"));
    REQUIRE_FALSE(r.has_value());
    INFO(r.error().message);
    CHECK(r.error().message.find("'Nope'") != std::string::npos);
    CHECK(r.error().message.find("'Valley Wide'") != std::string::npos);
    CHECK(s.engine.composition()->viewportView().showsFilm());
    const auto names = s.engine.cameraNames();
    REQUIRE(names.size() == 2);
    CHECK(names[1] == "Valley Wide");
}

TEST_CASE("render camera: a pose pins a fixed eye and aim, keeping the film's field of view unless it states one",
          "[camera][adr2231][integration]") {
    Stage s;
    build(s, "avgen_render_camera_pose");
    FixedStepClock clock(30.0);
    const float eye[3] = {7.0f, 3.0f, -4.0f};
    const float aim[3] = {0.0f, 1.0f, 0.0f};
    REQUIRE(s.engine.pinCamera(app::RenderCamera::pose(eye, aim)).has_value());
    runUntil(s.engine, clock, 1.0);
    const scene::Camera& cam = s.engine.composition()->scene().camera;
    CHECK_THAT(cam.position.x, WithinAbs(7.0f, 1e-4f));
    CHECK_THAT(cam.position.z, WithinAbs(-4.0f, 1e-4f));
    CHECK_THAT(cam.target.y, WithinAbs(1.0f, 1e-4f));
    CHECK_THAT(glm::degrees(cam.fovYRadians), WithinAbs(50.0f, 0.1f)); // the scene's own
    REQUIRE(s.engine.pinCamera(app::RenderCamera::pose(eye, aim, 30.0f)).has_value());
    runUntil(s.engine, clock, 1.5);
    CHECK_THAT(glm::degrees(s.engine.composition()->scene().camera.fovYRadians), WithinAbs(30.0f, 0.1f));
}

TEST_CASE("lab case: a camera is a name or an eye and an aim, and the reproduce command carries it",
          "[labs][case][adr2231]") {
    nlohmann::json doc = {{"lab", "volumetric"},       {"number", 301},  {"title", "t"},
                          {"question", "q"},           {"expectation", "e"},
                          {"fixture", "examples/labs/volumetric-atmosphere-lab.scene.json"},
                          {"timeSeconds", 0.5},        {"camera", "cam-overview"}};
    auto named = labs::caseFromJson(doc);
    REQUIRE(named.has_value());
    CHECK(named->cameraName == "cam-overview");
    CHECK_FALSE(named->hasCamera);
    CHECK(labs::toJson(*named)["camera"] == "cam-overview");
    const std::string cmd = labs::reproduceCommand(*named);
    INFO(cmd);
    CHECK(cmd.find(" --camera cam-overview") != std::string::npos);
    CHECK(cmd.find(" --render renders/labs/volumetric-301") != std::string::npos);

    doc["camera"] = {{"eye", {14.0, 6.0, 2.5}}, {"aim", {0.0, 1.0, 0.0}}};
    auto posed = labs::caseFromJson(doc);
    REQUIRE(posed.has_value());
    CHECK(posed->hasCamera);
    CHECK(labs::reproduceCommand(*posed).find(" --camera-pose 14,6,2.5,0,1,0") != std::string::npos);

    doc["camera"] = 3;
    CHECK_FALSE(labs::caseFromJson(doc).has_value());
    doc["camera"] = "";
    CHECK_FALSE(labs::caseFromJson(doc).has_value());
}

TEST_CASE("lab cases: every shipped case camera can be pinned on its own fixture", "[labs][case][adr2231]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The test the camera hole never had: a case camera that names a camera the fixture lacks, or whose fixture has no
    // composition to pin it on, fails here instead of reproducing the wrong frame in silence.
    const fs::path root(AVGEN_SOURCE_DIR);
    int withCamera = 0;
    std::set<std::string> fixturesChecked;
    for (const labs::LabDescriptor& lab : labs::labs()) {
        const fs::path file = root / labs::caseFilePath(lab.key);
        if (!fs::is_regular_file(file)) {
            continue;
        }
        const auto cases = labs::loadCases(file);
        REQUIRE(cases.has_value());
        for (const labs::LabCase& c : *cases) {
            if (c.cameraName.empty() && !c.hasCamera) {
                continue;
            }
            ++withCamera;
            INFO(c.lab << " case " << c.number << " on " << c.fixture);
            app::Engine engine(app::EngineMode::Offline);
            const auto loaded = engine.loadFile(root / c.fixture);
            INFO((loaded ? std::string() : loaded.error().message));
            REQUIRE(loaded.has_value());
            app::RenderCamera cam;
            if (!c.cameraName.empty()) {
                cam = app::RenderCamera::named(c.cameraName);
            } else {
                const float eye[3] = {c.eye.x, c.eye.y, c.eye.z};
                const float aim[3] = {c.aim.x, c.aim.y, c.aim.z};
                cam = app::RenderCamera::pose(eye, aim);
            }
            const auto pinned = engine.pinCamera(cam);
            INFO((pinned ? std::string() : pinned.error().message));
            CHECK(pinned.has_value());
            fixturesChecked.insert(c.fixture);
        }
    }
    // 15 at ADR-2231 (hdr 4, lighting 4, lod 4, shadow 3). Fewer means a case file lost its cameras or this scan
    // stopped seeing them -- a count is what tells "all passed" apart from "none were looked at".
    CHECK(withCamera >= 15);
    CHECK_FALSE(fixturesChecked.empty());
#endif
}
