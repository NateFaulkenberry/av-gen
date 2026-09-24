// One undo for every domain the Director writes (ADR-752, director-system-progress.md Slice 0.2).
//
// The Director applies a compiled plan as one operation that touches several domains at once: the
// sequence, the camera collection and camera track, the author timeline and routes, parameter bases.
// The AI control plane does the same over many tool calls. Both must land as ONE command on the
// editor's own history -- the history a person's Cmd+Z uses -- so there is no second undo. These
// cases check each domain's apply -> undo -> redo leaves its document identical to the side it
// claims to restore, and that the AI path now records every domain rather than parameters only.

#include "ai/control_plane.hpp"
#include "ai/scripted_provider.hpp"
#include "app/ai_edit_sink.hpp"
#include "app/edit_capture.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "params/serialization.hpp"
#include "scene/camera_rig.hpp"
#include "seq/events.hpp"
#include "seq/sequence.hpp"
#include "ui/ai_panel_logic.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

using namespace avgen;
using nlohmann::json;

namespace {

json routesJson(app::Engine& engine) {
    json out = json::array();
    for (const params::ModRoute& route : engine.modulator().routes()) {
        out.push_back(params::routeToJson(route));
    }
    return out;
}

// Every domain the Director writes, as documents, so "identical" is one comparison per domain.
struct Documents {
    json sequence;
    json cameras;
    json timeline;
    json routes;
    json gain;

    static Documents of(app::Engine& engine) {
        Documents d;
        d.sequence = engine.sequence().toJson();
        d.cameras = ui::capturedCameraDirection(engine).toJson();
        d.timeline = engine.timeline().toJson();
        d.routes = routesJson(engine);
        const params::IParameter* gain = engine.params().find("test/gain");
        d.gain = gain != nullptr ? json(gain->baseComponent(0)) : json();
        return d;
    }
};

void checkSame(const Documents& a, const Documents& b) {
    CHECK(a.sequence == b.sequence);
    CHECK(a.cameras == b.cameras);
    CHECK(a.timeline == b.timeline);
    CHECK(a.routes == b.routes);
    CHECK(a.gain == b.gain);
}

scene::CameraRig chaseRig() {
    scene::CameraRig rig;
    rig.name = "Rook Chase";
    rig.position = glm::vec3(3.0f, 1.0f, -6.0f);
    rig.target = glm::vec3(0.0f, 1.0f, 0.0f);
    rig.focalLength = 24.0f;
    return rig;
}

params::Track twoKeys(std::string target, float a, float b) {
    params::Track track;
    track.target = std::move(target);
    track.keys.push_back(params::Key{1.0, params::KeyValue{a, a, a, 0.0f}});
    track.keys.push_back(params::Key{3.0, params::KeyValue{b, b, b, 0.0f}});
    return track;
}

} // namespace

TEST_CASE("deleting a keyed, moved camera is undone whole: the rig, where it was moved, its keys",
          "[directing][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    ui::EditHistory history;

    // Add the camera as one command.
    scene::CameraDirection withRig = engine.composition()->cameraDirection();
    const scene::CameraId id = withRig.addCamera(chaseRig());
    std::string problem;
    ui::EditCommand add = ui::editCameraDirection(engine, "Add camera", withRig, &problem);
    INFO(problem);
    REQUIRE(add.cameras != nullptr);
    CHECK(add.automation == nullptr); // adding a camera takes no keys with it
    history.push(std::move(add));
    const std::string slug = engine.composition()->cameraDirection().find(id)->slug;
    const std::string position = "cameras/" + slug + "/position";
    REQUIRE(engine.params().find(position) != nullptr);

    // Move it the way the gizmo or "Place here" does -- a parameter base, not the struct -- and key
    // its position. This is the state a delete must give back.
    REQUIRE(ui::setBaseComponents(engine, position, {7.0f, 2.0f, -9.0f}));
    engine.timeline().addTrack(twoKeys(position, 1.0f, 5.0f));
    engine.rebind();
    const Documents before = Documents::of(engine);
    REQUIRE(engine.timeline().findTrack(position) != nullptr);

    // Delete it. `setCameraDirection` erases the camera's tracks and parameters, so the command must
    // carry the automation as well as the collection.
    scene::CameraDirection without = engine.composition()->cameraDirection();
    REQUIRE(without.removeCamera(id));
    ui::EditCommand remove = ui::editCameraDirection(engine, "Delete camera", without, &problem);
    REQUIRE(remove.cameras != nullptr);
    REQUIRE(remove.automation != nullptr);
    history.push(std::move(remove));
    const Documents after = Documents::of(engine);
    CHECK(engine.params().find(position) == nullptr);
    CHECK(engine.timeline().findTrack(position) == nullptr);

    const ui::EditApply undone = history.undo(engine);
    INFO((undone.problems.empty() ? std::string() : undone.problems.front()));
    CHECK(undone.ok());
    checkSame(Documents::of(engine), before);
    // Where it was MOVED to, not where it was created: the rig came back from a struct that
    // carried the base, not from the one `addCamera` stamped.
    const std::vector<float> back = ui::baseComponents(engine, position);
    REQUIRE(back.size() == 3);
    CHECK(back[0] == 7.0f);
    CHECK(back[2] == -9.0f);
    // And its keys are bound again, not merely present.
    REQUIRE(engine.timeline().findTrack(position) != nullptr);
    CHECK(engine.timeline().findTrack(position)->param != nullptr);

    CHECK(history.redo(engine).ok());
    checkSame(Documents::of(engine), after);
    CHECK(history.undo(engine).ok());
    checkSame(Documents::of(engine), before);
}

TEST_CASE("one captured operation across sequence, cameras, keys, routes and a base is one command",
          "[directing][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    params::IParameter& gain = engine.params().add(
        params::ParamDesc<float>{.path = "test/gain", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    engine.params().add(
        params::ParamDesc<float>{.path = "test/flash", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    ui::EditHistory history;
    const Documents before = Documents::of(engine);

    // What the Director's apply does: many writes, measured once.
    app::EditCapture capture;
    capture.begin(engine);
    {
        seq::Sequence piece;
        seq::Shot shot;
        shot.name = "rook-umbra";
        shot.startSeconds = 90.0;
        shot.durationSeconds = 5.0;
        piece.shots.push_back(shot);
        piece.markers.push_back(seq::Marker{92.55, "rook.jump_peak", seq::MarkerKind::Cue});
        // An actor with keys and cues, and an event on the marker: the rest of what a compiled
        // performance writes into the sequence, undone by the same record.
        seq::Actor rook;
        rook.id = "rook";
        rook.keys.push_back(seq::ActorKey{90.0, glm::vec3(0.0f), std::nullopt, std::nullopt,
                                          params::KeyInterp::Linear});
        rook.keys.push_back(seq::ActorKey{95.0, glm::vec3(10.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                          params::KeyInterp::Linear});
        rook.clips.push_back(seq::ClipCue{90.0, "Running", 1.0f, -1.0f});
        piece.actors.push_back(rook);
        seq::SequenceEvent peak;
        peak.id = "peak";
        peak.when.kind = seq::TriggerKind::Cue;
        peak.when.name = "rook.jump_peak";
        peak.what.kind = seq::EventActionKind::SetParameter;
        // A different parameter from the author's keys below, deliberately: `seq::install` owns
        // every track on a target the sequence bakes to, and erases an author track sharing one on
        // the next install. That is the engine's rule, not the history's, and the Director's
        // compiler must respect it (baked cues own their targets).
        peak.what.target = "test/flash";
        peak.what.amount = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
        piece.events.push_back(peak);
        REQUIRE(engine.setSequence(piece).has_value());

        scene::CameraDirection direction = engine.composition()->cameraDirection();
        scene::CameraShot cut;
        cut.camera = direction.addCamera(chaseRig());
        cut.startSeconds = 90.0;
        cut.endSeconds = 95.0;
        cut.locked = true;
        direction.shots.push_back(cut);
        REQUIRE(engine.setCameraDirection(direction).has_value());

        engine.timeline().addTrack(twoKeys("test/gain", 1.0f, 2.0f));
        params::ModRoute route;
        route.source = "audio.bass";
        route.target = "test/gain";
        route.amount = 0.5f;
        engine.modulator().addRoute(route);
        gain.setBaseComponent(0, 4.0f);
        engine.rebind();
    }
    ui::EditCommand command = capture.finish(engine, "Director: rook-umbra");
    CHECK(capture.unrecoverable().empty());
    REQUIRE(command.timeline != nullptr);
    REQUIRE(command.cameras != nullptr);
    REQUIRE(command.automation != nullptr);
    CHECK(command.automation->routesTouched);
    CHECK(command.params.size() == 1);
    history.push(std::move(command));
    REQUIRE(history.undoSize() == 1);
    const Documents after = Documents::of(engine);

    const ui::EditApply undone = history.undo(engine);
    INFO((undone.problems.empty() ? std::string() : undone.problems.front()));
    CHECK(undone.ok());
    checkSame(Documents::of(engine), before);
    CHECK(engine.sequence().shots.empty());
    CHECK(engine.sequence().actors.empty());
    CHECK(engine.sequence().events.empty());
    CHECK(engine.composition()->cameraDirection().cameras.size() == 1);

    CHECK(history.redo(engine).ok());
    checkSame(Documents::of(engine), after);
}

TEST_CASE("a camera that only moved is a parameter edit, not a new camera collection",
          "[directing][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    scene::CameraDirection direction = engine.composition()->cameraDirection();
    const scene::CameraId id = direction.addCamera(chaseRig());
    REQUIRE(engine.setCameraDirection(direction).has_value());
    const std::string position = "cameras/" + engine.composition()->cameraDirection().find(id)->slug + "/position";

    app::EditCapture capture;
    capture.begin(engine);
    REQUIRE(ui::setBaseComponents(engine, position, {1.0f, 2.0f, 3.0f}));
    const ui::EditCommand command = capture.finish(engine, "Move camera");
    CHECK(command.cameras == nullptr);
    CHECK(command.automation == nullptr);
    REQUIRE(command.params.size() == 1);
    CHECK(command.params.front().path == position);
}

namespace {

ai::ToolCall call(std::string id, std::string name, json args) {
    return ai::ToolCall{std::move(id), std::move(name), std::move(args)};
}

} // namespace

TEST_CASE("an AI task that adds a shot, a marker and a keyframe is one undo on the editor's history",
          "[directing][undo][ai]") {
    // Before ADR-752 the history sink recorded parameter bases only, so this task left the history
    // EMPTY and its shot, marker and keys could not be undone at all -- and "Undo this task", the
    // snapshot restore that could have, was never shown in the application.
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.params().add(
        params::ParamDesc<float>{.path = "test/gain", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 10.0f});
    app::JobSystem jobs{2};
    ai::ControlPlane plane{engine, &jobs};
    plane.setCredentialStore(std::make_unique<ai::MemoryCredentialStore>());
    app::EditSystem edits;
    app::EditHistoryTransactionSink sink(engine, edits, plane.transactionSink());
    plane.setTransactionSink(&sink);
    const Documents before = Documents::of(engine);

    plane.setProvider(std::make_shared<ai::ScriptedProvider>(std::vector<ai::ScriptedTurn>{
        ai::ScriptedTurn{"Adding the shot, its peak marker and a gain ramp.",
                         {call("c1", "sequence.add_shot", json{{"name", "rook-umbra"}, {"start", 90.0}, {"duration", 5.0}}),
                          call("c2", "sequence.add_marker",
                               json{{"name", "rook.jump_peak"}, {"time", 92.55}, {"kind", "cue"}}),
                          call("c3", "sequencer.add_keyframe", json{{"target", "test/gain"}, {"time", 92.0}, {"value", 3.0}})},
                         ai::StopReason::EndTurn,
                         {}},
        ai::ScriptedTurn{"Done.", {}, ai::StopReason::EndTurn, {}},
    }));
    auto task = plane.submit("at 1:30 add the rook shot");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!task->finished() && std::chrono::steady_clock::now() < deadline) {
        plane.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    plane.pump();
    REQUIRE(task->state() == ai::TaskState::Completed);
    const ai::TaskOutcome outcome = task->outcome();
    REQUIRE(outcome.toolCalls == 3);
    REQUIRE(engine.sequence().shots.size() == 1);
    REQUIRE(engine.sequence().markers.size() == 1);
    REQUIRE(engine.timeline().findTrack("test/gain") != nullptr);
    const Documents after = Documents::of(engine);

    // One command, and the task knows which.
    REQUIRE(edits.history().undoSize() == 1);
    CHECK(edits.history().undoLabel() == "at 1:30 add the rook shot");
    REQUIRE(outcome.editState != 0);
    CHECK(outcome.editState == edits.history().stateId());
    CHECK(ui::taskUndoState(outcome.editState, edits.history().stateId(), edits.history().canUndo()) ==
          ui::TaskUndo::Available);

    // "Undo this task" is exactly Cmd+Z now.
    REQUIRE(edits.execute(app::EditAction::Undo, engine));
    checkSame(Documents::of(engine), before);
    CHECK(ui::taskUndoState(outcome.editState, edits.history().stateId(), edits.history().canUndo()) ==
          ui::TaskUndo::Superseded);
    REQUIRE(edits.execute(app::EditAction::Redo, engine));
    checkSame(Documents::of(engine), after);
    CHECK(ui::taskUndoState(outcome.editState, edits.history().stateId(), edits.history().canUndo()) ==
          ui::TaskUndo::Available);
}

TEST_CASE("the AI panel offers a task's undo only while that task is the newest edit",
          "[directing][undo][ai]") {
    CHECK(ui::taskUndoState(0, 0, false) == ui::TaskUndo::None);
    CHECK(ui::taskUndoState(0, 7, true) == ui::TaskUndo::None);
    CHECK(ui::taskUndoState(7, 7, true) == ui::TaskUndo::Available);
    CHECK(ui::taskUndoState(7, 8, true) == ui::TaskUndo::Superseded);  // an edit since
    CHECK(ui::taskUndoState(7, 6, true) == ui::TaskUndo::Superseded);  // already undone
    CHECK(ui::taskUndoState(7, 7, false) == ui::TaskUndo::Superseded); // trimmed off the stack
}
