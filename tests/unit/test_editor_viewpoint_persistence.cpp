// What a save photographs when the editor is looking somewhere else (ADR-391).
//
// An app save photographs the run: it writes the parameters as they stand, which is how the
// multicam film's baked camera was lost the first time -- a viewport drag had rewritten
// `camera/position`, and Save wrote the result. The editor viewpoint exists so that a drag stops
// writing that value at all, and this file is the assertion, because "it does not write it" is
// exactly the kind of claim that is true of the code and false of the file on disk.
//
// **The persistence decision, and it is a decision** (ADR-225/ADR-350 ask for one either way).
// The editor viewpoint does *not* persist. It is not written to the project, not written to the
// scene, and a reload gets a composition whose viewport is the film's camera. Two reasons:
//
//   1. It is what makes the render invariant structural rather than careful. A `RenderJob`, an
//      offline run and a sequence render each build their own `Engine` from the file. If the file
//      cannot carry an editor viewpoint, no render can inherit one, and nothing has to remember to
//      clear it. The alternative -- serialise it and have every render path switch it off -- is one
//      forgotten path away from an editor's navigation reaching a deliverable.
//   2. It is editor state, like which panel is open and what is selected. The project is the film.
//
// The cost is real and is stated here rather than discovered: closing and reopening a project puts
// the canvas back on the film's camera, and a view somebody flew to is gone. If that turns out to
// be worth keeping, the place to keep it is the editor's own settings file beside the panel layout
// and the preview mode -- never the project.

#include "app/engine.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include "core/time.hpp"
#include "params/parameter_set.hpp"

using namespace avgen;

namespace {

// Somewhere absurd, and deliberately so: the assertion is that none of these six numbers reaches
// the file, and a pose near the film's camera would be indistinguishable from one that did.
constexpr glm::vec3 kAbsurdEye{-9414.0f, 6180.0f, 4242.0f};
constexpr glm::vec3 kAbsurdTarget{9999.0f, -777.0f, -1234.0f};

std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

glm::vec3 cameraPositionBase(app::Engine& engine) {
    const auto* p = engine.params().find("camera/position");
    REQUIRE(p != nullptr);
    return {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
}

} // namespace

TEST_CASE("A save with the editor viewpoint parked absurdly writes the film's camera",
          "[viewport][camera][project]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    const glm::vec3 film = cameraPositionBase(engine);

    comp->setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    scene::CameraPose pose;
    pose.position = kAbsurdEye;
    pose.target = kAbsurdTarget;
    comp->setEditorCamera(pose);

    // Played, not merely set: the frame has to have been drawn through the editor's viewpoint for
    // the test to be about what a real session saves. Before this line the override has not run.
    FrameTime t;
    t.renderTime = 1.0;
    t.deltaTime = 1.0 / 60.0;
    engine.setViewport(1920, 1080);
    engine.update(t);
    REQUIRE(glm::length(engine.scene().camera.position - kAbsurdEye) < 1e-3f);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "avgen-editor-viewpoint-save.json";
    REQUIRE(engine.saveProject(path).has_value());

    // The parameter the bug wrote, read back off the running engine and out of the file.
    CHECK(glm::length(cameraPositionBase(engine) - film) < 1e-4f);
    const std::string json = readAll(path);
    INFO("project is " << json.size() << " bytes");
    CHECK(json.find("-9414") == std::string::npos);
    CHECK(json.find("6180") == std::string::npos);
    CHECK(json.find("editorCamera") == std::string::npos);
    CHECK(json.find("viewportView") == std::string::npos);

    // And a reload comes back on the film, not on the editor's viewpoint -- the round trip the
    // decision above says must NOT happen, asserted so that adding serialisation quietly is a test
    // failure rather than a silent change to what a render does.
    app::Engine reopened(app::EngineMode::Offline);
    auto loaded = reopened.loadProject(path);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(reopened.composition() != nullptr);
    CHECK(reopened.composition()->viewportView().showsFilm());
    CHECK_FALSE(reopened.composition()->editorCameraSeeded());

    reopened.setViewport(1920, 1080);
    reopened.update(t);
    INFO("reloaded eye " << reopened.scene().camera.position.x);
    CHECK(glm::length(reopened.scene().camera.position - kAbsurdEye) > 1000.0f);

    std::filesystem::remove(path);
}

TEST_CASE("A saved composition carries no editor viewpoint either", "[viewport][camera][project]") {
    // The scene document is the other file a session writes, and the camera collection lives in it
    // (a shot names a rig by an id that only means something inside the scene it was composed for).
    // So it is the other place an editor pose could leak into a render, and the same assertion has
    // to be made about it rather than assumed from the project's.
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    comp->setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    scene::CameraPose pose;
    pose.position = kAbsurdEye;
    pose.target = kAbsurdTarget;
    comp->setEditorCamera(pose);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "avgen-editor-viewpoint-scene.json";
    REQUIRE(engine.saveComposition(path).has_value());
    const std::string json = readAll(path);
    INFO("scene is " << json.size() << " bytes");
    CHECK(json.find("-9414") == std::string::npos);
    CHECK(json.find("4242") == std::string::npos);
    // The control: the file really is a scene document with content in it, so the three `npos`
    // above are not three statements about an empty string.
    CHECK(json.find("camera") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("An offline engine renders the film whatever the editor did in another session",
          "[viewport][camera][project]") {
    // The invariant in its most direct form: the same project, rendered by two fresh engines, one
    // of which had an editor viewpoint parked absurdly *before the save*. The camera each frame is
    // what the renderer is handed, so comparing `Scene::camera` across the two runs compares what
    // would be drawn without needing a device.
    app::Engine authoring(app::EngineMode::Offline);
    authoring.newComposition();
    REQUIRE(authoring.composition() != nullptr);
    const std::filesystem::path plain =
        std::filesystem::temp_directory_path() / "avgen-editor-viewpoint-plain.json";
    REQUIRE(authoring.saveProject(plain).has_value());

    authoring.composition()->setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    scene::CameraPose pose;
    pose.position = kAbsurdEye;
    pose.target = kAbsurdTarget;
    authoring.composition()->setEditorCamera(pose);
    const std::filesystem::path flown =
        std::filesystem::temp_directory_path() / "avgen-editor-viewpoint-flown.json";
    REQUIRE(authoring.saveProject(flown).has_value());

    const auto cameraTrack = [](const std::filesystem::path& path) {
        std::vector<glm::vec3> eyes;
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(path).has_value());
        engine.setViewport(1920, 1080);
        for (int frame = 0; frame < 30; ++frame) {
            FrameTime t;
            t.renderTime = static_cast<double>(frame) / 30.0;
            t.deltaTime = 1.0 / 30.0;
            t.frameIndex = static_cast<std::uint64_t>(frame);
            engine.update(t);
            eyes.push_back(engine.scene().camera.position);
        }
        return eyes;
    };

    const std::vector<glm::vec3> a = cameraTrack(plain);
    const std::vector<glm::vec3> b = cameraTrack(flown);
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, glm::length(a[i] - b[i]));
    }
    INFO("worst frame differs by " << worst << " m over " << a.size() << " frames");
    CHECK(worst < 1e-5f);

    // **The control that could come out the other way** (ADR-182). Move the *film's* camera by the
    // same absurd amount and the two tracks must diverge -- otherwise this test proves only that
    // the same project was rendered twice.
    app::Engine moved(app::EngineMode::Offline);
    REQUIRE(moved.loadProject(plain).has_value());
    auto* p = moved.params().find("camera/position");
    REQUIRE(p != nullptr);
    for (std::size_t c = 0; c < 3; ++c) {
        p->setBaseComponent(c, kAbsurdEye[static_cast<int>(c)]);
    }
    if (auto* mode = dynamic_cast<params::Parameter<int>*>(moved.params().find("camera/mode"))) {
        mode->setBase(1); // position is only read in free mode
    }
    const std::filesystem::path elsewhere =
        std::filesystem::temp_directory_path() / "avgen-editor-viewpoint-elsewhere.json";
    REQUIRE(moved.saveProject(elsewhere).has_value());
    const std::vector<glm::vec3> c = cameraTrack(elsewhere);
    REQUIRE(c.size() == a.size());
    float control = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        control = std::max(control, glm::length(a[i] - c[i]));
    }
    INFO("the control moved the film's camera by " << control << " m");
    CHECK(control > 1000.0f);

    std::filesystem::remove(plain);
    std::filesystem::remove(flown);
    std::filesystem::remove(elsewhere);
}
