// §16's interaction matrix, as tests that run every time.
//
// The report behind the viewport/camera separation was fixed and has tests
// (`tests/unit/test_viewport_camera.cpp`): a cut moves a following viewport more than 50 m and a
// free-roaming one less than 0.5 m. Those are the right two assertions about the *one* thing that
// broke. What the specification asks for is broader and is what this file is:
//
//   > The viewport should not become unintentionally locked to a director camera merely because a
//   > shot/camera changes.
//
// "Merely because a shot/camera changes" covers more events than a cut, and the canvases the
// viewport can be in are three rather than one. So here every way the engine can be told about a
// camera is paired with the invariant that must survive it, and the surviving property is measured
// in metres and in camera ids rather than in flags -- §26: a test that checks a debug flag is set is
// not a test of engine behaviour.
//
// The three canvases are `ui::PreviewViewMode`: Workspace renders at the canvas's own size, Output
// Frame and Preview render at the *output's* aspect ratio inside it (ADR-246). They reach the camera
// through exactly two seams -- `Camera::projection(aspect)` and `Composition::setViewport` -- both
// fed from the extent `previewRenderExtent` chooses, so changing canvas is a change of aspect ratio
// and of nothing else. That is the claim; below it is an assertion.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "ui/output_preview.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// Two authored cameras a long way apart and a shot that cuts between them at five seconds. The
// distance is the instrument: "the viewport did not move" has to be a statement about metres rather
// than about floating-point noise, and 280 m between the two makes a followed cut unmistakable.
//
// Deliberately **not** axis-aligned and **not** at the origin. The Visibility Lab's predecessor bug
// survived every test in this repository because every fixture was a centred box, for which the
// right rule and the wrong rule were bit-identical (ADR-182). A camera at (100, 20, 100) aimed at
// the origin exercises a different code path from one on an axis in exactly the ways a projection,
// an aspect ratio and a blend can go wrong.
struct Rig {
    assets::AssetRegistry registry{std::filesystem::temp_directory_path() / "avgen-camlab-viewport"};
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    scene::Composition comp{registry, "camlab"};

    Rig() { comp.attach(params, modulator); install(); }

    void install(double cutAt = 5.0) {
        scene::CameraDirection dir;
        scene::CameraRig wide;
        wide.id = 2;
        wide.name = "Wide";
        wide.slug = "wide";
        wide.position = {103.0f, 21.0f, 97.0f};
        wide.target = {1.0f, 2.0f, -3.0f};
        dir.cameras.push_back(wide);
        scene::CameraRig hero;
        hero.id = 3;
        hero.name = "Hero";
        hero.slug = "hero";
        hero.position = {-97.0f, 6.0f, -101.0f};
        hero.target = {0.0f, 1.0f, 0.0f};
        dir.cameras.push_back(hero);
        dir.shots.push_back(scene::CameraShot{.camera = 2, .startSeconds = 0.0, .endSeconds = cutAt});
        dir.shots.push_back(
            scene::CameraShot{.camera = 3, .startSeconds = cutAt, .endSeconds = cutAt + 5.0});
        dir.nextId = 4;
        auto set = comp.setCameraDirection(std::move(dir));
        INFO((set ? std::string() : set.error().message));
        REQUIRE(set.has_value());
    }

    // Free placement, the way the viewport's own gesture sets it. `camera/mode` is a
    // `Parameter<int>` the generic component setter does not reach, and orbit mode ignores position
    // and target entirely -- so without this every "free-roam" arm measures an orbit around an empty
    // world instead of the pose it thought it set.
    // ADR-391: where the *editor's* viewpoint is, which is what a drag moves now. It used to be
    // `camera/position` in free mode -- the film's own camera -- and that coupling is what every
    // arm below was really measuring: a viewport that could only stay still by holding the film's
    // camera still.
    void placeViewport(const glm::vec3& eye, const glm::vec3& look) {
        scene::CameraPose pose;
        pose.position = eye;
        pose.target = look;
        comp.setEditorCamera(pose);
    }

    glm::vec3 eyeAt(double seconds) {
        FrameTime t;
        t.renderTime = seconds;
        t.deltaTime = 1.0 / 60.0;
        comp.update(t);
        return comp.scene().camera.position;
    }
};

// The canvas rectangle a docked centre panel actually has: not square, not 16:9, and narrower than
// the output -- which `output_preview.hpp` says is the ordinary case and the reason ADR-246 renders
// at the output's aspect rather than drawing a rectangle over the canvas's.
ui::CanvasRect canvas(float w, float h) {
    ui::CanvasRect r;
    r.x = 7.0f;
    r.y = 11.0f;
    r.width = w;
    r.height = h;
    r.hovered = true;
    return r;
}

const ui::PreviewViewMode kModes[] = {ui::PreviewViewMode::Workspace, ui::PreviewViewMode::OutputFrame,
                                      ui::PreviewViewMode::OutputPreview};

// What the host resizes the renderer to for a given canvas and view mode: the chain
// `ControlPanel::draw` and `Application::update` actually run, and the branch between them is the
// point of this helper.
//
// `previewRenderExtent` is only reached when `modeShowsOutputFrame` -- it always returns the
// *output's* aspect ratio, which is its whole job. In Workspace the panel leaves `previewRender()`
// zero and the host keeps the canvas's own device pixels. Calling it in Workspace too is the obvious
// way to write this helper and it is wrong: it made the control arm below report Workspace at
// 1920x1080 and quietly agree with the output, so the test that was supposed to establish the three
// canvases differ established that they do not.
ui::PreviewRender extentFor(ui::PreviewViewMode mode, const ui::CanvasRect& c, std::uint32_t ow,
                            std::uint32_t oh, float pixelScale = 2.0f) {
    if (!ui::modeShowsOutputFrame(mode)) {
        ui::PreviewRender r;
        r.width = static_cast<std::uint32_t>(std::lround(c.width * pixelScale));
        r.height = static_cast<std::uint32_t>(std::lround(c.height * pixelScale));
        return r;
    }
    const ui::PreviewFrame frame = ui::fitOutputFrame(c, ow, oh, ui::PreviewZoom{}, pixelScale);
    return ui::previewRenderExtent(frame, ow, oh, pixelScale, ui::PreviewQuality::Realtime);
}

} // namespace

// ---- the three canvases and the camera ----------------------------------------------------------

TEST_CASE("Output Frame and Preview render at the output's aspect; Workspace at the canvas's",
          "[camera][lab][viewport]") {
    const ui::CanvasRect c = canvas(980.0f, 690.0f);   // roughly 1.42:1, narrower than 16:9
    const double outputAspect = ui::outputAspect(1920, 1080);

    const ui::PreviewRender workspace = extentFor(ui::PreviewViewMode::Workspace, c, 1920, 1080);
    const double workspaceAspect =
        static_cast<double>(workspace.width) / static_cast<double>(workspace.height);
    INFO("workspace " << workspace.width << "x" << workspace.height << " = " << workspaceAspect
         << ", output " << outputAspect);
    // The control: Workspace is the canvas, and the canvas is *not* the output. If these agreed the
    // two arms below would be measuring nothing.
    CHECK(std::abs(workspaceAspect - outputAspect) > 0.2);

    for (const auto mode : {ui::PreviewViewMode::OutputFrame, ui::PreviewViewMode::OutputPreview}) {
        const ui::PreviewRender r = extentFor(mode, c, 1920, 1080);
        const double aspect = static_cast<double>(r.width) / static_cast<double>(r.height);
        INFO(ui::previewViewModeName(mode) << " " << r.width << "x" << r.height << " = " << aspect);
        CHECK(r.aspectMatches);
        CHECK(std::abs(aspect - outputAspect) < 0.01);
    }
}

TEST_CASE("Changing canvas changes the aspect and never the vertical field of view",
          "[camera][lab][viewport]") {
    // The claim ADR-246 rests on, as arithmetic. A perspective projection's *vertical* FOV is a
    // property of the camera and its horizontal FOV is derived from the aspect -- so a preview at
    // the output's aspect is the deliverable's framing, not an approximation of it. The opposite
    // convention (horizontal FOV fixed, vertical derived) would make every canvas change a reframe,
    // and the only way to know which this engine has is to measure it.
    scene::Camera cam;
    cam.position = {13.0f, 4.0f, -9.0f};
    cam.target = {0.0f, 1.0f, 0.0f};
    const float fovY = cam.effectiveFovY();

    // The vertical half-angle read back out of the projection matrix: P[1][1] = 1 / tan(fovY / 2),
    // by construction and independently of the aspect.
    const auto verticalFrom = [](const glm::mat4& p) { return 2.0f * std::atan(1.0f / p[1][1]); };
    const auto horizontalFrom = [](const glm::mat4& p) { return 2.0f * std::atan(1.0f / p[0][0]); };

    const glm::mat4 wide = cam.projection(16.0f / 9.0f);
    const glm::mat4 tall = cam.projection(9.0f / 16.0f);
    const glm::mat4 square = cam.projection(1.0f);

    CHECK_THAT(verticalFrom(wide), Catch::Matchers::WithinAbs(fovY, 1e-4));
    CHECK_THAT(verticalFrom(tall), Catch::Matchers::WithinAbs(fovY, 1e-4));
    CHECK_THAT(verticalFrom(square), Catch::Matchers::WithinAbs(fovY, 1e-4));
    // And the control, so "vertical is constant" is not being satisfied by a projection that ignores
    // aspect altogether: the horizontal angle does move, and by a lot.
    INFO("horizontal 16:9 " << horizontalFrom(wide) << " vs 9:16 " << horizontalFrom(tall));
    CHECK(horizontalFrom(wide) > horizontalFrom(tall) + 0.5f);
}

TEST_CASE("Near and far planes change the depth range and never the field of view",
          "[camera][lab][viewport]") {
    scene::Camera cam;
    cam.position = {13.0f, 4.0f, -9.0f};
    cam.nearPlane = 0.1f;
    cam.farPlane = 200.0f;
    const glm::mat4 shallow = cam.projection(16.0f / 9.0f);

    scene::Camera deep = cam;
    deep.nearPlane = 1.0f;
    deep.farPlane = 8000.0f;
    const glm::mat4 far = deep.projection(16.0f / 9.0f);

    // The two angle terms are untouched...
    CHECK_THAT(far[0][0], Catch::Matchers::WithinAbs(shallow[0][0], 1e-5));
    CHECK_THAT(far[1][1], Catch::Matchers::WithinAbs(shallow[1][1], 1e-5));
    // ...and the depth terms are not, which is the control: an assertion that only checked the first
    // two would pass on a projection that ignored the planes entirely.
    CHECK(std::abs(far[2][2] - shallow[2][2]) > 1e-4f);

    // A point 100 m down the view axis lands at a different normalised depth under the two, and in
    // 0..1 under both -- this engine's convention (WebGPU/Metal), not OpenGL's -1..1.
    const auto depthOf = [&](const glm::mat4& p, const scene::Camera& c) {
        const glm::vec4 clip = p * c.view() * glm::vec4(c.position + glm::normalize(c.target - c.position) * 100.0f, 1.0f);
        return clip.z / clip.w;
    };
    const float a = depthOf(shallow, cam);
    const float b = depthOf(far, deep);
    INFO("depth at 100 m: near=0.1 far=200 -> " << a << "; near=1 far=8000 -> " << b);
    CHECK(a >= 0.0f);
    CHECK(a <= 1.0f);
    CHECK(b >= 0.0f);
    CHECK(b <= 1.0f);
    CHECK(std::abs(a - b) > 1e-3f);
}

// ---- the headline invariant, over every way a camera can change ---------------------------------

TEST_CASE("A free-roaming viewport survives every kind of camera change", "[camera][lab][viewport]") {
    // §16's sentence, enumerated. Each section is one thing that "changes the shot or the camera",
    // and the invariant after every one of them is the same: the viewport is still the artist's, and
    // it has not jumped.
    const glm::vec3 mine{7.0f, 3.5f, 11.0f};

    SECTION("a cut between two authored cameras") {
        Rig fx;
        fx.eyeAt(1.0);
        fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
        fx.placeViewport(mine, glm::vec3(0.0f));
        const glm::vec3 before = fx.eyeAt(1.0);
        const glm::vec3 after = fx.eyeAt(7.0);
        INFO("moved " << glm::length(after - before) << " m across the cut");
        // Exact, not approximate. The editor's viewpoint is a pose, not an integrator: the
        // half-metre tolerance this used to need was the main camera's own orbit angle advancing
        // under a viewport that was standing on it.
        CHECK(glm::length(after - before) < 1e-4f);
        // And the director still did its job -- which the old assertion here could not say,
        // because free-roam overwrote `activeCamera` with "Viewport", so the Cameras panel lost
        // track of the film for as long as anybody was flying.
        CHECK(fx.comp.activeCamera().camera == 3);
    }

    SECTION("the whole camera direction being replaced") {
        // The case the specification actually names: a *shot or camera changes*, not the clock. An
        // editor that re-installs the direction on every edit -- adding a shot, renaming a camera,
        // dragging a shot's edge -- must not thereby take the canvas back.
        Rig fx;
        fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
        fx.placeViewport(mine, glm::vec3(0.0f));
        const glm::vec3 before = fx.eyeAt(7.0);
        fx.install(3.0);   // the same two cameras, the cut moved
        CHECK(fx.comp.viewportView().mode == scene::ViewportCamera::Editor);
        const glm::vec3 after = fx.eyeAt(7.0);
        INFO("moved " << glm::length(after - before) << " m across a re-install");
        CHECK(glm::length(after - before) < 1e-4f);
        CHECK(fx.comp.activeCamera().camera == 3);
    }

    SECTION("a camera being added to the collection") {
        Rig fx;
        fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
        fx.placeViewport(mine, glm::vec3(0.0f));
        const glm::vec3 before = fx.eyeAt(7.0);
        scene::CameraDirection dir = fx.comp.cameraDirection();
        scene::CameraRig extra;
        extra.id = 4;
        extra.name = "Third";
        extra.slug = "third";
        extra.position = {0.0f, 400.0f, 0.0f};
        extra.target = {0.0f, 0.0f, 0.0f};
        dir.cameras.push_back(extra);
        dir.nextId = 5;
        REQUIRE(fx.comp.setCameraDirection(std::move(dir)).has_value());
        CHECK(fx.comp.viewportView().mode == scene::ViewportCamera::Editor);
        const glm::vec3 after = fx.eyeAt(7.0);
        INFO("moved " << glm::length(after - before) << " m across an added camera");
        CHECK(glm::length(after - before) < 1e-4f);
    }

    SECTION("the viewport being resized, in every canvas mode") {
        // Every canvas the editor has, at a real output resolution, driven through the same chain
        // `Application` uses. A resize is the one camera-adjacent event that happens without anybody
        // touching a camera at all, and it must move nothing.
        for (const auto mode : kModes) {
            Rig fx;
            fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
            fx.placeViewport(mine, glm::vec3(0.0f));
            const ui::PreviewRender a = extentFor(mode, canvas(980.0f, 690.0f), 1920, 1080);
            fx.comp.setViewport(a.width, a.height);
            const glm::vec3 before = fx.eyeAt(7.0);
            const ui::PreviewRender b = extentFor(mode, canvas(1440.0f, 420.0f), 1080, 1920);
            fx.comp.setViewport(b.width, b.height);
            const glm::vec3 after = fx.eyeAt(7.0);
            INFO(ui::previewViewModeName(mode) << ": " << a.width << "x" << a.height << " -> "
                 << b.width << "x" << b.height << ", moved " << glm::length(after - before) << " m");
            // The control first: the two extents really are different shapes, so a resize that
            // changed nothing is not what made this pass.
            CHECK((a.width != b.width || a.height != b.height));
            CHECK(fx.comp.viewportView().mode == scene::ViewportCamera::Editor);
            CHECK(glm::length(after - before) < 1e-4f);
            CHECK(fx.comp.activeCamera().camera == 3);
        }
    }
}

TEST_CASE("A following viewport still follows every kind of camera change", "[camera][lab][viewport]") {
    // The other half of every arm above, and the reason they mean anything: a viewport that never
    // moves would satisfy all of them. Directing must still direct.
    SECTION("the cut moves it") {
        Rig fx;
        REQUIRE(fx.comp.viewportView().showsFilm());
        const glm::vec3 wide = fx.eyeAt(1.0);
        CHECK(fx.comp.activeCamera().camera == 2);
        const glm::vec3 hero = fx.eyeAt(7.0);
        CHECK(fx.comp.activeCamera().camera == 3);
        INFO("the cut moved the viewport " << glm::length(hero - wide) << " m");
        CHECK(glm::length(hero - wide) > 50.0f);
    }

    SECTION("moving the cut moves when it happens") {
        Rig fx;
        const glm::vec3 atThree = fx.eyeAt(3.5);
        CHECK(fx.comp.activeCamera().camera == 2);   // the cut is at 5 s
        fx.install(3.0);
        const glm::vec3 after = fx.eyeAt(3.5);
        CHECK(fx.comp.activeCamera().camera == 3);   // now it is at 3 s
        INFO("re-cutting moved the viewport " << glm::length(after - atThree) << " m");
        CHECK(glm::length(after - atThree) > 50.0f);
    }

    SECTION("a resize does not move it") {
        // A following viewport is owned by the director, so a resize must not move it either --
        // different reason, same number. Without this arm, "free-roam survives a resize" would be
        // consistent with a resize simply never moving any camera, which would make that test a
        // test of nothing.
        Rig fx;
        fx.comp.setViewport(1920, 1080);
        const glm::vec3 before = fx.eyeAt(7.0);
        fx.comp.setViewport(720, 1280);
        const glm::vec3 after = fx.eyeAt(7.0);
        INFO("resize moved a following viewport " << glm::length(after - before) << " m");
        CHECK(glm::length(after - before) < 1e-3f);
        CHECK(fx.comp.activeCamera().camera == 3);
    }
}

TEST_CASE("Lock and unlock are reversible from every canvas mode", "[camera][lab][viewport]") {
    // "Camera lock/unlock" from §16's test list. The property is that there is no state you can get
    // into and not get out of -- which is what the original report was really about -- and it has to
    // hold whichever canvas the person is in, because the canvas is what they will have changed
    // while trying to get out.
    for (const auto mode : kModes) {
        Rig fx;
        const ui::PreviewRender extent = extentFor(mode, canvas(1180.0f, 760.0f), 2560, 1440);
        fx.comp.setViewport(extent.width, extent.height);

        const glm::vec3 directed = fx.eyeAt(7.0);
        REQUIRE(fx.comp.activeCamera().camera == 3);

        fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
        fx.placeViewport(glm::vec3(4.0f, 9.0f, -13.0f), glm::vec3(0.0f));
        const glm::vec3 roamed = fx.eyeAt(7.0);
        // The film never left camera 3 -- flying the editor's viewpoint is not a change of shot.
        CHECK(fx.comp.activeCamera().camera == 3);
        CHECK(glm::length(roamed - directed) > 50.0f);

        fx.comp.setViewportView({});
        const glm::vec3 back = fx.eyeAt(7.0);
        INFO(ui::previewViewModeName(mode) << " at " << extent.width << "x" << extent.height
             << ": directed " << directed.x << ", roamed " << roamed.x << ", back " << back.x);
        CHECK(fx.comp.activeCamera().camera == 3);
        CHECK(glm::length(back - directed) < 1e-3f);
    }
}

TEST_CASE("Flying the viewport never edits anybody's camera", "[camera][lab][viewport]") {
    Rig fx;
    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    fx.placeViewport(glm::vec3(42.0f, 17.0f, -31.0f), glm::vec3(3.0f, 1.0f, 2.0f));
    fx.eyeAt(7.0);
    // Checked on the rigs rather than on what is drawn: "the picture did not change" would also be
    // true of a camera that was quietly rewritten and then not displayed.
    const scene::CameraRig* hero = fx.comp.cameraDirection().find(3);
    REQUIRE(hero != nullptr);
    CHECK_THAT(hero->position.x, Catch::Matchers::WithinAbs(-97.0, 1e-4));
    CHECK_THAT(hero->position.z, Catch::Matchers::WithinAbs(-101.0, 1e-4));
    const scene::CameraRig* wide = fx.comp.cameraDirection().find(2);
    REQUIRE(wide != nullptr);
    CHECK_THAT(wide->position.x, Catch::Matchers::WithinAbs(103.0, 1e-4));
}

// ---- the tenth axis: shot cameras and the ten presets --------------------------------------------
//
// §16 lists "shot camera" and "camera presets" beside the director camera and free roam, and they
// reach the viewport by a **different route**, which is the finding this section exists to record.
//
// The finding this section recorded, and what ADR-391 did to it. The old free-roam flag redirected
// `activeCamera_` away from an authored `scene::CameraRig` and back to `kMainCamera`. A `seq::Shot`'s
// camera is **not a rig**: `Sequence::bake` emits ordinary `camera/position` and `camera/target`
// keys onto the timeline, and the main camera is what those keys drive -- so it was exactly the
// camera free-roam handed the viewport back to. Free-roam was free of *rigs* and not free of
// *tracks*, and nothing in the flag's name said so: a baked sequence dragged the "free" viewport
// around by the face.
//
// (Two things called "shot", and this is where they meet: `seq::Shot` is a sequence's shot and bakes
// to tracks; `scene::CameraShot` is the camera director's and names a rig. Confusing them is the
// standing trap in this area.)
//
// The editor's viewpoint is neither a rig nor a track, so it is free of both, and the assertion
// below changed direction when it landed. It is still measured rather than read, because reading
// the code is how the original report got argued about for a week.

#include "app/cinematic.hpp"
#include "app/engine.hpp"
#include "seq/sequence.hpp"

namespace {

std::filesystem::path presetProject() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "city" / "night-shift.json";
}

// One shot on `preset`, installed into a real engine, played for a second. Returns how far the
// camera the viewport is showing travelled -- which is the only question that matters here.
struct PresetRun {
    bool ran = false;
    float travelled = 0.0f;
    scene::CameraId active = scene::kMainCamera;
};

PresetRun runPreset(seq::CameraPreset preset, bool editorView) {
    PresetRun out;
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(presetProject());
    if (!loaded) {
        WARN("load: " << loaded.error().message);
        return out;
    }
    scene::Composition* comp = engine.composition();
    if (comp == nullptr || comp->nodes().empty()) {
        WARN("no nodes");
        return out;
    }
    app::FocalTarget subject;
    subject.position = {0.0f, 0.0f, 0.0f};
    subject.radius = 4.0f;
    subject.name = "subject";

    seq::Sequence piece;
    piece.name = "preset-probe";
    seq::Actor actor;
    actor.id = "subject";
    actor.node = comp->nodes().front()->name;
    actor.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = {-20.0f, 0.0f, 0.0f}});
    actor.keys.push_back(seq::ActorKey{.timeSeconds = 4.0, .position = {20.0f, 0.0f, 0.0f}});
    piece.actors.push_back(actor);

    seq::Shot shot;
    shot.name = "probe";
    shot.startSeconds = 0.0;
    shot.durationSeconds = 4.0;
    shot.camera = seq::cameraFromPreset(preset, subject);
    shot.camera.lookAtActor = "subject";
    // The three behaviour presets resolve against a performer rather than a point, and the bake
    // refuses rather than quietly placing the camera at the origin -- so the fixture has to name
    // one. The first version of this file did not, and three of the ten presets skipped the whole
    // test case rather than failing it, which is the failure mode a `SKIP` inside a loop always has.
    shot.camera.behavior.actor = "subject";
    piece.shots.push_back(shot);
    if (auto seq = engine.setSequence(piece); !seq) {
        WARN("setSequence: " << seq.error().message);
        return out;
    }
    if (editorView) {
        comp->setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    }
    // **Played, not seeked** (ADR-091). `seekSeconds` moves the clock; `Engine::update` re-derives
    // the scene. A test that seeks and reads measures the frame it was already on, which has
    // produced two false results in this project.
    FixedStepClock clock(60.0);
    engine.seekSeconds(0.0);
    engine.setViewport(1920, 1080);
    engine.update(engine.tick(clock));
    const glm::vec3 first = engine.scene().camera.position;
    for (int frame = 0; frame < 120; ++frame) {
        engine.update(engine.tick(clock));
    }
    out.travelled = glm::length(engine.scene().camera.position - first);
    out.active = comp->activeCamera().camera;
    out.ran = true;
    return out;
}

} // namespace

TEST_CASE("Every camera preset installs as timeline keys, and the editor's viewpoint is immune to"
          " them", "[camera][lab][preset][viewport]") {
    if (!std::filesystem::exists(presetProject())) {
        SKIP("night-shift is not present");
    }
    // All ten from the table, not a hand-picked few: `allCameraPresets` is the one list, so a
    // preset added tomorrow is covered by this the day it is added.
    std::size_t moved = 0;
    std::size_t still = 0;
    for (const seq::CameraPreset preset : seq::allCameraPresets()) {
        const PresetRun following = runPreset(preset, false);
        const PresetRun roaming = runPreset(preset, true);
        // A failure, not a skip: a preset whose fixture will not install is a preset this test is
        // silently not covering, and the loop would carry on to the next one.
        REQUIRE(following.ran);
        REQUIRE(roaming.ran);
        INFO(seq::cameraPresetName(preset) << ": the film travelled " << following.travelled
             << " m, the editor's viewpoint travelled " << roaming.travelled << " m, active camera "
             << roaming.active);
        // A shot camera is a *track*, not a rig, so `activeCamera` is the main camera either way.
        // That is still true and still worth saying: it is why free-roam could not shield the
        // viewport from a baked sequence, and why the fix had to be a pose the film does not own
        // rather than a redirection to one of the film's own cameras.
        CHECK(following.active == scene::kMainCamera);
        CHECK(roaming.active == scene::kMainCamera);
        // **The assertion that changed direction.** Whatever the preset does to the film's camera,
        // it does nothing at all to the editor's viewpoint -- which is what "navigating is not the
        // same act as modifying the film's camera" means when the film is the one moving.
        CHECK_THAT(roaming.travelled, Catch::Matchers::WithinAbs(0.0, 1e-4));
        (following.travelled > 0.01f ? moved : still)++;
    }
    // The control, and it is load-bearing here: "the editor's viewpoint did not move" is satisfied
    // by ten presets that move nothing at all. At least one preset must actually drive the film's
    // camera for the comparison to have had something to be immune to.
    INFO(moved << " preset(s) move the film's camera, " << still << " hold it still");
    CHECK(moved > 0);
}
