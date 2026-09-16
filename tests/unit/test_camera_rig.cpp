// Multiple cameras, and the one that is on screen (ADR-245).
//
// The three things this file checks are the three things the design is made of, and they are
// checked separately on purpose:
//
//   * the **collection** -- stable ids, unique parameter slugs, a shot that goes with its camera,
//     and a document that refuses what it cannot evaluate;
//   * the **director** -- a pure function from (cameras, shots, events, t) to which camera is live,
//     including the priority order, the half-open boundary, the blend and the fallback;
//   * the **maths** -- what a cross-fade interpolates, and the one thing it must not.
//
// Every case here that asserts a *choice* is written so that the obvious wrong implementation fails
// it (ADR-182). The control arms run in docs/decisions/ADR-245: first-match shot selection, events
// not outranking shots, a blend that reaches back more than one claim, and a removed camera that
// leaves its parameters behind were each made to fail one of these before the code was written the
// right way round.

#include "scene/camera_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

scene::CameraDirection twoCameras() {
    scene::CameraDirection d;
    d.ensureMainCamera();
    scene::CameraRig wide;
    wide.name = "Valley Wide";
    wide.position = glm::vec3(0.0f, 60.0f, 200.0f);
    wide.target = glm::vec3(0.0f, 10.0f, 0.0f);
    wide.focalLength = 24.0f;
    d.addCamera(std::move(wide));
    return d;
}

} // namespace

// ---- the collection ------------------------------------------------------------------------------

TEST_CASE("A fresh camera collection is one camera and changes nothing", "[camera][multicam]") {
    scene::CameraDirection d;
    d.ensureMainCamera();
    REQUIRE(d.cameras.size() == 1);
    CHECK(d.cameras[0].id == scene::kMainCamera);
    CHECK(d.cameras[0].channelPrefix() == "camera/");
    // The whole backward-compatibility claim in one assertion: nothing is directing, so the
    // composition takes the path it took before this system existed.
    CHECK_FALSE(d.directing());
    const auto state = scene::resolveActiveCamera(d, {}, 12.5);
    CHECK(state.camera == scene::kMainCamera);
    CHECK(state.reason == scene::ActiveCameraReason::Default);
    CHECK(state.blend == 1.0f);
}

TEST_CASE("Camera ids are stable and slugs are unique", "[camera][multicam]") {
    scene::CameraDirection d;
    scene::CameraRig a;
    a.name = "Hero Free Roam";
    const scene::CameraId first = d.addCamera(a);
    scene::CameraRig b;
    b.name = "Hero Free Roam"; // the same display name on purpose
    const scene::CameraId second = d.addCamera(b);
    CHECK(first != second);
    CHECK(d.find(first)->slug == "herofreeroam");
    // Two cameras sharing a slug would share their parameters, and so their animation. The second
    // one gets a different path rather than the first one's keys.
    CHECK(d.find(second)->slug == "herofreeroam2");
    CHECK(d.find(first)->channelPrefix() == "cameras/herofreeroam/");
    CHECK(d.find(second)->channelPrefix() == "cameras/herofreeroam2/");

    // An id is never reused: a shot saved against a deleted camera must not come back pointing at
    // a stranger.
    REQUIRE(d.removeCamera(second));
    scene::CameraRig c;
    c.name = "Third";
    CHECK(d.addCamera(c) != second);
}

TEST_CASE("Deleting a camera takes its shots; the main camera cannot be deleted", "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 0.0, .endSeconds = 4.0});
    d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera, .startSeconds = 4.0, .endSeconds = 8.0});
    REQUIRE(d.validate().has_value());

    CHECK_FALSE(d.removeCamera(scene::kMainCamera));
    CHECK(d.cameras.size() == 2);

    REQUIRE(d.removeCamera(wide));
    CHECK(d.cameras.size() == 1);
    // A shot naming a camera that is gone resolves to nothing and falls through to the default,
    // which on screen is indistinguishable from the shot list being ignored.
    CHECK(d.shots.size() == 1);
    CHECK(d.validate().has_value());
}

TEST_CASE("A collection that cannot be evaluated is refused", "[camera][multicam]") {
    SECTION("a shot naming a camera that does not exist") {
        scene::CameraDirection d;
        d.ensureMainCamera();
        d.shots.push_back(scene::CameraShot{.camera = 99, .startSeconds = 0.0, .endSeconds = 1.0});
        REQUIRE_FALSE(d.validate().has_value());
    }
    SECTION("a shot that ends before it starts") {
        scene::CameraDirection d;
        d.ensureMainCamera();
        d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera, .startSeconds = 4.0, .endSeconds = 2.0});
        REQUIRE_FALSE(d.validate().has_value());
    }
    SECTION("two cameras with the same slug") {
        scene::CameraDirection d = twoCameras();
        scene::CameraRig clash = d.cameras.back();
        clash.id = d.nextId++;
        d.cameras.push_back(clash);
        REQUIRE_FALSE(d.validate().has_value());
    }
    SECTION("an id the counter would hand out again") {
        scene::CameraDirection d = twoCameras();
        d.nextId = scene::kMainCamera + 1;
        REQUIRE_FALSE(d.validate().has_value());
    }
    SECTION("a spline camera that names no spline") {
        scene::CameraDirection d;
        scene::CameraRig s;
        s.name = "Rail";
        s.placement = scene::CameraPlacement::Spline;
        d.addCamera(s);
        REQUIRE_FALSE(d.validate().has_value());
    }
}

TEST_CASE("A camera collection survives a save and a load", "[camera][multicam]") {
    // ADR-225: a setting the application does not keep is not a setting.
    scene::CameraDirection d = twoCameras();
    scene::CameraRig ufo;
    ufo.name = "UFO Watch";
    ufo.eventScenario = "abduction";
    ufo.eventLeadSeconds = 0.4;
    ufo.eventTailSeconds = 1.25;
    ufo.priority = 7;
    ufo.autoDirectorEligible = true;
    ufo.focalLength = 50.0f;
    const scene::CameraId ufoId = d.addCamera(ufo);
    d.shots.push_back(scene::CameraShot{.camera = d.cameras[1].id,
                                        .startSeconds = 0.0,
                                        .endSeconds = 6.0,
                                        .transition = scene::ShotTransition::Blend,
                                        .blendSeconds = 1.5,
                                        .label = "opening"});
    d.defaultCamera = scene::kMainCamera;
    REQUIRE(d.validate().has_value());

    const nlohmann::json doc = d.toJson();
    auto back = scene::CameraDirection::fromJson(doc);
    REQUIRE(back.has_value());
    CHECK(*back == d);
    // And again, so the second save is the same document as the first.
    CHECK(back->toJson() == doc);

    // The control arm for this test is a field that is written and not read: dropping `eventTail`
    // from `toJson` makes exactly this line go red and nothing else.
    CHECK(back->find(ufoId)->eventTailSeconds == 1.25);
    CHECK(back->find(ufoId)->priority == 7);
    CHECK(back->shots.front().blendSeconds == 1.5);
    CHECK(back->shots.front().transition == scene::ShotTransition::Blend);
    CHECK(back->nextId == d.nextId);
}

TEST_CASE("A document that cannot be evaluated does not load", "[camera][multicam]") {
    const auto doc = nlohmann::json::parse(R"({
        "cameras": [{"id": 1, "name": "Main"}],
        "shots": [{"camera": 42, "start": 0.0, "end": 2.0}],
        "default": 1, "nextId": 2
    })");
    REQUIRE_FALSE(scene::CameraDirection::fromJson(doc).has_value());
}

TEST_CASE("A scene document with no camera block loads as one camera", "[camera][multicam]") {
    auto empty = scene::CameraDirection::fromJson(nlohmann::json::object());
    REQUIRE(empty.has_value());
    CHECK(empty->cameras.size() == 1);
    CHECK(empty->cameras[0].id == scene::kMainCamera);
    CHECK_FALSE(empty->directing());
}

// ---- the director ---------------------------------------------------------------------------------

TEST_CASE("An authored shot claims the frame for its span and no longer", "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 2.0, .endSeconds = 6.0});

    CHECK(scene::resolveActiveCamera(d, {}, 1.999).camera == scene::kMainCamera);
    // Half-open, like every other span in this engine: the shot owns its start instant.
    CHECK(scene::resolveActiveCamera(d, {}, 2.0).camera == wide);
    CHECK(scene::resolveActiveCamera(d, {}, 5.999).camera == wide);
    // ...and not its end instant, so two shots that meet exactly do not both claim the join.
    CHECK(scene::resolveActiveCamera(d, {}, 6.0).camera == scene::kMainCamera);

    const auto mid = scene::resolveActiveCamera(d, {}, 4.0);
    CHECK(mid.reason == scene::ActiveCameraReason::Shot);
    CHECK(mid.name == "Valley Wide");
    CHECK(mid.sinceSeconds == 2.0);
    CHECK(mid.untilSeconds == 6.0);
    // The camera's optical identity travels with the claim, so the frame's lens knows what it is on.
    CHECK(mid.focalLength == 24.0f);

    // A gap is a named state, not a silent hand-off: the default camera has it, and says so.
    const auto gap = scene::resolveActiveCamera(d, {}, 9.0);
    CHECK(gap.reason == scene::ActiveCameraReason::Default);
    CHECK(gap.camera == d.defaultCamera);
}

TEST_CASE("Overlapping shots resolve to the last one in the list", "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 0.0, .endSeconds = 10.0});
    d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera, .startSeconds = 4.0, .endSeconds = 6.0});
    // The sequencer convention, and the only rule under which dragging a shot on top of another
    // does what it looks like. A first-match implementation answers `wide` here.
    CHECK(scene::resolveActiveCamera(d, {}, 5.0).camera == scene::kMainCamera);
    CHECK(scene::resolveActiveCamera(d, {}, 3.0).camera == wide);
    CHECK(scene::resolveActiveCamera(d, {}, 7.0).camera == wide);
}

TEST_CASE("A running event outranks an authored shot and returns it afterwards", "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    scene::CameraRig ufo;
    ufo.name = "UFO Watch";
    ufo.eventScenario = "abduction";
    ufo.eventLeadSeconds = 0.5;
    ufo.eventTailSeconds = 1.0;
    const scene::CameraId ufoId = d.addCamera(ufo);
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 0.0, .endSeconds = 30.0});

    const std::vector<scene::CameraEventSpan> events{{"abduction", 10.0, 14.0}};

    CHECK(scene::resolveActiveCamera(d, events, 9.0).camera == wide);
    // The lead is the difference between seeing the event and seeing its aftermath.
    CHECK(scene::resolveActiveCamera(d, events, 9.6).camera == ufoId);
    CHECK(scene::resolveActiveCamera(d, events, 12.0).camera == ufoId);
    CHECK(scene::resolveActiveCamera(d, events, 14.5).camera == ufoId); // inside the tail
    // And then the frame goes back to whatever the piece said it should be, with no memory of the
    // event needed to get there: resolution simply falls through again.
    const auto after = scene::resolveActiveCamera(d, events, 15.5);
    CHECK(after.camera == wide);
    CHECK(after.reason == scene::ActiveCameraReason::Shot);

    const auto during = scene::resolveActiveCamera(d, events, 12.0);
    CHECK(during.reason == scene::ActiveCameraReason::Event);
    CHECK(during.eventName == "abduction");
}

TEST_CASE("An event that has not finished keeps the frame", "[camera][multicam]") {
    // A staging scenario is live state: what has started is known, what will stop is not. An open
    // span (end <= start) is how the composition says "still running".
    scene::CameraDirection d;
    scene::CameraRig ufo;
    ufo.name = "UFO Watch";
    ufo.eventScenario = "abduction";
    ufo.eventLeadSeconds = 0.0;
    const scene::CameraId ufoId = d.addCamera(ufo);
    const std::vector<scene::CameraEventSpan> open{{"abduction", 10.0, 10.0}};
    CHECK(scene::resolveActiveCamera(d, open, 9.9).camera == scene::kMainCamera);
    CHECK(scene::resolveActiveCamera(d, open, 10.0).camera == ufoId);
    CHECK(scene::resolveActiveCamera(d, open, 600.0).camera == ufoId);
}

TEST_CASE("Two event cameras are separated by priority, then by id", "[camera][multicam]") {
    scene::CameraDirection d;
    scene::CameraRig low;
    low.name = "Low";
    low.eventScenario = "abduction";
    low.eventLeadSeconds = 0.0;
    low.priority = 1;
    const scene::CameraId lowId = d.addCamera(low);
    scene::CameraRig high;
    high.name = "High";
    high.eventScenario = "abduction";
    high.eventLeadSeconds = 0.0;
    high.priority = 9;
    const scene::CameraId highId = d.addCamera(high);
    const std::vector<scene::CameraEventSpan> events{{"abduction", 0.0, 5.0}};
    CHECK(scene::resolveActiveCamera(d, events, 2.0).camera == highId);

    // Equal priority falls back to the lower id rather than to vector order, so re-ordering the
    // camera list in the UI cannot change the film.
    d.find(highId)->priority = 1;
    CHECK(scene::resolveActiveCamera(d, events, 2.0).camera == lowId);
    std::ranges::reverse(d.cameras);
    CHECK(scene::resolveActiveCamera(d, events, 2.0).camera == lowId);
}

TEST_CASE("A blend ramps from the outgoing camera and ends exactly on the incoming one",
          "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    d.shots.push_back(scene::CameraShot{.camera = wide,
                                        .startSeconds = 4.0,
                                        .endSeconds = 10.0,
                                        .transition = scene::ShotTransition::Blend,
                                        .blendSeconds = 2.0});

    const auto before = scene::resolveActiveCamera(d, {}, 3.9);
    CHECK(before.camera == scene::kMainCamera);
    CHECK_FALSE(before.blending());

    const auto start = scene::resolveActiveCamera(d, {}, 4.0);
    CHECK(start.camera == wide);
    CHECK(start.previous == scene::kMainCamera);
    CHECK_THAT(start.blend, WithinAbs(0.0f, 1e-5f));
    CHECK(start.blending());

    CHECK_THAT(scene::resolveActiveCamera(d, {}, 5.0).blend, WithinAbs(0.5f, 1e-5f));
    // Past the blend the outgoing camera is gone entirely -- which is what makes the cost of a
    // finished blend exactly nothing.
    const auto done = scene::resolveActiveCamera(d, {}, 6.5);
    CHECK_FALSE(done.blending());
    CHECK(done.previous == done.camera);
    CHECK(done.blend == 1.0f);
}

TEST_CASE("A cut does not blend, and a blend onto the same camera is not a blend",
          "[camera][multicam]") {
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    SECTION("a cut") {
        d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 4.0, .endSeconds = 10.0});
        CHECK_FALSE(scene::resolveActiveCamera(d, {}, 4.1).blending());
    }
    SECTION("a blend onto the camera that was already live") {
        d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera,
                                            .startSeconds = 4.0,
                                            .endSeconds = 10.0,
                                            .transition = scene::ShotTransition::Blend,
                                            .blendSeconds = 2.0});
        // Cross-fading a camera with itself is a two-second stretch of arithmetic that changes no
        // pixel. The resolver reports a finished blend instead.
        const auto state = scene::resolveActiveCamera(d, {}, 4.5);
        CHECK_FALSE(state.blending());
        CHECK(state.blend == 1.0f);
    }
}

TEST_CASE("The director is a pure function of the clock", "[camera][multicam]") {
    // ADR-091: an offline render must reproduce a live one exactly, and a scrub must land where a
    // play-through would. That is only true if resolution depends on `seconds` and nothing else.
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    scene::CameraRig ufo;
    ufo.name = "UFO Watch";
    ufo.eventScenario = "abduction";
    const scene::CameraId ufoId = d.addCamera(ufo);
    d.shots.push_back(scene::CameraShot{.camera = wide,
                                        .startSeconds = 2.0,
                                        .endSeconds = 9.0,
                                        .transition = scene::ShotTransition::Blend,
                                        .blendSeconds = 1.0});
    d.shots.push_back(scene::CameraShot{.camera = scene::kMainCamera, .startSeconds = 9.0, .endSeconds = 20.0});
    const std::vector<scene::CameraEventSpan> events{{"abduction", 12.0, 15.0}};

    std::vector<double> times;
    for (int i = 0; i <= 400; ++i) {
        times.push_back(static_cast<double>(i) * 0.05);
    }
    std::vector<scene::ActiveCameraState> forward;
    forward.reserve(times.size());
    for (const double t : times) {
        forward.push_back(scene::resolveActiveCamera(d, events, t));
    }

    // The same instants asked for in a shuffled order, which is what scrubbing is.
    std::vector<std::size_t> order(times.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::mt19937 rng(20260916u);
    std::ranges::shuffle(order, rng);
    for (const std::size_t i : order) {
        REQUIRE(scene::resolveActiveCamera(d, events, times[i]) == forward[i]);
    }
    // And the run does contain the interesting states, so the invariance above is not vacuous.
    CHECK(std::ranges::any_of(forward, [](const auto& s) { return s.blending(); }));
    CHECK(std::ranges::any_of(forward, [ufoId](const auto& s) { return s.camera == ufoId; }));
    CHECK(std::ranges::any_of(forward, [](const auto& s) {
        return s.reason == scene::ActiveCameraReason::Default;
    }));
}

// ---- the maths -------------------------------------------------------------------------------------

TEST_CASE("A cross-fade interpolates the picture, and a stated lens is never dragged to zero",
          "[camera][multicam]") {
    scene::CameraPose a;
    a.position = glm::vec3(0.0f);
    a.target = glm::vec3(0.0f, 0.0f, -10.0f);
    a.fovDegrees = 30.0f;
    a.focalLength = 24.0f;
    scene::CameraPose b;
    b.position = glm::vec3(10.0f, 0.0f, 0.0f);
    b.target = glm::vec3(10.0f, 0.0f, -10.0f);
    b.fovDegrees = 50.0f;
    b.focalLength = 84.0f;

    const scene::CameraPose mid = scene::blendPoses(a, b, 0.5f);
    CHECK_THAT(mid.position.x, WithinAbs(5.0f, 1e-5f));
    CHECK_THAT(mid.fovDegrees, WithinAbs(40.0f, 1e-5f));
    CHECK_THAT(mid.focalLength, WithinAbs(54.0f, 1e-5f));

    // One end with no opinion about its lens must not sweep the other towards a pinhole. The value
    // switches at the halfway point instead of being averaged with a zero that means "unset".
    b.focalLength = 0.0f;
    CHECK_THAT(scene::blendPoses(a, b, 0.25f).focalLength, WithinAbs(24.0f, 1e-5f));
    CHECK_THAT(scene::blendPoses(a, b, 0.75f).focalLength, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("A camera aimed at its own eye is given a direction", "[camera][multicam]") {
    scene::CameraPose pose;
    pose.position = glm::vec3(3.0f, 4.0f, 5.0f);
    pose.target = pose.position;
    scene::ensureDistinctAim(pose);
    CHECK(glm::length(pose.target - pose.position) > 0.5f);
}

TEST_CASE("A camera slug is a usable parameter path segment", "[camera][multicam]") {
    CHECK(scene::cameraSlug("Valley Wide") == "valleywide");
    CHECK(scene::cameraSlug("UFO / Watch #2") == "ufowatch2");
    CHECK(scene::cameraSlug("") == "camera");
    CHECK(scene::cameraSlug("!!!") == "camera");
    // A path segment that begins with a digit reads as an index everywhere a path is shown.
    CHECK(scene::cameraSlug("2nd Unit") == "c2ndunit");
}

TEST_CASE("A locked shot is not taken by an event", "[camera][multicam]") {
    // The author's veto. Without it a world whose events run continuously -- Glowmere's saucer
    // abducts something every nineteen seconds -- can never show an authored establishing shot.
    scene::CameraDirection d = twoCameras();
    const scene::CameraId wide = d.cameras.back().id;
    scene::CameraRig ufo;
    ufo.name = "UFO Watch";
    ufo.eventScenario = "abduction";
    ufo.eventLeadSeconds = 0.0;
    const scene::CameraId ufoId = d.addCamera(ufo);
    d.shots.push_back(
        scene::CameraShot{.camera = wide, .startSeconds = 0.0, .endSeconds = 6.0, .locked = true});
    d.shots.push_back(scene::CameraShot{.camera = wide, .startSeconds = 6.0, .endSeconds = 12.0});
    const std::vector<scene::CameraEventSpan> events{{"abduction", 0.0, 30.0}};

    // Inside the locked shot the event is refused, and the reason says the shot has it.
    const auto locked = scene::resolveActiveCamera(d, events, 3.0);
    CHECK(locked.camera == wide);
    CHECK(locked.reason == scene::ActiveCameraReason::Shot);
    // The instant the lock ends, the event that was waiting takes the frame.
    const auto freed = scene::resolveActiveCamera(d, events, 6.0);
    CHECK(freed.camera == ufoId);
    CHECK(freed.reason == scene::ActiveCameraReason::Event);
}
