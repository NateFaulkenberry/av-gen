#pragma once

// The right-click menu for a world object, in one place.
//
// Written once and called from both the scene hierarchy and the viewport, because a context menu
// that offers different things depending on where you right-clicked the same object is a menu
// nobody can learn. It is also the only way to keep the addendum's section 10 honest at more than
// one call site: **every item here runs an existing command.** Cut, Copy, Paste, Duplicate, Delete
// and Select All go through `app::EditSystem::execute`, which ADR-101 names as the one route the
// menu, the keyboard and a context menu all take; group, ungroup, visibility, lock and hero go
// through `ui::WorldEditor`, which pushes to `EditHistory`. Nothing here mutates a composition
// directly, so everything here is undoable.
//
// Two things a professional editor would offer are deliberately absent, and their absence is the
// point rather than an oversight:
//
//   * **Rename.** There is no rename operation in this application. `WorldEditor` has none,
//     `world_edit.hpp` has none, `EditCommand` has no record that could carry one, and
//     `scene/composition.cpp` explicitly refuses to rename a hero. Adding one would be a new
//     mutation path and a new undo record -- exactly what section 10 says not to build behind a
//     context menu. A menu item that silently did nothing, or one that renamed without being
//     undoable, would both be worse than its absence.
//
//   * **Reset position.** Rotation and scale have obvious identities and resetting either is one
//     undoable command. Position's "identity" is the world origin, and an object teleported there
//     is almost never what was meant -- so the two that are useful are offered and the one that is
//     a trap is not.

#include <string>

namespace avgen::app {
class Engine;
class EditSystem;
} // namespace avgen::app

namespace avgen::ui {

class WorldEditor;

// What the menu needs from its host that it cannot reach itself.
struct WorldMenuHost {
    // Set true to ask the viewport to frame the selection. The camera stays the viewport's: this is
    // the same `frameSelectionRequested` flag the F key and the Frame button set, so there is one
    // answer to what framing means (see `Application::runLive`).
    bool* frameSelection = nullptr;
};

// Submits the body of an already-open popup. `clicked` is the node the pointer was over, which may
// be empty for a right-click on nothing. Returns true if an item was chosen.
//
// When `clicked` names an object outside the current selection, the menu acts on *that* object and
// says so -- right-clicking an unselected thing and getting a menu about something else is the
// commonest way a context menu surprises somebody.
bool worldObjectMenuBody(app::Engine& engine, WorldEditor& editor, const std::string& clicked,
                         const WorldMenuHost& host);

} // namespace avgen::ui
