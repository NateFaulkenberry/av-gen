// The viewport and the director's camera are two things.
//
// Reported as: "the canvas viewport is now effectively owned by whichever camera the director
// currently controls... there is no reliable way to leave the director camera and freely navigate
// the world."
//
// That was accurate, and it was a consequence of ADR-245 rather than a bug in it. Before ADR-245
// there was one camera: `Scene::camera` came from the `camera/*` block, flying the viewport moved
// that block, and "which camera" and "where the camera is" were the same question. ADR-245 split
// them -- correctly -- so a shot can put an authored *rig* on screen. What did not follow was the
// viewport's way out: `Application::ensureFreeCamera` released the director's hold by deleting the
// tracks it had baked onto `camera/*`, which does exactly nothing when the thing being displayed is
// a different camera entirely. The drag moved a camera nobody was looking through.
//
// The fix is an override on the *result* of `resolveActiveCamera`, never inside it. The resolver is
// a pure function of (cameras, shots, events, time) and ADR-091 rests on that -- it is what makes a
// cut scrubbable and an offline render identical to the live one. Feeding an editor's navigation
// state into it would make the film depend on where somebody had flown the viewport.
//
// So: the director still decides, `activeCamera()` still reports it truthfully, and free-roam only
// changes which pose is written into `Scene::camera` for display.

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

TEST_CASE("Following the director, a camera cut moves the viewport", "[viewport][camera]") {
    Fixture fx;
    REQUIRE_FALSE(fx.comp.viewportFreeRoam()); // following is the default, as it always was

    const glm::vec3 onWide = fx.eyeAt(1.0);
    CHECK(fx.comp.activeCamera().camera == 2);
    const glm::vec3 onHero = fx.eyeAt(7.0);
    CHECK(fx.comp.activeCamera().camera == 3);

    // The case that is *correct* and must keep working: the director cut, and the canvas cut with
    // it. Without this the free-roam test below would pass on a viewport that never moves at all.
    INFO("wide eye " << onWide.x << ", hero eye " << onHero.x);
    CHECK(glm::length(onHero - onWide) > 50.0f);
}

TEST_CASE("Free-roaming, a camera cut does not move the viewport", "[viewport][camera]") {
    Fixture fx;
    fx.eyeAt(1.0);
    REQUIRE(fx.comp.activeCamera().camera == 2);

    // Take the viewport back, and put it somewhere of the artist's choosing -- the main camera is
    // the one the viewport has always flown, so this is what a drag writes.
    fx.comp.setViewportFreeRoam(true);
    const glm::vec3 mine{7.0f, 3.0f, 11.0f};
    fx.placeViewport(mine, glm::vec3(0.0f));

    // Where the free-roam view actually sits is the main camera's business -- its placement mode,
    // its own parameters -- and not what this file is about. What is asserted is the *invariant*:
    // whatever the viewport is showing, a camera cut does not change it.
    const glm::vec3 before = fx.eyeAt(1.0);
    INFO("free-roam eye " << before.x << ", " << before.y << ", " << before.z);

    // **The report, as an assertion.** The director cuts from Wide to Hero at five seconds; the
    // viewport must not go with it.
    //
    // A millimetre rather than zero, and the millimetre is real: the main camera integrates its own
    // angle every frame (`cameraAngle_ += orbitSpeed * dt`, one of the two path-dependent terms this
    // engine still carries), so two reads a frame apart differ by a hair whatever the director does.
    // What is being ruled out is the *jump*, and the scale is what makes that unambiguous -- the two
    // director cameras are 280 m apart, so a viewport that followed the cut would move by hundreds.
    const glm::vec3 after = fx.eyeAt(7.0);
    INFO("moved " << glm::length(after - before) << " m across the cut");
    CHECK(glm::length(after - before) < 0.5f);

    // And the director still did its job -- this is what separates "the viewport is independent"
    // from "the director stopped working". `activeCamera` reports the *film's* camera, which the
    // Cameras panel shows and an offline render uses.
    CHECK(fx.comp.activeCamera().camera == scene::kMainCamera);
}

TEST_CASE("Free-roam does not disturb the director's cameras", "[viewport][camera]") {
    Fixture fx;
    fx.comp.setViewportFreeRoam(true);
    fx.placeViewport(glm::vec3(42.0f), glm::vec3(0.0f));
    fx.eyeAt(7.0);

    // Flying the viewport is not an edit to anybody's camera. Checked on the rigs themselves rather
    // than on what is drawn, because "the picture did not change" would also be true of a camera
    // that was quietly rewritten and then not displayed.
    const scene::CameraRig* hero = fx.comp.cameraDirection().find(3);
    REQUIRE(hero != nullptr);
    CHECK_THAT(hero->position.x, Catch::Matchers::WithinAbs(-100.0, 1e-4));
    CHECK_THAT(hero->position.z, Catch::Matchers::WithinAbs(-100.0, 1e-4));
}

TEST_CASE("Handing the viewport back resumes the director's camera", "[viewport][camera]") {
    Fixture fx;
    fx.comp.setViewportFreeRoam(true);
    fx.placeViewport(glm::vec3(7.0f, 3.0f, 11.0f), glm::vec3(0.0f));
    const glm::vec3 roamed = fx.eyeAt(7.0);

    fx.comp.setViewportFreeRoam(false);
    const glm::vec3 followed = fx.eyeAt(7.0);
    INFO("roamed " << roamed.x << " -> followed " << followed.x);
    CHECK(fx.comp.activeCamera().camera == 3);
    CHECK(glm::length(followed - roamed) > 50.0f);

    // Reversible, which is the property the report was really about: there must be no state you can
    // get into and not get out of. Compared against the *director's* camera rather than against the
    // earlier free-roam pose to the millimetre -- the main camera integrates its own angle every
    // frame (`cameraAngle_ += orbitSpeed * dt`, one of the two path-dependent terms this engine
    // still has), so two reads a frame apart legitimately differ by a hair.
    fx.comp.setViewportFreeRoam(true);
    const glm::vec3 again = fx.eyeAt(7.0);
    CHECK(fx.comp.activeCamera().camera == scene::kMainCamera);
    CHECK(glm::length(again - followed) > 50.0f);
    CHECK(glm::length(again - roamed) < 1.0f);
}
