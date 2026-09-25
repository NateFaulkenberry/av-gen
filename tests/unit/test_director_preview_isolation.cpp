// A preview or a render must never save the person's project (Director program 0.4, spec §8).
//
// `Application::startRenderFromUi` and the render queue used to get the file a render loads by
// calling `saveProject(projectPath())`: every unsaved edit went into the person's own file, and the
// "unsaved changes" prompt that would have let them decline was cleared by the same save. The
// Director's preview is built the same way a render is -- a scratch session loaded from a file -- so
// the rule it needs is the render's rule: read a scratch COPY (`Engine::writeProjectCopy`), resolve
// outputs against the person's folder, and leave their file, their project path and their dirty
// state exactly as they were.

#include "app/engine.hpp"
#include "app/render_source.hpp"
#include "scene/camera_rig.hpp"
#include "support/gltf_fixture.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("a project copy carries the unsaved session and leaves the person's project alone",
          "[directing][preview]") {
    testsupport::ScratchDir dir{"director_preview"};
    testsupport::ScratchDir elsewhere{"director_preview_scratch"};
    const fs::path scene = dir / "world.scene.json";
    const fs::path project = dir / "world.json";

    // A saved, by-reference project: the shape every shipped world has.
    app::Engine engine(app::EngineMode::Offline);
    {
        const auto tmp = testsupport::writeTriangleGlb("director_preview");
        fs::copy_file(tmp, dir / "tri.glb", fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        engine.newComposition();
        scene::CompositionNode node;
        node.name = "rook";
        node.kind = scene::NodeKind::Gltf;
        node.asset = (dir / "tri.glb").generic_string();
        REQUIRE(engine.addNode(std::move(node)).has_value());
        REQUIRE(engine.saveComposition(scene).has_value());
        REQUIRE(engine.saveProject(project).has_value());
    }
    const std::string onDisk = bytesOf(project);

    // An unsaved edit: the thing a render used to write into the person's file.
    scene::CameraDirection direction = engine.composition()->cameraDirection();
    scene::CameraRig rig;
    rig.name = "Preview Cam";
    direction.addCamera(rig);
    REQUIRE(engine.setCameraDirection(direction).has_value());
    testsupport::stepFrames(engine, 1);
    REQUIRE(engine.projectDirty(true));

    const fs::path copy = elsewhere / "session-copy.json";
    REQUIRE(engine.writeProjectCopy(copy).has_value());

    // Nothing about the person's session moved.
    CHECK(bytesOf(project) == onDisk);
    CHECK(engine.projectPath() == project);
    CHECK(engine.projectDirty(true));

    // And the copy is the session as it is now, loadable from somewhere else: its asset paths were
    // made relative to where it was written, so the scene and the glTF resolve.
    app::Engine preview(app::EngineMode::Offline);
    REQUIRE(preview.loadProject(copy).has_value());
    CHECK(preview.projectWarnings().empty());
    REQUIRE(preview.composition() != nullptr);
    CHECK(preview.composition()->findNode("rook") != nullptr);
    CHECK(preview.composition()->cameraDirection().findByName("Preview Cam") != nullptr);
}

TEST_CASE("a render reads a scratch copy and still writes its output beside the project",
          "[directing][preview]") {
    const fs::path temp = "/tmp/scratch-root";
    const app::RenderSource saved =
        app::renderSourceFor("/Users/someone/films/night.json", temp, "render", 3, 4242);
    CHECK(saved.scratch.parent_path() == temp);
    CHECK(saved.scratch != fs::path("/Users/someone/films/night.json"));
    CHECK(saved.outputBase == fs::path("/Users/someone/films"));

    // Two renders in one session, and the same render in two sessions, never share a file.
    CHECK(app::renderSourceFor("/a/b.json", temp, "render", 4, 4242).scratch != saved.scratch);
    CHECK(app::renderSourceFor("/a/b.json", temp, "render", 3, 4243).scratch != saved.scratch);
    CHECK(app::renderSourceFor("/a/b.json", temp, "queue", 3, 4242).scratch != saved.scratch);

    // A session that was never saved has no folder of its own; its output goes where it always did.
    CHECK(app::renderSourceFor({}, temp, "render", 1, 1).outputBase == temp);
}
