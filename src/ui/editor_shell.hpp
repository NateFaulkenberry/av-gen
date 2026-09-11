#pragma once

// The ImGui half of the editor shell (ADR-076): the dockspace the world shows through, the dock
// tree the default layout builds, and the status bar along the foot. The rules about which panel
// belongs where live in editor_layout.hpp, which knows nothing about ImGui.

#include "ui/editor_layout.hpp"

#include <imgui.h>

namespace avgen::ui {

// Fills the viewport's work area with a dockspace. The centre node holds the canvas window and
// nothing else; everything around it is opaque chrome. Returns the dockspace's node id.
[[nodiscard]] ImGuiID beginEditorDockspace();

// The window in the central dock node, showing `texture` -- the frame rendered at the canvas's own
// size. Zero draws the empty ground instead, which is what the first frame of a run looks like.
// `centreNode` is the dock node to claim if the canvas has never been placed: without it a saved
// layout from a build that had no canvas window would open with the world floating over the
// panels. Returns where it ended up, for the host to size the next frame's render target from.
// Keeps the centre the canvas's alone, every run.
//
// `buildDefaultDockLayout` sets `NoTabBar | NoDockingOverMe` on the central node and the old
// comment claimed both survive a restart. Only one does: ImGui's .ini serialiser writes
// `CentralNode` and `NoTabBar` and **not** `NoDockingOverMe`, so the half that actually keeps
// panels out of the centre is the half that is lost on load. A layout saved once with a panel in
// the centre then keeps it there for ever, and because the node also carries `NoTabBar` there is
// no tab bar to reveal that two windows are sharing one rectangle -- so a panel paints over the
// world instead of beside it.
//
// Reapplies the flags and moves any stranger back to the region it is registered for. Returns how
// many it evicted, which is zero on every run after the first.
std::size_t enforceCanvasCentre(ImGuiID dockspace, const EditorLayout& layout);

[[nodiscard]] CanvasRect drawCanvasWindow(std::uint64_t texture, std::uint32_t centreNode);

// Rebuilds the default dock tree under `dockspace`, discarding whatever is there: a column each
// side, a strip along the foot, and the canvas in the middle. Records the four node ids in
// `layout` so a panel opened later drops into its own region instead of floating over the world.
void buildDefaultDockLayout(ImGuiID dockspace, EditorLayout& layout);

// True when `dockspace` has no tree yet -- a first run, or an ini file that predates docking.
[[nodiscard]] bool dockspaceIsEmpty(ImGuiID dockspace);

// A one-line bar across the foot of the viewport, outside the dockspace. Submit it before the
// dockspace: ImGui takes the space out of the work area from the next frame onwards, and a
// dockspace sized from a stale work area overlaps it for that one frame either way.
[[nodiscard]] bool beginStatusBar(const char* name);
void endStatusBar();

} // namespace avgen::ui
