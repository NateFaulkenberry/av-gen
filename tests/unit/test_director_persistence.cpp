// The persistence gate for everything the Director will write (director-system-progress.md,
// Slice 0.3; spec §7).
//
// The Director compiles a plan into ordinary engine content: a `seq::Sequence` (shots with their
// cameras, actors with keys, a path and clip cues, markers, events, piece tracks), the composition's
// `scene::CameraDirection` (rigs and the camera track), and author timeline keys. A plan that
// compiled into content a save then dropped would be worse than no Director at all, because the
// user would approve a diff and later find half of it gone. So each domain is written here the way
// the compiler will write it, saved through the real save path after the engine has run a frame
// (see support/project_round_trip.hpp for why the frame matters), reloaded into a fresh engine, and
// compared.
//
// Two scene shapes, because the project writes them differently (ADR-264, ADR-271, ADR-276):
//
//   inline        the composition has no file, so the project carries `Composition::toJson`
//   by reference  the composition came from a scene file, so the project carries a path and a hash
//                 and ONLY the families somebody remembered to add as override keys
//
// The by-reference shape is the one every shipped world uses (Glowmere Valley 2 multicam among
// them), and it is the one with a history of silently dropping state: world effects (ADR-207),
// atmospheric effects (ADR-230), heroes (ADR-276), lights.

#include "app/engine.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "seq/events.hpp"
#include "seq/sequence.hpp"
#include "support/gltf_fixture.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A sequence shaped like the Director's output for "a 5 s shot at 1:30 following Rook": one shot
// with a behaviour camera, one actor with keys, a path and three clip cues, two cue markers, one
// scheduled event keyed to a marker, and one piece-level track.
seq::Sequence directorShapedSequence() {
    seq::Sequence piece;
    piece.name = "director-probe";

    seq::Shot shot;
    shot.name = "rook-umbra";
    shot.startSeconds = 90.0;
    shot.durationSeconds = 5.0;
    shot.camera.kind = seq::CameraKind::Behavior;
    shot.camera.behavior.kind = seq::CameraBehaviorKind::Chase;
    shot.camera.behavior.actor = "rook";
    shot.camera.behavior.offset = glm::vec3(0.0f, 0.4f, -3.0f);
    shot.camera.behavior.lagSeconds = 0.1;
    shot.camera.behavior.clearance = 0.5f;
    shot.in = seq::Transition{seq::TransitionKind::Cut, 0.0};
    piece.shots.push_back(shot);

    // A second shot with hand-authored camera keys: what the compiler emits when no behaviour can
    // express a move (spec §15 prefers behaviours, but keys are the fallback and must survive too).
    seq::Shot keyed;
    keyed.name = "rook-pass";
    keyed.startSeconds = 95.0;
    keyed.durationSeconds = 2.0;
    keyed.camera.kind = seq::CameraKind::Keys;
    seq::CameraKey k0;
    k0.timeSeconds = 0.0;
    k0.position = glm::vec3(2.0f, 1.0f, -3.0f);
    k0.target = glm::vec3(0.0f, 1.0f, 0.0f);
    seq::CameraKey k1 = k0;
    k1.timeSeconds = 2.0;
    k1.position = glm::vec3(4.0f, 4.0f, 5.0f);
    keyed.camera.keys = {k0, k1};
    keyed.in = seq::Transition{seq::TransitionKind::MatchCut, 0.0};
    piece.shots.push_back(keyed);

    seq::Actor rook;
    rook.id = "rook";
    rook.keys.push_back(seq::ActorKey{90.0, glm::vec3(1.0f, 0.0f, 2.0f), std::nullopt, std::nullopt,
                                      params::KeyInterp::Linear});
    rook.keys.push_back(seq::ActorKey{95.0, glm::vec3(20.0f, 0.0f, 9.0f), glm::vec3(0.0f, 45.0f, 0.0f),
                                      std::nullopt, params::KeyInterp::Smooth});
    rook.path.active = true;
    rook.path.startSeconds = 90.0;
    rook.path.endSeconds = 92.0;
    for (const glm::vec3 p : {glm::vec3(1.0f, 0.0f, 2.0f), glm::vec3(8.0f, 0.0f, 4.0f),
                              glm::vec3(12.0f, 0.0f, 6.0f)}) {
        spatial::SplinePoint point;
        point.position = p;
        rook.path.spline.points.push_back(point);
    }
    rook.clips.push_back(seq::ClipCue{90.0, "Running", 1.0f, 0.2f});
    rook.clips.push_back(seq::ClipCue{92.1, "Jump_running", 1.0f, 0.1f});
    rook.clips.push_back(seq::ClipCue{93.0, "Landing", 0.9f, -1.0f});
    piece.actors.push_back(rook);

    piece.markers.push_back(seq::Marker{92.55, "rook.jump_peak", seq::MarkerKind::Cue});
    piece.markers.push_back(seq::Marker{93.0, "rook.landed", seq::MarkerKind::Cue});

    seq::SequenceEvent pulse;
    pulse.id = "peak-echo";
    pulse.when.kind = seq::TriggerKind::Cue;
    pulse.when.name = "rook.jump_peak";
    pulse.what.kind = seq::EventActionKind::SetParameter;
    pulse.what.target = "scene/brightness";
    pulse.what.amount = glm::vec4(1.4f, 0.0f, 0.0f, 0.0f);
    pulse.what.seconds = 0.2;
    pulse.what.holdSeconds = 0.5;
    piece.events.push_back(pulse);

    params::Track track;
    track.target = "scene/brightness";
    track.keys.push_back(params::Key{89.0, params::KeyValue{1.0f, 0.0f, 0.0f, 0.0f}});
    track.keys.push_back(params::Key{96.0, params::KeyValue{1.2f, 0.0f, 0.0f, 0.0f}});
    piece.tracks.push_back(track);
    return piece;
}

// A camera collection shaped like the Director's: one new rig aimed at a node, one locked shot on
// the camera track naming it.
scene::CameraDirection withDirectorCamera(scene::CameraDirection direction) {
    scene::CameraRig rig;
    rig.name = "Rook Chase";
    rig.position = glm::vec3(3.0f, 1.0f, -6.0f);
    rig.target = glm::vec3(0.0f, 1.0f, 0.0f);
    rig.focalLength = 24.0f;
    const scene::CameraId id = direction.addCamera(rig);
    scene::CameraShot shot;
    shot.camera = id;
    shot.startSeconds = 90.0;
    shot.endSeconds = 95.0;
    shot.transition = scene::ShotTransition::Cut;
    shot.locked = true;
    shot.label = "rook-umbra";
    direction.shots.push_back(shot);
    return direction;
}

// One glTF node in a composition, saved to a scene file so the project that follows references it.
struct ByReferenceScene {
    testsupport::ScratchDir dir{"director_persistence"};
    fs::path scene = dir / "world.scene.json";
    fs::path project = dir / "world.json";
    app::Engine engine{app::EngineMode::Offline};

    ByReferenceScene() {
        const auto tmp = testsupport::writeTriangleGlb("director_persistence");
        const fs::path glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        engine.newComposition();
        scene::CompositionNode node;
        node.name = "rook";
        node.kind = scene::NodeKind::Gltf;
        node.asset = glb.generic_string();
        REQUIRE(engine.addNode(std::move(node)).has_value());
        REQUIRE(engine.saveComposition(scene).has_value());
        REQUIRE_FALSE(engine.compositionPath().empty());
    }
};

} // namespace

TEST_CASE("a Director-shaped sequence survives a project save and reload",
          "[directing][persistence]") {
    ByReferenceScene fixture;
    const seq::Sequence authored = directorShapedSequence();
    REQUIRE(fixture.engine.setSequence(authored).has_value());
    const json expected = fixture.engine.sequence().toJson();

    auto trip = testsupport::saveAndReload(fixture.engine, fixture.project);
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip.has_value());
    const json back = trip->reloaded->sequence().toJson();
    INFO("differs at: " << fmt::format("{}", fmt::join(testsupport::differingPaths(expected, back), ", ")));
    CHECK(back == expected);
    // And the second save writes the same sequence the first did: a round trip that is stable only
    // once is a round trip that drifts.
    REQUIRE(trip->saved.contains("sequence"));
    REQUIRE(trip->resaved.contains("sequence"));
    CHECK(trip->saved["sequence"] == trip->resaved["sequence"]);
    CHECK(testsupport::missingTopLevelKeys(trip->saved, trip->resaved).empty());
}

TEST_CASE("an inline composition's camera direction survives a project save",
          "[directing][persistence]") {
    // The control for the case below. An inline composition travels whole in the project, so this
    // arm should hold whatever the by-reference arm does.
    testsupport::ScratchDir dir{"director_persistence_inline"};
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.composition() != nullptr);
    REQUIRE(engine.setCameraDirection(withDirectorCamera(engine.composition()->cameraDirection())));
    const json expected = engine.composition()->cameraDirection().toJson();
    REQUIRE(engine.composition()->cameraDirection().shots.size() == 1);

    auto trip = testsupport::saveAndReload(engine, dir / "inline.json");
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip.has_value());
    REQUIRE(trip->reloaded->composition() != nullptr);
    const json back = trip->reloaded->composition()->cameraDirection().toJson();
    INFO("differs at: " << fmt::format("{}", fmt::join(testsupport::differingPaths(expected, back), ", ")));
    CHECK(back == expected);
}

TEST_CASE("a by-reference scene's camera direction survives a project save",
          "[directing][persistence]") {
    ByReferenceScene fixture;
    REQUIRE(fixture.engine.composition() != nullptr);
    REQUIRE(fixture.engine.setCameraDirection(
        withDirectorCamera(fixture.engine.composition()->cameraDirection())));
    const json expected = fixture.engine.composition()->cameraDirection().toJson();

    auto trip = testsupport::saveAndReload(fixture.engine, fixture.project);
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip.has_value());
    REQUIRE(trip->reloaded->composition() != nullptr);
    const scene::CameraDirection& back = trip->reloaded->composition()->cameraDirection();
    INFO("session: " << fixture.engine.composition()->cameraDirection().cameras.size()
                     << " camera(s), " << fixture.engine.composition()->cameraDirection().shots.size()
                     << " shot(s); reloaded: " << back.cameras.size() << " camera(s), "
                     << back.shots.size() << " shot(s)");
    INFO("differs at: " << fmt::format("{}", fmt::join(testsupport::differingPaths(expected, back.toJson()), ", ")));
    CHECK(back.toJson() == expected);
}

TEST_CASE("an untouched by-reference project writes no camera direction and leaves its scene alone",
          "[directing][persistence]") {
    // The control that makes the arm above mean anything: a save that wrote the collection into
    // every project would pass it too, and would make every project read as edited (ADR-440).
    ByReferenceScene fixture;
    std::ifstream sceneIn(fixture.scene, std::ios::binary);
    const std::string sceneBefore((std::istreambuf_iterator<char>(sceneIn)), std::istreambuf_iterator<char>());
    auto trip = testsupport::saveAndReload(fixture.engine, fixture.project);
    REQUIRE(trip.has_value());
    CHECK_FALSE(trip->saved.contains("cameraDirection"));
    CHECK_FALSE(trip->resaved.contains("cameraDirection"));

    // And an edit writes the key without touching the scene file (ADR-271's rule).
    REQUIRE(fixture.engine.setCameraDirection(
        withDirectorCamera(fixture.engine.composition()->cameraDirection())));
    REQUIRE(fixture.engine.saveProject(fixture.project).has_value());
    CHECK(testsupport::readJson(fixture.project).contains("cameraDirection"));
    std::ifstream sceneAfterIn(fixture.scene, std::ios::binary);
    const std::string sceneAfter((std::istreambuf_iterator<char>(sceneAfterIn)), std::istreambuf_iterator<char>());
    CHECK(sceneAfter == sceneBefore);
}

TEST_CASE("deleting a camera the scene file declares stays deleted after a save",
          "[directing][persistence]") {
    // The additions-only failure pointing the other way: a save that wrote only what was added
    // would bring a deleted camera -- and every cut that named it -- back on reload.
    ByReferenceScene fixture;
    REQUIRE(fixture.engine.setCameraDirection(
        withDirectorCamera(fixture.engine.composition()->cameraDirection())));
    REQUIRE(fixture.engine.saveComposition(fixture.scene).has_value()); // the scene now declares it
    REQUIRE(fixture.engine.composition()->cameraDirection().cameras.size() == 2);

    scene::CameraDirection without = fixture.engine.composition()->cameraDirection();
    REQUIRE(without.removeCamera(without.cameras.back().id));
    REQUIRE(fixture.engine.setCameraDirection(without));

    auto trip = testsupport::saveAndReload(fixture.engine, fixture.project);
    REQUIRE(trip.has_value());
    const scene::CameraDirection& back = trip->reloaded->composition()->cameraDirection();
    CHECK(back.cameras.size() == 1);
    CHECK(back.shots.empty());
}

TEST_CASE("the Rook/Umbra benchmark project loads cleanly and a save adds no camera direction",
          "[directing][persistence][benchmark]") {
    // Glowmere Valley 2 multicam is the Director's benchmark scene. Its audio resolves outside the
    // repository (`assets.audio.path` is relative to the user's Desktop), so this also says out
    // loud when a machine cannot run the benchmark rather than letting it half-load.
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json";
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    INFO("warnings: " << fmt::format("{}", fmt::join(engine.projectWarnings(), " | ")));
    CHECK(engine.projectWarnings().empty());
    testsupport::stepFrames(engine, 2);
    const json doc = engine.projectDocument(project);
    // Its cameras live in its scene file, untouched, so nothing may be recorded over them -- in
    // particular not Song Mode's regenerated shots.
    CHECK_FALSE(doc.contains("cameraDirection"));
}
