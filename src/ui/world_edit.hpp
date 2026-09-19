#pragma once

// The world editor's model of the scene it is editing (ADR-092, world-authoring-spec §23-§28):
// what is selected, what a group is, and the operations an artist performs on both.
//
// Every operation here does two things and returns the second: it performs the edit, and it
// describes it as an `EditCommand` the caller pushes onto the history. They are not separable --
// an operation that could be performed without being described is an operation somebody will
// eventually perform without describing it, and that is precisely how an editor ends up with a
// half-covered undo stack.
//
// **Groups are node parenting.** A group is a `NodeKind::Group` node -- an empty transform -- with
// the grouped objects parented to it. Nothing new had to be invented: `Composition::setParent`
// already builds the world transform as parent x local, the scene file already writes `"parent"`,
// the group's own position is already a keyframeable, modulatable, serialised parameter, and the
// members are still ordinary nodes you can select and move one at a time. The alternative -- a
// separate table of names off to one side -- would have been a second hierarchy that the renderer,
// the serialiser and the parameter system all knew nothing about.
//
// ImGui-free, so all of it is testable without a window; see tests/unit/test_world_edit.cpp.

#include "scene/composition.hpp"
#include "ui/edit_history.hpp"

#include <glm/glm.hpp>

#include <span>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

// What the artist has chosen, in the order they chose it.
//
// Ordered rather than a set because the *last* thing selected is the active one: it is whose
// numbers the inspector shows, and in a transform of several objects it is the one the others move
// relative to. That is the convention every 3D tool uses and the one people arrive with.
// What one selected thing is. A camera and a light are not `CompositionNode`s and deliberately
// never became one: ADR-278 rejected `NodeKind::Light` against 148 switch sites in 16 files, each
// of which would have had to decide what a node that draws no geometry means to the brush, the
// context menu and the asset browser. That decision stands, so "the selection" cannot go on being
// a list of node names.
//
// This is the viewport-authoring spec's `SelectionTarget`, and the reason it is one type rather
// than a selection per panel is that the spec forbids the alternative in as many words: two
// selection models means two gizmo paths, and the second one is always the one that misses the
// undo coalescing, the multi-select or the Escape handling.
//
// `Node` is first so a default-constructed ref is the kind that already existed.
struct SelectionRef {
    enum class Kind : std::uint8_t { Node, Light, Camera };
    Kind kind = Kind::Node;
    std::string name;

    friend bool operator==(const SelectionRef&, const SelectionRef&) = default;
};
[[nodiscard]] const char* selectionKindName(SelectionRef::Kind kind);

class Selection {
public:
    [[nodiscard]] const std::vector<SelectionRef>& refs() const { return refs_; }
    // The node members, in selection order.
    //
    // **A filter over the one storage, not a second selection.** It returns the `Kind::Node`
    // members, which is exactly what every existing caller means by "the selection": `deleteNodes`,
    // `groupNodes`, `topmostOf` and `withDescendants` are all questions about the composition tree
    // and none of them has an answer for a light. So a mixed selection of a lamp and a rock groups
    // the rock and moves both, which is what every tool does and what needs no special case.
    //
    // Cached rather than computed because this returns a reference and the callers hold it across a
    // frame; it is rebuilt on mutation, and the mutations are user gestures, not a loop.
    [[nodiscard]] const std::vector<std::string>& nodes() const { return nodeNames_; }
    [[nodiscard]] bool empty() const { return refs_.empty(); }
    [[nodiscard]] std::size_t size() const { return refs_.size(); }
    // Unqualified, `contains` asks about a node, because that is what every existing caller means.
    [[nodiscard]] bool contains(const std::string& name) const;
    [[nodiscard]] bool contains(const SelectionRef& ref) const;
    [[nodiscard]] std::size_t countOf(SelectionRef::Kind kind) const;
    // The active object: the last one added. A default `SelectionRef{}` when nothing is selected,
    // whose `name` is empty -- so `primary().empty()` stays the "nothing selected" test the
    // existing callers already write, and keeps meaning it for a node.
    [[nodiscard]] const SelectionRef& primaryRef() const;
    [[nodiscard]] const std::string& primary() const;

    void clear() { refs_.clear(); nodeNames_.clear(); }
    void set(std::string name);
    void set(std::vector<std::string> names);
    void set(SelectionRef ref);
    void set(std::vector<SelectionRef> refs);
    void add(std::string name);          // no-op when already selected
    void add(SelectionRef ref);
    void remove(const std::string& name);
    void remove(const SelectionRef& ref);
    void toggle(std::string name);       // shift-click
    void toggle(SelectionRef ref);
    // True when this changed anything, so a caller can avoid recording a selection edit that did
    // nothing.
    //
    // Nodes are checked against the composition, lights against its authored list. A camera ref is
    // kept whatever happens: a camera's identity is a `CameraId` that the name only labels, so
    // dropping one on a name miss would deselect the camera every time somebody renamed it.
    bool retainOnly(const scene::Composition& composition); // drops entries that no longer exist

private:
    void rebuildNodeNames();

    std::vector<SelectionRef> refs_;
    std::vector<std::string> nodeNames_; // the Kind::Node members, in order
};

// ---- group navigation --------------------------------------------------------------------------

// The outermost Group above `name`, or `name` itself when it is not in one. Clicking an object
// inside a group selects the group, which is what every tool does and what makes a group worth
// making; holding the modifier that bypasses it is the caller's decision, not this function's.
[[nodiscard]] std::string groupRootOf(const scene::Composition& composition, const std::string& name);
// Whether `name` is locked out of the pointer -- itself, or by anything it sits under. A lock on a
// group covers what is inside it, the way a locked layer group does in an image editor: an artist
// who locks "terrain" means the ground and everything the ground is made of, not one node whose
// children stay clickable through it.
//
// Lives here rather than in the picker because it is a question about the scene, and the picker, the
// drag box and Select All all have to answer it the same way. One of them answering differently is
// exactly the bug this feature exists to remove.
[[nodiscard]] bool nodeLocked(const scene::Composition& composition, const std::string& name);

// Every node parented under `name`, transitively, not including `name`.
[[nodiscard]] std::vector<std::string> descendantsOf(const scene::Composition& composition,
                                                     const std::string& name);
// `names` with every descendant added, deduplicated. What actually moves when a selection moves.
[[nodiscard]] std::vector<std::string> withDescendants(const scene::Composition& composition,
                                                       std::span<const std::string> names);
// `names` with any member that is a descendant of another member dropped: moving a group *and* one
// of its children would move the child twice.
[[nodiscard]] std::vector<std::string> topmostOf(const scene::Composition& composition,
                                                  std::span<const std::string> names);

// The box around a set of nodes, and the point a gizmo sits at.
[[nodiscard]] scene::WorldBounds selectionBounds(scene::Composition& composition,
                                                  std::span<const std::string> names);

// ---- writing a transform -------------------------------------------------------------------------

// Sets a node's position / rotation (Euler degrees) / scale, writing **both** the parameter's base
// value and the node's own authored transform.
//
// Both, because both are read and they are read by different things. The parameter is what the
// flattened scene, the modulation chain and the project file use; `CompositionNode::transform` is
// what a node re-registers its parameters from when it is added back after an undo, and what the
// scene file falls back to. This project has twice lost time to a value that lived in two places
// and was written in one.
bool setNodePosition(app::Engine& engine, const std::string& node, glm::vec3 value);
bool setNodeRotation(app::Engine& engine, const std::string& node, glm::vec3 eulerDegrees);
bool setNodeScale(app::Engine& engine, const std::string& node, glm::vec3 value);

// Shows or hides a node. Goes through the `visible` parameter rather than the node's own flag,
// because the parameter is what the renderer, the project file and the timeline all read -- so a
// hide is undoable, saveable and animatable for free, and is the same edit whether it came from
// this panel, a script or the assistant.
bool setNodeVisible(app::Engine& engine, const std::string& node, bool value);

// The three parameter paths a transform of `names` will write, for EditHistory::beginDrag.
[[nodiscard]] std::vector<std::string> transformParamPaths(std::span<const std::string> names);

// ---- operations ----------------------------------------------------------------------------------

// Adds nodes to the composition and returns the command that did it. Rebinds once at the end
// rather than once per node: `Engine::addNode` rebinds on every call, which for a brush stroke of
// fifty plants is fifty passes over every route and every timeline track.
//
// `created` is filled with the names the composition actually gave them, which may not be the names
// asked for -- names are made unique on the way in.
[[nodiscard]] EditCommand placeNodes(app::Engine& engine, std::vector<scene::CompositionNode> nodes,
                                     std::string label, std::vector<std::string>* created = nullptr);

// Deletes nodes and everything parented under them. Deleting a group deletes its contents, because
// a group whose deletion left twenty rocks scattered at the origin would be a trap.
[[nodiscard]] EditCommand deleteNodes(app::Engine& engine, std::span<const std::string> names);

// Copies nodes (and their descendants, keeping the hierarchy) and offsets the copies. §27: select,
// duplicate, move, duplicate again.
// Puts nodes the caller already owns into the scene, under fresh names, as one command. What Paste
// is: the clipboard holds clones that are no longer in the scene (and may never return to it, if
// what they were copied from has since been deleted), so this takes nodes rather than names.
//
// Hierarchy inside the batch is preserved -- a copied group's children hang off the *copy* -- and a
// parent naming something outside the batch is dropped to a root, because the thing it named may
// not exist in this scene at all.
[[nodiscard]] EditCommand pasteNodes(app::Engine& engine,
                                     const std::vector<scene::CompositionNode>& nodes,
                                     glm::vec3 offset, std::vector<std::string>* created);

[[nodiscard]] EditCommand duplicateNodes(app::Engine& engine, std::span<const std::string> names,
                                         glm::vec3 offset, std::vector<std::string>* created = nullptr);

// Makes a Group node at the centroid of `names` and parents them to it, adjusting each one's local
// transform so that **nothing moves**: a grouping operation that shifted the objects being grouped
// would be an operation nobody could use.
[[nodiscard]] EditCommand groupNodes(app::Engine& engine, std::span<const std::string> names,
                                     std::string groupName, std::string* created = nullptr);

// Dissolves a group: its children take the group's parent, keeping their world transforms, and the
// group node goes. `members` is filled with the freed children so the caller can select them.
[[nodiscard]] EditCommand ungroupNode(app::Engine& engine, const std::string& groupName,
                                      std::vector<std::string>* members = nullptr);

// ---- heroes (ADR-072/074) --------------------------------------------------------------------
//
// A hero is what the camera director travels towards and what a reaction profile answers the music
// through. Until now the only way to declare one was to hand-write a `heroes` block into the scene
// file, which meant the feature existed and could not be reached from the application.
//
// Designation is a *description* of a node that is already placed, not a change to it: nothing
// moves, resizes or relights. What the hero gets is measured from the node -- where it stands, how
// big it is, how far a camera should stand off to see it -- which is the part nobody should have to
// type.

// Is this node declared a hero? True when any of the scene's heroes names it (see `heroNamesNode`).
[[nodiscard]] bool nodeIsHero(const scene::Composition& composition, const std::string& name);

// The hero `name` would become, measured from the flattened scene.
//
// The numbers that cannot be measured take the type's defaults, with two exceptions that are
// derived because a default would be actively wrong at scale: a camera stand-off of `3r + 1.5h`
// (which puts Glowmere's authored heroes within a few metres of the distances a person chose for
// them by eye), and an activation radius of three times that, which is the ratio those same
// authored heroes use.
//
// `importance` is deliberately left at the default: it is a judgement about the piece, not about
// the geometry, and heroes that all claim to be the subject are heroes among which nothing can be
// chosen. Ties are broken by the order they were designated in.
//
// Non-const because measuring means reading the flattened scene, which may have to be rebuilt.
[[nodiscard]] world::HeroPoint heroFromNode(scene::Composition& composition, const std::string& name);

// Declares or undeclares `names` as heroes, as one command. Toggling off keeps the whole hero in
// the command, so an undo brings back an authored importance and reaction profile rather than a
// fresh guess at them.
//
// Declaring takes node names, because the hero is measured from the node. Undeclaring takes either
// a node name or a hero's own name, because a hero that names an assembly has no single node to be
// found from -- and one that could not be undeclared from the application would be a hero the
// director keeps travelling to with nothing to say about it.
[[nodiscard]] EditCommand setNodesHero(app::Engine& engine, std::span<const std::string> names, bool hero);

// Moves a selection by a world-space delta as one command. Used by numeric entry and by the arrow
// keys; the gizmo drag uses EditHistory's drag coalescing instead, because a drag is one edit made
// of sixty writes.
[[nodiscard]] EditCommand moveNodes(app::Engine& engine, std::span<const std::string> names,
                                    glm::vec3 delta);

} // namespace avgen::ui
