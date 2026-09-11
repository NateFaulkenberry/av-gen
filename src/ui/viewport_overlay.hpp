#pragma once

// Drawing the editor over the world (ADR-092, world-authoring-spec §18/§22/§23/§24).
//
// The ghost, the selection outlines, the gizmo and the drag readout are drawn into the canvas
// window's ImGui draw list rather than into the scene. That is a deliberate choice and not a
// shortcut:
//
//   * **Nothing the editor draws can reach a render.** The offline renderer reloads the project from
//     file and draws the scene; a gizmo that lived in the scene as geometry would be a gizmo that
//     could end up in a take.
//   * **No renderer or shader change is needed**, which matters while other passes hold those files.
//   * **Input and drawing agree by construction.** The handle the pointer is over is decided from
//     the same projection that drew it, in the same frame.
//
// The cost is that the overlay is not depth-tested: a gizmo behind a hill still draws. That is what
// every 3D tool does with its gizmo, and for the ghost it is the right answer too -- a preview you
// cannot see because a leaf is in front of it is a preview that has failed at its one job.
//
// This file is the *only* place in the editor that knows about pixels. Everything it draws it is
// told; see world_editor.hpp.

#include "app/engine.hpp"
#include "scene/camera.hpp"
#include "ui/editor_layout.hpp"
#include "ui/world_editor.hpp"

#include <imgui.h>

namespace avgen::ui {

// Reads the pointer out of ImGui and expresses it in the canvas's normalised device coordinates.
// Call while the canvas window is current, so `overCanvas` answers false when a panel, a popup or
// a menu is over the world.
[[nodiscard]] EditorInput editorInputFromImGui(const CanvasRect& canvas);

// Draws everything the editor wants shown this frame into the current window's draw list.
void drawViewportOverlay(const WorldEditor& editor, const scene::Camera& camera, const CanvasRect& canvas,
                         float aspect);

// The one-line heads-up in the corner of the viewport: the mode, and what the next click will do.
// Separate from the status bar because it has to be where the artist is looking (§42: "the viewport
// should clearly communicate the current mode").
void drawViewportHud(const WorldEditor& editor, const CanvasRect& canvas);

} // namespace avgen::ui
