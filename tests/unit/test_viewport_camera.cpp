// The viewport and the film's camera are two things (ADR-391).
//
// Reported first as: "the canvas viewport is now effectively owned by whichever camera the director
// currently controls... there is no reliable way to leave the director camera and freely navigate
// the world." The first answer to that was free-roam: an override on the *result* of
// `resolveActiveCamera` that pinned the frame to the MAIN camera. It fixed the reported symptom and
// left the cause standing, because the main camera **is** `camera/*` -- so flying the viewport was
// still an edit to the film, and on a project whose cut is baked the only way to honour a drag was
// to destroy the cut (ADR-386's lock, and its cost).
//
// The answer now is a pose the film does not own. The viewport has a mode -- the film's camera, the
// editor's own viewpoint, or pinned through one authored rig -- and only the first of those is a
// camera any render has ever heard of.
//
// The rule that makes it safe is unchanged and is the reason this file exists: the override is
// applied to the *frame*, never inside the resolver. `resolveActiveCamera` stays a pure function of
// (cameras, shots, events, time), which is what ADR-091 rests on -- it is what makes a cut
// scrubbable and an offline render identical to the live one. So the director still decides,
// `activeCamera()` still reports the film truthfully **in every mode** (free-roam did not: it
// overwrote the answer with "Viewport"), and all that changes is which pose is written into
// `Scene::camera` for display.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>

#include <filesystem>

using namespace avgen;

namespace {

struct Fixture {
    assets::AssetRegistry registry{std::filesystem::temp_directory_path() / "avgen-viewport-cam"};
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    scene::Composition comp{registry, "viewport"};

    Fixture() {
        comp.attach(params, modulator);
        // Two authored cameras a long way apart, and a shot that puts the second one on screen from
        // five seconds. Far apart on purpose: "the viewport did not move" has to be a statement
        // about metres, not about floating-point noise.
        scene::CameraDirection dir;
        scene::CameraRig wide;
        wide.id = 2;
        wide.name = "Wide";
        wide.slug = "wide";
        wide.position = {100.0f, 20.0f, 100.0f};
        wide.target = {0.0f, 0.0f, 0.0f};
        dir.cameras.push_back(wide);
        scene::CameraRig hero;
        hero.id = 3;
        hero.name = "Hero";
        hero.slug = "hero";
        hero.position = {-100.0f, 5.0f, -100.0f};
        hero.target = {0.0f, 1.0f, 0.0f};
        dir.cameras.push_back(hero);
        dir.shots.push_back(scene::CameraShot{.camera = 2, .startSeconds = 0.0, .endSeconds = 5.0});
        dir.shots.push_back(scene::CameraShot{.camera = 3, .startSeconds = 5.0, .endSeconds = 10.0});
        // `validate` refuses an id the counter would hand out again, so the counter has to be past
        // the highest id here -- the same rule a project file obeys.
        dir.nextId = 4;
        auto set = comp.setCameraDirection(std::move(dir));
        INFO((set ? std::string() : set.error().message));
        REQUIRE(set.has_value());
    }

    // Free placement, the way the viewport's own gesture sets it. `camera/mode` is a
    // `Parameter<int>` and the generic component setter does not reach it -- orbit mode ignores
    // position and target entirely and circles the scene bounds, so without this every "free-roam"
    // arm below measures an orbit around an empty world instead of the pose it set.
    void freePlacement() {
        auto* p = params.find("camera/mode");
        auto* mode = dynamic_cast<params::Parameter<int>*>(p);
        REQUIRE(mode != nullptr);
        mode->setBase(1);
    }

    // The editor's own viewpoint -- what a drag writes now (`Application::setViewportPose`).
    void placeEditorView(const glm::vec3& eye, const glm::vec3& look) {
        scene::CameraPose pose;
        pose.position = eye;
        pose.target = look;
        comp.setEditorCamera(pose);
    }

    // The film's main camera, by its parameters. Still reachable, and still what a drag writes when
    // the canvas is deliberately showing the film.
    void placeViewport(const glm::vec3& eye, const glm::vec3& look) {
        freePlacement();
        auto* pos = params.find("camera/position");
        auto* tgt = params.find("camera/target");
        REQUIRE(pos != nullptr);
        REQUIRE(tgt != nullptr);
        for (std::size_t c = 0; c < 3; ++c) {
            pos->setBaseComponent(c, eye[static_cast<int>(c)]);
            tgt->setBaseComponent(c, look[static_cast<int>(c)]);
        }
    }

    // One frame at `seconds`, then where the frame is looking from.
    glm::vec3 eyeAt(double seconds) {
        FrameTime t;
        t.renderTime = seconds;
        t.deltaTime = 1.0 / 60.0;
        comp.update(t);
        return comp.scene().camera.position;
    }
};

} // namespace

TEST_CASE("Following the film, a camera cut moves the viewport", "[viewport][camera]") {
    Fixture fx;
    REQUIRE(fx.comp.viewportView().showsFilm()); // the composition's default, and every render

    const glm::vec3 onWide = fx.eyeAt(1.0);
    CHECK(fx.comp.activeCamera().camera == 2);
    const glm::vec3 onHero = fx.eyeAt(7.0);
    CHECK(fx.comp.activeCamera().camera == 3);

    // The case that is *correct* and must keep working: the director cut, and the canvas cut with
    // it. Without this the editor-viewpoint test below would pass on a viewport that never moves.
    INFO("wide eye " << onWide.x << ", hero eye " << onHero.x);
    CHECK(glm::length(onHero - onWide) > 50.0f);
}

TEST_CASE("The editor's viewpoint does not move when the film cuts", "[viewport][camera]") {
    Fixture fx;
    fx.eyeAt(1.0);
    REQUIRE(fx.comp.activeCamera().camera == 2);

    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    const glm::vec3 mine{7.0f, 3.0f, 11.0f};
    fx.placeEditorView(mine, glm::vec3(0.0f));

    const glm::vec3 before = fx.eyeAt(1.0);
    INFO("editor eye " << before.x << ", " << before.y << ", " << before.z);
    // Where it sits is not a matter of opinion any more. Free-roam's pose was the main camera's,
    // so this could only ever be asserted to within the orbit angle that camera integrates every
    // frame; the editor's viewpoint is the pose it was given.
    CHECK(glm::length(before - mine) < 1e-4f);

    // **The report, as an assertion.** The director cuts from Wide to Hero at five seconds; the
    // viewport must not go with it. The two cameras are 280 m apart, so a viewport that followed
    // would move by hundreds.
    const glm::vec3 after = fx.eyeAt(7.0);
    INFO("moved " << glm::length(after - before) << " m across the cut");
    CHECK(glm::length(after - before) < 1e-4f);

    // And the director still did its job. This is the assertion free-roam could not make: it
    // rewrote `activeCamera` to say "Viewport", so while anybody was flying, the Cameras panel,
    // the preview label and the lens all lost track of what the film was on.
    CHECK(fx.comp.activeCamera().camera == 3);
    CHECK(fx.comp.activeCamera().reason == scene::ActiveCameraReason::Shot);
}

TEST_CASE("The editor's viewpoint never touches the film's cameras", "[viewport][camera]") {
    Fixture fx;
    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    fx.placeEditorView(glm::vec3(42.0f), glm::vec3(0.0f));
    fx.eyeAt(7.0);

    // Flying the viewport is not an edit to anybody's camera. Checked on the rigs and on the main
    // camera's own parameters rather than on what is drawn, because "the picture did not change"
    // would also be true of a camera that was quietly rewritten and then not displayed -- and
    // `camera/position` is exactly the value an app save photographs.
    const scene::CameraRig* hero = fx.comp.cameraDirection().find(3);
    REQUIRE(hero != nullptr);
    CHECK_THAT(hero->position.x, Catch::Matchers::WithinAbs(-100.0, 1e-4));
    CHECK_THAT(hero->position.z, Catch::Matchers::WithinAbs(-100.0, 1e-4));

    const auto* pos = fx.params.find("camera/position");
    REQUIRE(pos != nullptr);
    const glm::vec3 base(pos->baseComponent(0), pos->baseComponent(1), pos->baseComponent(2));
    INFO("camera/position base " << base.x << ", " << base.y << ", " << base.z);
    CHECK(glm::length(base - glm::vec3(42.0f)) > 1.0f);
}

TEST_CASE("Showing the film again resumes the director's camera", "[viewport][camera]") {
    Fixture fx;
    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    fx.placeEditorView(glm::vec3(7.0f, 3.0f, 11.0f), glm::vec3(0.0f));
    const glm::vec3 roamed = fx.eyeAt(7.0);

    fx.comp.setViewportView({});
    const glm::vec3 followed = fx.eyeAt(7.0);
    INFO("editor " << roamed.x << " -> film " << followed.x);
    CHECK(fx.comp.activeCamera().camera == 3);
    CHECK(glm::length(followed - roamed) > 50.0f);

    // Reversible, which is the property the report was really about: there must be no state you can
    // get into and not get out of. And exactly reversible -- the editor's pose is kept while the
    // canvas is elsewhere rather than being re-derived when it comes back.
    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    const glm::vec3 again = fx.eyeAt(7.0);
    CHECK(fx.comp.activeCamera().camera == 3);
    CHECK(glm::length(again - followed) > 50.0f);
    CHECK(glm::length(again - roamed) < 1e-4f);
}

TEST_CASE("The editor's viewpoint starts where the film left it", "[viewport][camera]") {
    // Seeding, and it is what stops "take the viewport back" from being a teleport. A person who
    // switches to the editor's viewpoint is looking at a frame and wants to fly *from there*; an
    // unseeded pose would throw them to wherever a default happened to be, which is the original
    // report ("no reliable way to leave the director camera") wearing a different hat.
    Fixture fx;
    const glm::vec3 onHero = fx.eyeAt(7.0);
    REQUIRE(fx.comp.activeCamera().camera == 3);
    REQUIRE_FALSE(fx.comp.editorCameraSeeded());

    fx.comp.setViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
    const glm::vec3 first = fx.eyeAt(7.0);
    INFO("hero " << onHero.x << " -> first editor frame " << first.x);
    CHECK(glm::length(first - onHero) < 1e-4f);
    CHECK(fx.comp.editorCameraSeeded());

    // Seeded once. The film cuts on; the editor's viewpoint stays where the seed put it, rather
    // than re-seeding every frame -- which would be a viewport that follows the director again.
    const glm::vec3 later = fx.eyeAt(1.0);
    CHECK(fx.comp.activeCamera().camera == 2);
    CHECK(glm::length(later - first) < 1e-4f);
}

TEST_CASE("Looking through a camera pins the canvas to it", "[viewport][camera]") {
    // The feature that could not be built before there was a pose the film did not own. It is not
    // "go to that camera": the canvas rides the rig, and the film goes on cutting underneath.
    Fixture fx;
    const glm::vec3 onWide = fx.eyeAt(1.0);
    REQUIRE(fx.comp.activeCamera().camera == 2);

    fx.comp.setViewportView({scene::ViewportCamera::Through, 3});
    const glm::vec3 throughHero = fx.eyeAt(1.0);
    INFO("wide " << onWide.x << " -> through hero " << throughHero.x);
    // Looking through Hero at a second when the film is on Wide -- which is the whole point.
    CHECK(glm::length(throughHero - glm::vec3(-100.0f, 5.0f, -100.0f)) < 1e-3f);
    CHECK(fx.comp.activeCamera().camera == 2);

    // And it stays pinned across the cut, which is the difference between "look through" and
    // "go to": at seven seconds the film arrives at Hero and the canvas does not move at all.
    const glm::vec3 after = fx.eyeAt(7.0);
    CHECK(fx.comp.activeCamera().camera == 3);
    CHECK(glm::length(after - throughHero) < 1e-3f);
}

TEST_CASE("Looking through a camera that is gone falls back to the film", "[viewport][camera]") {
    // A camera can be deleted while the canvas is pinned to it, and the frame has to come from
    // somewhere. The film is the only answer that is always available.
    Fixture fx;
    fx.comp.setViewportView({scene::ViewportCamera::Through, 3});
    const glm::vec3 pinned = fx.eyeAt(1.0);

    scene::CameraDirection dir = fx.comp.cameraDirection();
    std::erase_if(dir.shots, [](const scene::CameraShot& s) { return s.camera == 3; });
    dir.removeCamera(3);
    REQUIRE(fx.comp.setCameraDirection(std::move(dir)).has_value());

    const glm::vec3 after = fx.eyeAt(1.0);
    INFO("pinned " << pinned.x << " -> fell back to " << after.x);
    CHECK(fx.comp.activeCamera().camera == 2);
    CHECK(glm::length(after - glm::vec3(100.0f, 20.0f, 100.0f)) < 1e-3f);
}

TEST_CASE("A viewport mode is not a camera the director can see", "[viewport][camera]") {
    // ADR-091, stated as an assertion. `resolveActiveCamera` is a pure function of (cameras, shots,
    // events, time) and the viewport's mode is none of those four. If the mode ever leaked into the
    // resolver, the film would depend on where somebody had flown -- so the same second, resolved
    // under each of the three modes, must give the same answer three times.
    const auto filmAt = [](scene::ViewportView view, double seconds) {
        Fixture fx;
        fx.comp.setViewportView(view);
        fx.placeEditorView(glm::vec3(900.0f, 900.0f, 900.0f), glm::vec3(0.0f));
        fx.eyeAt(seconds);
        return fx.comp.activeCamera();
    };
    for (const double seconds : {1.0, 4.9, 5.1, 7.0}) {
        const scene::ActiveCameraState film = filmAt({}, seconds);
        const scene::ActiveCameraState editor =
            filmAt({scene::ViewportCamera::Editor, scene::kNoCamera}, seconds);
        const scene::ActiveCameraState through = filmAt({scene::ViewportCamera::Through, 2}, seconds);
        INFO("at " << seconds << " s the film is on camera " << film.camera);
        CHECK(editor.camera == film.camera);
        CHECK(editor.reason == film.reason);
        CHECK(through.camera == film.camera);
        CHECK(through.reason == film.reason);
    }
    // The control: the resolver is not answering the same thing at every second either.
    CHECK(filmAt({}, 1.0).camera != filmAt({}, 7.0).camera);
}
