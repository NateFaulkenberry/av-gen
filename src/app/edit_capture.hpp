#pragma once

// What an operation changed, measured rather than recorded, as one undoable command (ADR-752).
//
// Three kinds of caller make many changes the editor did not watch happen: an AI task (dozens of
// tool calls over many frames), the Director applying a compiled plan (a sequence, a camera, keys at
// once), and a script. None of them should have to describe their own edit gesture by gesture --
// that is the design `EditHistoryTransactionSink` already chose for parameters ("capture at the
// start, compare at the end, push once"), and this is that design extended to every domain the
// Director writes, so there is exactly one definition of "what did this operation change":
//
//   parameters       base values of every parameter that existed at `begin` and still does
//   sequence         the whole `seq::Sequence` (shots, actors, cues, markers, events, tracks)
//   cameras          the camera collection and camera track, rigs carrying their current bases
//   automation       the author timeline (tracks, keys, cues) and the modulation routes
//   nodes            nodes the operation ADDED (undo detaches them) and parent changes
//
// The records are the history's own whole-domain ones (`TimelineChange`, `CameraDirectionChange`,
// `AutomationChange`), so an operation measured here is undone by exactly the code a person's edit
// is undone by. There is no second undo.
//
// **What it cannot do:** bring back a node the operation *destroyed*. A diff taken afterwards has
// only the node's name, and `EditCommand` holds departing nodes whole precisely because a
// description would lose what the scene format does not write. Such names are reported by
// `unrecoverable()`, and a caller that removes nodes must hold them itself (ADR-752).
//
// Cost: one pass over the parameter set and one serialisation of the sequence and the timeline at
// each end. Measured on the Glowmere multicam project in test_director_undo.cpp; it is an
// operation-boundary cost, never a per-frame one.

#include "params/modulation.hpp"
#include "params/timeline.hpp"
#include "scene/camera_rig.hpp"
#include "seq/sequence.hpp"
#include "ui/edit_history.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace avgen::app {

class Engine;

class EditCapture {
public:
    // Snapshots every domain. Calling it again restarts the capture.
    void begin(Engine& engine);
    // The command that takes the engine from the `begin` state to now, already applied (push it;
    // do not replay it). Empty when nothing changed. Closes the capture.
    [[nodiscard]] ui::EditCommand finish(Engine& engine, std::string label);
    // Forgets the capture without building anything.
    void cancel();

    [[nodiscard]] bool open() const { return open_; }
    // Nodes present at `begin` and gone at `finish`, which the command cannot restore.
    [[nodiscard]] const std::vector<std::string>& unrecoverable() const { return unrecoverable_; }

private:
    bool open_ = false;
    std::unordered_map<std::string, std::vector<float>> bases_;
    seq::Sequence sequence_;
    nlohmann::json sequenceJson_;
    scene::CameraDirection cameras_;    // with bases captured into the rigs: what undo installs
    scene::CameraDirection rawCameras_; // as authored: what decides whether the collection changed
    std::vector<std::string> sequenceTargets_;
    params::Timeline timeline_;
    nlohmann::json timelineJson_;
    std::vector<params::ModRoute> routes_;
    nlohmann::json routesJson_;
    std::map<std::string, std::string> parents_; // node -> parent, "" for a root
    std::vector<std::string> unrecoverable_;
};

} // namespace avgen::app
