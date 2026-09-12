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
class Selection {
public:
    [[nodiscard]] const std::vector<std::string>& nodes() const { return nodes_; }
    [[nodiscard]] bool empty() const { return nodes_.empty(); }
    [[nodiscard]] std::size_t size() const { return nodes_.size(); }
    [[nodiscard]] bool contains(const std::string& name) const;
    // The active object: the last one added. Empty when nothing is selected.
    [[nodiscard]] const std::string& primary() const;

    void clear() { nodes_.clear(); }
    void set(std::string name);
    void set(std::vector<std::string> names);
    void add(std::string name);          // no-op when already selected
    void remove(const std::string& name);
    void toggle(std::string name);       // shift-click
    // True when this changed anything, so a caller can avoid recording a selection edit that did
    // nothing.
    bool retainOnly(const scene::Composition& composition); // drops names that no longer exist

private:
    std::vector<std::string> nodes_;
};

// ---- group navigation --------------------------------------------------------------------------

// The outermost Group above `name`, or `name` itself when it is not in one. Clicking an object
// inside a group selects the group, which is what every tool does and what makes a group worth
// making; holding the modifier that bypasses it is the caller's decision, not this function's.
[[nodiscard]] std::string groupRootOf(const scene::Composition& composition, const std::string& name);
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

// Moves a selection by a world-space delta as one command. Used by numeric entry and by the arrow
// keys; the gizmo drag uses EditHistory's drag coalescing instead, because a drag is one edit made
// of sixty writes.
[[nodiscard]] EditCommand moveNodes(app::Engine& engine, std::span<const std::string> names,
                                    glm::vec3 delta);

} // namespace avgen::ui
