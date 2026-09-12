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

#include "core/error.hpp"
#include "scene/composition.hpp"

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

// Does this hero declaration stand for that node? The link is by *name*, and deliberately loose: a
// hero can be an assembly of several nodes (Glowmere's "elder" is three), so it names either the
// node it stands on or the assembly the node belongs to. All three fields are checked because a
// hand-authored scene uses whichever one read best at the time.
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
        return params.empty() && parents.empty() && heroes.empty() && added.empty() && removed.empty();
    }
    // How many things the user would say this touched, for the label and for tests.
    [[nodiscard]] std::size_t touched() const;
};

// What applying a command actually managed to do. Problems are reported rather than thrown: an undo
// that could only restore four of five nodes must say so, because the alternative is an editor that
// claims to have undone something it did not.
struct EditApply {
    std::size_t nodesAdded = 0;
    std::size_t nodesRemoved = 0;
    std::size_t paramsWritten = 0;
    std::size_t parentsSet = 0;
    std::size_t heroesSet = 0;
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
