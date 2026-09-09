// Physical camera: exposure maths, automatic metering, the lens (field of view and circle of
// confusion), focus tracking and the camera/* parameters (ADR-037).

#include "params/parameter_set.hpp"
#include "scene/camera.hpp"
#include "scene/composition_data.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

TEST_CASE("Exposure value follows the photographic triangle", "[scene][camera][exposure]") {
    // EV100 = log2(N^2 / t) - log2(S / 100). Hand-computed reference points:
    // f/1, 1 s, ISO 100 is EV 0 by definition.
    CHECK(scene::exposureValue100(1.0f, 1.0f, 100.0f) == Approx(0.0f).margin(1e-5));
    // f/16, 1/125 s, ISO 100 ("sunny 16"): log2(256 * 125) = log2(32000) = 14.9658.
    CHECK(scene::exposureValue100(16.0f, 1.0f / 125.0f, 100.0f) == Approx(14.9658f).margin(1e-3));
    // f/2.8, 1/60 s, ISO 200: log2(7.84 * 60) - log2(2) = 8.87754 - 1 = 7.87754.
    CHECK(scene::exposureValue100(2.8f, 1.0f / 60.0f, 200.0f) == Approx(7.87754f).margin(1e-3));
    // The engine defaults f/5.6, 1/50 s, ISO 400: log2(31.36 * 50) - 2 = 8.61471 = referenceEv100.
    scene::ExposureSettings defaults;
    CHECK(scene::exposureValue100(defaults) == Approx(defaults.referenceEv100).margin(1e-4));

    // The photometric scale 1 / (1.2 * 2^EV100).
    CHECK(scene::exposureScaleFromEv100(0.0f) == Approx(1.0f / 1.2f).margin(1e-6));
    CHECK(scene::exposureScaleFromEv100(3.0f) == Approx(1.0f / (1.2f * 8.0f)).margin(1e-6));

    // A reflected-light meter: EV100 = log2(L * 100 / 12.5) = log2(L * 8).
    CHECK(scene::meteredEv100(12.5f / 100.0f) == Approx(0.0f).margin(1e-5));
    CHECK(scene::meteredEv100(1.0f) == Approx(3.0f).margin(1e-4));
}

TEST_CASE("Manual exposure is one stop per stop and a no-op at the defaults", "[scene][camera][exposure]") {
    scene::ExposureSettings s;
    CHECK(scene::manualExposureScale(s) == Approx(1.0f).margin(1e-5));

    // Opening from f/5.6 to f/4 is (5.6/4)^2 = 1.96x the light (the f-stop scale is rounded).
    scene::ExposureSettings wider = s;
    wider.aperture = 4.0f;
    CHECK(scene::manualExposureScale(wider) == Approx(1.96f).epsilon(1e-3));
    // One stop of shutter, and one stop of ISO, each double it too.
    scene::ExposureSettings slower = s;
    slower.shutterSeconds = 1.0f / 25.0f;
    CHECK(scene::manualExposureScale(slower) == Approx(2.0f).epsilon(1e-4));
    scene::ExposureSettings faster = s;
    faster.iso = 800.0f;
    CHECK(scene::manualExposureScale(faster) == Approx(2.0f).epsilon(1e-4));
    // Two stops down.
    scene::ExposureSettings stopped = s;
    stopped.aperture = 11.2f;
    CHECK(scene::manualExposureScale(stopped) == Approx(0.25f).epsilon(0.002));
    // Compensation is a plain EV offset on top.
    scene::ExposureSettings compensated = s;
    compensated.compensation = 1.0f;
    CHECK(scene::manualExposureScale(compensated) == Approx(2.0f).epsilon(1e-4));
    compensated.compensation = -2.0f;
    CHECK(scene::manualExposureScale(compensated) == Approx(0.25f).epsilon(1e-4));
}

TEST_CASE("Automatic metering seeds from the manual exposure and converges deterministically",
          "[scene][camera][exposure]") {
    scene::ExposureSettings s;
    s.mode = scene::ExposureSettings::Mode::Automatic;
    s.speedUp = 4.0f;
    s.speedDown = 2.0f;

    SECTION("the first frame holds the manual exposure rather than chasing black") {
        scene::ExposureState state;
        const float scale = scene::updateExposure(state, 0.0f, /*hasMeasurement=*/false, 1.0f / 60.0f, s);
        CHECK(scale == Approx(1.0f).margin(1e-5));
        CHECK(state.seeded);
        CHECK(state.ev100 == Approx(s.referenceEv100).margin(1e-4));
    }

    SECTION("a mid-grey image needs no correction") {
        scene::ExposureState state;
        scene::seedExposure(state, s);
        const float scale = scene::updateAutoExposure(state, 0.18f, 1.0f, s);
        CHECK(state.ev100 == Approx(s.referenceEv100).margin(1e-3));
        CHECK(scale == Approx(1.0f).epsilon(0.005));
    }

    SECTION("a blown-out image darkens, a rate-limited stop at a time") {
        scene::ExposureState state;
        scene::seedExposure(state, s);
        const float target = scene::autoTargetEv100(18.0f, s); // 100x mid grey = 6.64 stops over
        CHECK(target - s.referenceEv100 == Approx(std::log2(100.0f)).margin(1e-3));
        // One 1/60 s frame moves at most speedUp / 60 EV.
        scene::updateAutoExposure(state, 18.0f, 1.0f / 60.0f, s);
        CHECK(state.ev100 == Approx(s.referenceEv100 + s.speedUp / 60.0f).margin(1e-4));
        // Two seconds of frames get there and stop.
        for (int i = 0; i < 240; ++i) {
            scene::updateAutoExposure(state, 18.0f, 1.0f / 60.0f, s);
        }
        CHECK(state.ev100 == Approx(target).margin(1e-3));
        CHECK(scene::appliedExposureScale(state.ev100, s) == Approx(0.01f).epsilon(0.01));
    }

    SECTION("darkening obeys the slower speedDown rate") {
        scene::ExposureState state;
        scene::seedExposure(state, s);
        scene::updateAutoExposure(state, 0.09f, 1.0f / 60.0f, s); // one stop under
        CHECK(state.ev100 == Approx(s.referenceEv100 - s.speedDown / 60.0f).margin(1e-4));
    }

    SECTION("the EV clamps bound the result") {
        scene::ExposureSettings clamped = s;
        clamped.minEv = clamped.referenceEv100; // an auto-exposure used purely as a highlight guard
        clamped.maxEv = clamped.referenceEv100 + 2.0f;
        scene::ExposureState state;
        scene::seedExposure(state, clamped);
        for (int i = 0; i < 600; ++i) {
            scene::updateAutoExposure(state, 1e-4f, 1.0f / 60.0f, clamped); // very dark
        }
        CHECK(state.ev100 == Approx(clamped.minEv).margin(1e-4));
        CHECK(scene::appliedExposureScale(state.ev100, clamped) == Approx(1.0f).margin(1e-4));
        for (int i = 0; i < 600; ++i) {
            scene::updateAutoExposure(state, 1e4f, 1.0f / 60.0f, clamped); // very bright
        }
        CHECK(state.ev100 == Approx(clamped.maxEv).margin(1e-4));
        CHECK(scene::appliedExposureScale(state.ev100, clamped) == Approx(0.25f).epsilon(1e-3));
    }

    SECTION("two runs over the same luminance sequence agree exactly") {
        const std::vector<float> sequence = {0.02f, 0.3f, 5.0f, 40.0f, 41.0f, 2.0f, 0.7f, 0.7f, 0.05f, 12.0f};
        auto run = [&] {
            scene::ExposureState state;
            std::vector<float> trace;
            for (int repeat = 0; repeat < 4; ++repeat) {
                for (const float l : sequence) {
                    trace.push_back(scene::updateExposure(state, l, true, 1.0f / 30.0f, s));
                }
            }
            return trace;
        };
        const auto a = run();
        const auto b = run();
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            INFO("sample " << i);
            CHECK(a[i] == b[i]); // bit-identical, not merely close
        }
        // And it actually moved: the sequence ends far from where it started.
        CHECK(a.back() != a.front());
    }

    SECTION("a zero delta time holds the state still") {
        scene::ExposureState state;
        scene::seedExposure(state, s);
        const float before = state.ev100;
        scene::updateAutoExposure(state, 100.0f, 0.0f, s);
        CHECK(state.ev100 == before);
    }
}

TEST_CASE("Lens: focal length to field of view, and the analytic circle of confusion",
          "[scene][camera][lens]") {
    scene::LensSettings lens;
    lens.sensorWidth = 36.0f;
    lens.sensorHeight = 24.0f;

    SECTION("field of view") {
        lens.focalLength = 35.0f;
        CHECK(lens.fovYRadians() == Approx(2.0f * std::atan(24.0f / 70.0f)).margin(1e-6));
        CHECK(lens.fovYRadians() == Approx(0.660595f).margin(1e-5)); // 37.85 degrees
        CHECK(lens.fovXRadians() == Approx(2.0f * std::atan(36.0f / 70.0f)).margin(1e-6));
        lens.focalLength = 50.0f;
        CHECK(lens.fovYRadians() == Approx(0.471090f).margin(1e-5)); // 27.0 degrees
        lens.focalLength = 24.0f;
        CHECK(lens.fovYRadians() == Approx(0.927295f).margin(1e-5)); // 53.1 degrees, an establisher
        // Longer lens, narrower angle, monotonically.
        CHECK(scene::LensSettings{.focalLength = 85.0f}.fovYRadians() <
              scene::LensSettings{.focalLength = 35.0f}.fovYRadians());
    }

    SECTION("circle of confusion, c = f^2 |d - s| / (N d (s - f))") {
        lens.focalLength = 50.0f;
        lens.aperture = 2.8f;
        lens.focusDistance = 5.0f; // metres
        // In focus: zero.
        CHECK(lens.circleOfConfusion(5.0f) == Approx(0.0f).margin(1e-5));
        // At 10 m: 2500 * 5000 / (2.8 * 10000 * 4950) = 0.0901876 mm.
        CHECK(lens.circleOfConfusion(10.0f) == Approx(0.0901876f).margin(1e-5));
        // At 2.5 m (nearer than focus): 2500 * 2500 / (2.8 * 2500 * 4950) = 0.180375 mm.
        CHECK(lens.circleOfConfusion(2.5f) == Approx(0.180375f).margin(1e-5));
        // Stopping down two stops (f/2.8 -> f/5.6) halves it.
        scene::LensSettings stopped = lens;
        stopped.aperture = 5.6f;
        CHECK(stopped.circleOfConfusion(10.0f) == Approx(lens.circleOfConfusion(10.0f) * 0.5f).epsilon(1e-4));
        // It grows monotonically away from focus and saturates at infinity.
        CHECK(lens.circleOfConfusion(6.0f) < lens.circleOfConfusion(8.0f));
        CHECK(lens.circleOfConfusion(1e6f) == Approx(2500.0f / (2.8f * 4950.0f)).epsilon(1e-3));
        // In pixels: mm on the sensor scaled by pixels per millimetre of sensor height.
        CHECK(lens.circleOfConfusionPixels(10.0f, 1080.0f) ==
              Approx(0.0901876f * 1080.0f / 24.0f).epsilon(1e-4));
    }

    SECTION("effectiveFovY honours useExplicitFov so pre-lens scenes are untouched") {
        scene::Camera camera;
        camera.fovYRadians = 0.87f;
        camera.lens.focalLength = 85.0f;
        camera.lens.useExplicitFov = true;
        CHECK(camera.effectiveFovY() == Approx(0.87f).margin(1e-6));
        camera.lens.useExplicitFov = false;
        CHECK(camera.effectiveFovY() == Approx(camera.lens.fovYRadians()).margin(1e-6));
        CHECK(camera.effectiveFovY() < 0.87f);
    }
}

TEST_CASE("Focus tracking follows a target at a limited speed", "[scene][camera][focus]") {
    scene::Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    scene::CompositionData composition;
    scene::FocusSettings focus;

    SECTION("fixed mode returns the lens's own focus distance") {
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(7.5f));
    }
    SECTION("point mode measures to a world position") {
        focus.mode = scene::FocusSettings::Mode::Point;
        focus.point = {3.0f, 4.0f, 0.0f};
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(5.0f));
    }
    SECTION("focal mode uses the composition, and falls back when there is none") {
        focus.mode = scene::FocusSettings::Mode::Focal;
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(7.5f));
        scene::FocalPoint near;
        near.name = "near";
        near.position = {0.0f, 0.0f, -6.0f};
        near.weight = 0.5f;
        scene::FocalPoint hero;
        hero.name = "hero";
        hero.position = {0.0f, 0.0f, -20.0f};
        hero.weight = 2.0f;
        composition.focalPoints = {near, hero};
        // No name and no camera target: the heaviest point wins.
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(20.0f));
        // The composition's own camera target is next in precedence.
        composition.cameraTarget = "near";
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(6.0f));
        // An explicit name wins over both.
        focus.name = "hero";
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(20.0f));
        // A name that does not resolve falls back to the heaviest point, not to an error.
        focus.name = "missing";
        CHECK(scene::focusTargetDistance(focus, camera, composition, 7.5f) == Approx(20.0f));
    }

    SECTION("the tracker snaps on the first frame and then rate-limits") {
        scene::FocusState state;
        CHECK(scene::updateFocus(state, 12.0f, 1.0f / 60.0f, 4.0f) == Approx(12.0f)); // seed: instant
        CHECK(scene::updateFocus(state, 20.0f, 1.0f, 4.0f) == Approx(16.0f));         // 4 m in 1 s
        CHECK(scene::updateFocus(state, 20.0f, 1.0f, 4.0f) == Approx(20.0f));
        CHECK(scene::updateFocus(state, 20.0f, 1.0f, 4.0f) == Approx(20.0f)); // and stays
        CHECK(scene::updateFocus(state, 0.0f, 1.0f, 4.0f) == Approx(16.0f));  // symmetric
        // Speed 0 means instant.
        CHECK(scene::updateFocus(state, 3.0f, 1.0f, 0.0f) == Approx(3.0f));
    }
}

TEST_CASE("Camera parameters register and apply finals", "[scene][camera][params]") {
    params::ParameterSet params;
    scene::LensSettings lens;
    scene::ExposureSettings exposure;
    scene::FocusSettings focus;
    auto p = scene::registerCameraParameters(params, lens, exposure, focus);
    REQUIRE(p.focalLength != nullptr);
    CHECK(params.size() == 20);
    CHECK(params.find("camera/lens/focalLength") != nullptr);
    CHECK(params.find("camera/exposure/mode")->kind() == params::ParamKind::Int);
    CHECK(params.find("camera/focus/point")->componentCount() == 3);

    p.focalLength->setBase(85.0f);
    p.useExplicitFov->setBase(false);
    p.exposureMode->setBase(1);
    p.compensation->setBase(-1.5f);
    p.focusMode->setBase(2);
    p.focusSpeed->setBase(6.0f);
    params.resetFinals();
    scene::applyCameraParameters(p, lens, exposure, focus);
    CHECK(lens.focalLength == 85.0f);
    CHECK_FALSE(lens.useExplicitFov);
    CHECK(exposure.mode == scene::ExposureSettings::Mode::Automatic);
    CHECK(exposure.compensation == -1.5f);
    CHECK(focus.mode == scene::FocusSettings::Mode::Focal);
    CHECK(focus.speed == 6.0f);
    CHECK(std::string(scene::exposureModeName(exposure.mode)) == "automatic");
    CHECK(std::string(scene::focusModeName(focus.mode)) == "focal");

    // Re-registering returns the same parameters (a scene swap keeps the live values).
    auto again = scene::registerCameraParameters(params, lens, exposure, focus);
    CHECK(again.focalLength == p.focalLength);
    CHECK(params.size() == 20);
}

// A scene's authored `camera.fov` governs framing: `LensSettings::useExplicitFov` defaults true,
// so a focal length in the project drives depth of field without silently re-framing the shot.
// Worth pinning down: it was reported the other way round during the cinematic pass, and the two
// readings imply very different shots.
TEST_CASE("An authored field of view outranks the lens until the scene says otherwise", "[camera][lens]") {
    scene::Camera cam;
    cam.fovYRadians = glm::radians(52.0f);
    cam.lens.focalLength = 42.0f;
    cam.lens.sensorWidth = 36.0f;
    cam.lens.sensorHeight = 24.0f;

    CHECK(cam.lens.useExplicitFov);
    CHECK(glm::degrees(cam.effectiveFovY()) == Catch::Approx(52.0f).margin(1e-3));

    // The lens's own angle is a different number, so this is a real choice rather than a
    // coincidence: 42 mm on a 24 mm-high sensor is about 32 degrees.
    CHECK(glm::degrees(cam.lens.fovYRadians()) == Catch::Approx(31.86f).margin(0.1));

    // Turning the flag off hands framing to the lens.
    cam.lens.useExplicitFov = false;
    CHECK(glm::degrees(cam.effectiveFovY()) == Catch::Approx(31.86f).margin(0.1));

    // Either way the focal length still sets the circle of confusion, so depth of field follows
    // the lens even when the framing does not.
    cam.lens.useExplicitFov = true;
    cam.lens.focusDistance = 10.0f;
    const float near = cam.lens.circleOfConfusion(5.0f);
    const float far = cam.lens.circleOfConfusion(40.0f);
    CHECK(near > 0.0f);
    CHECK(far > 0.0f);
    CHECK(cam.lens.circleOfConfusion(10.0f) == Catch::Approx(0.0f).margin(1e-6));
}
