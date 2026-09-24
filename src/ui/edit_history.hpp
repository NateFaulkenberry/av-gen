#pragma once

// Undo and redo for the world editor (ADR-092, world-authoring-spec §28).
//
// The model is a **record of the edit**, not a snapshot of the scene. A snapshot would be simple
// and wrong here: a Glowmere scene carries 256 terrain chunks, a quarter of a million scattered
// instances and several thousand parameters, so copying it to make one flower undoable would cost
// more than placing the flower. What actually changed is always small, and it is always one of
// four things:
//
//   * a parameter's **base value** moved -- which is every transform, because every transform is a
//     parameter (`nodes/<name>/position`) and nothing in this editor moves anything any other way;
//   * a **node entered** the scene -- a placement, a duplicate, a new group;
//   * a **node left** it -- a deletion, or the far side of an undone placement;
//   * a node's **parent** changed -- grouping, ungrouping, reparenting.
//
// A command is a list of those, so undo is the same list with `before` and `after` exchanged and
// the two node lists swapped. Nothing about the operation has to be re-derived to reverse it, which
// is the property that makes a command model worth the trouble: "move these eleven rocks four
// metres east" is undone by writing eleven vectors, not by working out what the opposite of a drag
// is.
//
// A departing node is **kept, not described.** `EditCommand` owns the `CompositionNode` itself
// while it is out of the scene. Serialising it instead would mean a delete-then-undo silently
// losing whatever the scene-file format does not write -- a loaded glTF asset, a nested child
// composition, a terrain's built chunks -- and losing it in the one situation where the user is
// most certain nothing happened.
//
// Everything here is ImGui-free and testable without a window or a GPU; see
// tests/unit/test_edit_history.cpp.

#include "audio/arrangement.hpp"
#include "core/error.hpp"
#include "params/modulation.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"
#include "world/effects/effect_instance.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

// One parameter's base value, before and after. Components rather than a typed value so one record
// covers a vec3 position, a float gain and a bool visibility without three kinds of record; that is
// exactly what IParameter::setBaseComponent is for.
struct ParamChange {
    std::string path;
    std::vector<float> before;
    std::vector<float> after;
};

// One node's parent, before and after. Empty means "a root".
struct ParentChange {
    std::string node;
    std::string before;
    std::string after;
};

// One object's hero declaration, before and after (ADR-072/074). Usually one or none either side;
// a vector because the rule for "this node is a hero" is a name match and a hand-edited scene can
// contain two heroes that both name it.
//
// The whole `HeroPoint` is kept rather than a flag, because a hero carries authored judgement --
// how important it is, how far a camera should stand off, which reaction profile it answers the
// music with -- and a toggle that threw that away would be a toggle nobody could take back.
struct HeroChange {
    std::string node;
    std::vector<world::HeroPoint> before;
    std::vector<world::HeroPoint> after;
};

// One object's hero declaration is one hero, so `before`/`after` hold at most one -- vectors because
// a hand-edited scene can still name the same object twice and undo has to put back what was there.

// Does this hero declaration stand for that node?
//
// One object, one hero, matched by name (ADR-107). The link used to be looser -- a hero could name
// an "assembly" of several nodes -- which produced heroes that no row in the editor was and that no
// reaction could reach, because everything downstream addresses a hero as `nodes/<its name>/...`.
[[nodiscard]] bool heroNamesNode(const world::HeroPoint& hero, const std::string& node);

// A node the command moves in or out of the scene. `held` owns it while it is *out*: null while the
// node is live in the composition, non-null while the history is the only thing keeping it alive.
struct NodeRecord {
    std::string name;
    std::unique_ptr<scene::CompositionNode> held;

    NodeRecord() = default;
    explicit NodeRecord(std::string n) : name(std::move(n)) {}
    NodeRecord(std::string n, std::unique_ptr<scene::CompositionNode> h)
        : name(std::move(n)), held(std::move(h)) {}
};

// A sequencer edit, recorded **whole**.
//
// The note at the top of this file argues against snapshots, and that argument is about the
// *composition*: 256 terrain chunks and a quarter of a million scattered instances are not
// something to copy so that one flower can be undone. **A sequence is not that.** It is a name, a
// duration, a handful of shots, a cast, some cues and a section timeline -- kilobytes, all of it
// value types -- and the audio clips beside it are file paths and offsets, not samples.
//
// So the calculation reverses, and with it the design: rather than a record type per gesture
// (delete a shot, trim one, split one, drop a clip, add an actor -- each with its own inverse to
// get right), one before-and-after covers every edit the sequencer can make, including the ones
// nobody has written yet. The reversal of "the sequence was this, now it is that" is not something
// that has to be derived.
//
// `clipsTouched` exists because installing audio clips **re-opens the sources and re-mixes the
// piece** (ADR-103), which is a pass over every sample. A shot drag must not pay for that, so an
// edit says whether it touched the audio at all and the apply believes it.
struct TimelineChange {
    seq::Sequence before;
    seq::Sequence after;
    std::vector<audio::AudioClip> clipsBefore;
    std::vector<audio::AudioClip> clipsAfter;
    bool clipsTouched = false;
};

// One object's authored-light list, before and after.
//
// Recorded **whole**, on `TimelineChange`'s argument rather than `NodeRecord`'s. The note at the
// top of this file argues against snapshots and that argument is about the *composition*: 256
// terrain chunks and a quarter of a million scattered instances are not something to copy so one
// flower can be undone. **A light list is not that.** It is a handful of structs of plain floats
// and two short strings -- so rather than a record type per gesture (add a light, delete one,
// rename one, retype one, re-node one, reorder them), each needing its own inverse to get right,
// one before-and-after reverses every edit the panel can make, including the ones nobody has
// written yet.
//
// It also happens to be the exact shape `Composition::setAuthoredLights` already takes, which is
// what makes applying it in either direction a single call rather than a merge.
//
// Note what is NOT here: a light's intensity, colour and position are `ParamChange`s like every
// other transform in this editor, because they are registered parameters (ADR-271). This record is
// for the *set* -- which lights exist and what kind they are.
struct LightChange {
    std::vector<scene::Composition::AuthoredLight> before;
    std::vector<scene::Composition::AuthoredLight> after;
};

// The scene's effect list, before and after (ADR-702).
//
// Recorded **whole**, on `LightChange`'s argument: the list is every owner's effects -- a handful to
// a few dozen instances of plain values -- and one before-and-after reverses every structural edit
// the Effects section can make (add, remove, duplicate, reorder, preset, reset, a source or anchor
// change), including the ones nobody has written yet. It is also the exact shape
// `Engine::setEffects` takes, so applying it either way is one call.
//
// Both sides are `Engine::capturedEffects()` -- the authored list with every slider's current BASE
// captured into it -- because `setEffects` registers the parameters FROM the list, and a snapshot of
// the authored list alone would put every slider moved since the last structural edit back where it
// started. A slider move on its own is a `ParamChange`, like every other parameter in this editor.
//
// `routesTouched` is for "+ Add Effect", which also attaches the type's default audio routes: undoing
// the add without taking them back would leave routes aimed at parameters that no longer exist, and
// every rebind would then report them. Only that gesture sets it; the route list is otherwise the
// Modulation panel's to edit.
struct EffectChange {
    std::vector<world::EffectInstance> before;
    std::vector<world::EffectInstance> after;
    bool routesTouched = false;
    std::vector<params::ModRoute> routesBefore;
    std::vector<params::ModRoute> routesAfter;
};

// One reversible change. Move-only, because it owns nodes.
struct EditCommand {
    // What the user did, in their words, for the status bar and the history list: "Place 12 x fern",
    // "Move 3 objects", "Group 5 objects". Shown after "Undo", so it reads as a sentence.
    std::string label;
    std::vector<ParamChange> params;
    std::vector<ParentChange> parents;
    std::vector<HeroChange> heroes;
    std::vector<NodeRecord> added;    // put into the scene by this command
    std::vector<NodeRecord> removed;  // taken out of the scene by this command
    // The sequencer's side of the same history, or null for the world edits that are most of it.
    // A pointer so that a command which moves eleven rocks does not carry a sequence-shaped hole.
    std::unique_ptr<TimelineChange> timeline;
    // The authored lights either side, or null for the edits that are most of them. A pointer for
    // the same reason `timeline` is one: a command that moves eleven rocks should not carry a
    // light-list-shaped hole.
    std::unique_ptr<LightChange> lights;
    // The effect list either side (ADR-702), or null for the edits that are most of them.
    std::unique_ptr<EffectChange> effects;
    // The selection either side, so undoing a delete gives you back what you had selected rather
    // than leaving you staring at a scene with nothing chosen and no idea what came back.
    std::vector<std::string> selectionBefore;
    std::vector<std::string> selectionAfter;

    EditCommand() = default;
    explicit EditCommand(std::string l) : label(std::move(l)) {}
    EditCommand(EditCommand&&) = default;
    EditCommand& operator=(EditCommand&&) = default;
    EditCommand(const EditCommand&) = delete;
    EditCommand& operator=(const EditCommand&) = delete;

    [[nodiscard]] bool empty() const {
        return params.empty() && parents.empty() && heroes.empty() && added.empty() &&
               removed.empty() && timeline == nullptr && lights == nullptr && effects == nullptr;
    }
    // How many things the user would say this touched, for the label and for tests.
    [[nodiscard]] std::size_t touched() const;
};

// What applying a command actually managed to do. Problems are reported rather than thrown: an undo
// that could only restore four of five nodes must say so, because the alternative is an editor that
// claims to have undone something it did not.
struct EditApply {
    std::size_t nodesAdded = 0;
    // 1 when this command installed a sequence, 0 otherwise. Counted rather than flagged so the
    // field reads like the four beside it.
    std::size_t timelinesInstalled = 0;
    std::size_t nodesRemoved = 0;
    std::size_t paramsWritten = 0;
    std::size_t parentsSet = 0;
    std::size_t heroesSet = 0;
    // 1 when this command installed an authored-light list, 0 otherwise. Counted rather than
    // flagged so the field reads like the others beside it.
    std::size_t lightListsInstalled = 0;
    // 1 when this command installed an effect list, 0 otherwise.
    std::size_t effectListsInstalled = 0;
    std::vector<std::string> problems;
    [[nodiscard]] bool ok() const { return problems.empty(); }
};

// Applies `command` to the engine. `forward` replays it; false reverses it. Mutates the command:
// the node records change which side owns the node. Rebinds once at the end, so a node that comes
// back gets its routes and timeline tracks back with it -- `Timeline::bind` skips tracks naming an
// unknown parameter without a word, so an undo that did not rebind would restore the node and
// silently leave its automation disconnected.
EditApply applyEdit(app::Engine& engine, EditCommand& command, bool forward);

// The undo and redo stacks.
//
// Linear, with the usual rule: a new edit after an undo discards the redo stack. Bounded, because
// the history owns deleted nodes and an unbounded one is a memory leak with a nice name; the oldest
// command is dropped when the limit is passed, which is also when its held nodes are finally freed.
class EditHistory {
public:
    // Roughly a working session's worth. A command is a few hundred bytes plus whatever nodes it is
    // holding, and only deletions hold nodes.
    static constexpr std::size_t kDefaultCapacity = 256;

    explicit EditHistory(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {}

    // Records a command that has *already happened*. The caller performs the edit and describes it;
    // pushing does not replay it. Empty commands are dropped rather than cluttering the stack with
    // "Move 0 objects".
    void push(EditCommand command);

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    // The label of what the next undo/redo would do, or an empty string.
    [[nodiscard]] const std::string& undoLabel() const;
    [[nodiscard]] const std::string& redoLabel() const;

    // Reverses the newest command and moves it to the redo stack. `selection` is filled with the
    // selection the command recorded for that side, so the caller can restore it; left untouched
    // when there is nothing to undo.
    EditApply undo(app::Engine& engine, std::vector<std::string>* selection = nullptr);
    EditApply redo(app::Engine& engine, std::vector<std::string>* selection = nullptr);

    void clear();

    // ---- identity, for save state and for the UI ------------------------------------------------
    //
    // `stateId` names *the document state*, not the depth of the stack. Depth cannot answer "is this
    // what was saved": undo twice and make a different edit and the stack is the same height as it
    // was, holding an entirely different document. Every command gets a serial when it is pushed and
    // the state is named by the newest one, so a re-edit after an undo is a state nobody has saved
    // even though the count agrees.
    //
    // The empty-stack state has its own id, and it *moves* when the oldest command is trimmed away:
    // once a command has fallen off the bottom, "no commands" means the document as it stood after
    // that one, which is a different document from the one the session opened with.
    [[nodiscard]] std::uint64_t stateId() const;
    // Bumps on every change -- push, undo, redo, clear. A panel can compare it against what it drew
    // last rather than re-reading the whole stack every frame.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

    [[nodiscard]] std::size_t undoSize() const { return undo_.size(); }
    [[nodiscard]] std::size_t redoSize() const { return redo_.size(); }
    // The labels of the undo stack, newest last. For the history list in the editor.
    [[nodiscard]] std::vector<std::string> labels() const;
    // The redo stack's labels, next-to-be-redone first. The history panel shows these as the future
    // they are, so "where am I" is a thing to look at rather than to work out from two counts.
    [[nodiscard]] std::vector<std::string> redoLabels() const;

    // ---- coalescing a continuous edit ----------------------------------------------------------
    //
    // A gizmo drag writes a parameter every frame for as long as the mouse is down. Pushing one
    // command per frame would make undo useless -- sixty presses to take back one drag -- so a drag
    // opens a command, writes through it, and closes it once on release. The before values are
    // captured at open; the after values are read at commit, which is the only moment at which they
    // are actually known.
    //
    // `beginDrag` captures the current base values of `paths`. `commitDrag` reads them again and
    // pushes a single command if anything moved. `cancelDrag` throws it away *and* puts the before
    // values back, which is what Escape during a drag means.
    void beginDrag(app::Engine& engine, std::string label, const std::vector<std::string>& paths,
                   std::vector<std::string> selection = {});
    [[nodiscard]] bool dragging() const { return dragging_; }
    void commitDrag(app::Engine& engine);
    void cancelDrag(app::Engine& engine);

private:
    void trim();

    std::vector<EditCommand> undo_;
    std::vector<EditCommand> redo_;
    // Parallel to the two stacks: which state each command produced. Kept beside rather than inside
    // EditCommand because it is the *history's* bookkeeping, not part of what the command does.
    std::vector<std::uint64_t> undoIds_;
    std::vector<std::uint64_t> redoIds_;
    std::uint64_t nextId_ = 1;
    std::uint64_t baseId_ = 0;    // the state with an empty undo stack
    std::uint64_t revision_ = 0;
    std::size_t capacity_;
    bool dragging_ = false;
    EditCommand drag_;
};

// Reads a parameter's base value into a component vector, or an empty vector when it does not
// exist. The one place the editor turns a path into numbers, so a path that is not registered is a
// visible empty rather than an exception or a zero that looks like a value.
[[nodiscard]] std::vector<float> baseComponents(app::Engine& engine, const std::string& path);
// Writes one back. False when the parameter does not exist or the arity disagrees -- which is the
// signal that a command was recorded against a scene that has since changed underneath it.
bool setBaseComponents(app::Engine& engine, const std::string& path, const std::vector<float>& values);

} // namespace avgen::ui
