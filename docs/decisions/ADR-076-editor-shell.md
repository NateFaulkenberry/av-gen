# ADR-076: The editor shell

Status: accepted
Date: 2026-09-10

## Context

Everything the world editor can do was already reachable (ADR-066, ADR-068, ADR-069) and none of it
was *comfortable*. The application opened at 1440x900 whatever the display was, and eight floating
ImGui windows opened on top of each other in the middle of it. The first thing a person saw was a
dashboard with a world somewhere behind it, and the first thing they had to do was drag four windows
out of the way to find the thing they had come to edit. Nothing was remembered: `io.IniFilename` was
set to `nullptr` with the comment "no layout persistence in 0.1", so every launch put the panels back
where they had just been dragged from.

This is phase one of a larger editor brief. It is only the shell: the workspace the panels live in.
No panel's contents changed.

## Decision

### The canvas is a rectangle, not the window with chrome on top

ImGui's docking was already enabled and unused. The shell is one `DockSpaceOverViewport`, and the
world lives in a window called **Viewport** docked into its central node.

The obvious cheaper arrangement — `ImGuiDockNodeFlags_PassthruCentralNode`, the frame still drawn
edge to edge, the panels docked over the top — was built first and is wrong. It looks nearly right
in a screenshot and is wrong in the thing that matters: the world is *behind* the editor rather than
*beside* it. With the dark theme's 94%-opaque window background the scene bleeds visibly through
every panel, and the picture is the size and shape of the window rather than of the space it is
actually shown in. So:

- The frame is rendered into `finalTexture_` **at the canvas's own size**, and shown with
  `ImGui::Image`. The WGPU backend takes a `WGPUTextureView` as its `ImTextureID` and builds the
  bind group on demand, so this needs nothing from the renderer.
- The main window's surface is no longer presented through the output mapper at all. It is
  **cleared**, and ImGui draws everything on it — the canvas included. (Projection outputs and
  Syphon/NDI still publish `finalTexture_` through the mapper as before; they now get the
  canvas-sized frame, which is the frame.)
- `ImGuiCol_WindowBg` is forced opaque, because translucency over a full-window render was the only
  thing it was ever buying.

Three things follow from the canvas being a sub-rectangle, and all three are silent when wrong:

1. **The render target is the canvas.** `renderer_->resize(canvas)` and `engine_->setViewport(canvas)`.
   The scene renderer takes its aspect from its own HDR target's dimensions, so the camera follows
   automatically and there is no second place for the aspect to be got wrong.
2. **The canvas size is known one frame late.** ImGui decides how big the central node is while it
   lays the frame out; the renderer needs the number before anything is drawn. So the host reads
   *last* frame's rect. One frame of stretch while a splitter is being dragged, nothing on a still
   window. The alternative — laying out twice per frame — buys a frame of accuracy during a drag and
   costs a whole second layout every frame.
3. **A click is measured from the canvas.** `canvasPixelFor` subtracts the canvas origin and *then*
   scales points to pixels. Skipping the subtraction leaves picking wrong by exactly the width of
   the left-hand column: every click still picks something, so nothing looks broken. It has a unit
   test with that number in it.

The centre is flagged `NoDockingOverCentralNode`, and the central node itself carries `NoTabBar` and
`NoDockingOverMe`, so nothing can be dropped in beside the world and there is no tab bar over it.
`buildDefaultDockLayout` docks no *panel* into the centre whatever the registry says, and a test
asserts none asks to. Three guards, because this is the one property the whole phase exists to
establish.

### The canvas owns the mouse, and `WantCaptureMouse` no longer can

The viewport used to get an event when `!imgui_->wantsMouse()`. The canvas is an ImGui window now,
so `WantCaptureMouse` is true precisely when the pointer is on the world — the old test inverted
itself. The canvas reports its own `IsWindowHovered` instead, which is false when a panel, a popup
or a menu is over it, and that is the gate.

The rule from ADR-068 survives unchanged and explicitly: a drag that began on the canvas keeps the
mouse until the button is released, however far the cursor wanders.

### The panel registry is one table, and the menu is generated from it

`src/ui/editor_layout.hpp` holds every panel: its id, its menu label, the region the default layout
docks it in, whether it opens by default, and a line of tooltip. The View menu iterates that table
rather than listing items by hand, which is how the **Render** window came to be unreachable — it
had a `drawRender`, a `showRender_` and no menu item, and had been dead for some time before this
change found it.

The id is the ImGui window title, the key in the layout file *and* the name the dock builder docks
by. One string doing three jobs cannot drift; three strings and a mapping table eventually do.

The default layout:

| Region | Panels | Share |
|---|---|---|
| Left | World Builder, Assets | 19% width |
| Right | World, Parameters, Render | 21% width |
| Bottom | Control, Analysis, Modulation, Graph | 26% height |
| Centre | the canvas — the world, and nothing else | 60% x 74% |

Panels in a region share one dock node, so they arrive as tabs: everything that was open before is
still open, and none of it costs the canvas anything beyond one tab bar per side. Build on the left,
inspect on the right, run underneath.

### Layout persistence is split in two, along the line of who knows what

**ImGui keeps the dock tree**, in an ini beside the recent-files list
(`<prefs>/editor-layout.ini`). It already serialises split ratios, tab order, per-window geometry
and which node each window sits in, and it restores them with the same node ids it wrote. Writing
that ourselves would be a second, worse implementation of a format ImGui is going to keep writing
anyway, and the two would disagree the first time somebody dragged a splitter.

**avgen keeps which panels are open**, in `<prefs>/editor-layout.json`, in the same shape as
`recent.json` (format, version, a missing file is a first run, a malformed file is an error that
leaves the running state alone). Those booleans are ours; ImGui has never heard of them. It also
records the four dock node ids the default layout minted, which is what lets a panel opened later
land in its own region instead of floating over the world — `SetNextWindowDockID(node,
ImGuiCond_FirstUseEver)`, which ImGui applies only to a window it has no saved settings for, so a
panel somebody deliberately tore off stays torn off.

There is no hook to set a dirty bit in, because ImGui writes the flags through the `bool*` that
`slot()` hands it. The panel compares a cheap signature between frames instead and writes when it
moves, throttled to once every two seconds the way ImGui throttles its own ini — dragging down the
View menu with the button held toggles items on the way past, and each of those should not be a file
write.

### Restore Default Layout rebuilds the tree, in this process, now

`View > Restore Default Layout` resets the open-panel set and asks for a rebuild; the next frame
runs `DockBuilderRemoveNode` / `AddNode` / `SplitNode` / `DockWindow` / `Finish` against the live
dockspace. Deleting the ini and telling the user to restart is not restoring a layout, it is
declining to.

The same builder runs automatically when ImGui found no ini at all, and on the first frame if the
dockspace is somehow still empty. **Only on the first frame**: asking every frame would rebuild the
default the moment somebody dragged the last panel out of the tree, which is fighting them over
their own layout rather than restoring anything.

### A split ratio is restated against the node that is left

`DockBuilderSplitNode` cuts the node it is given, not the original. The right-hand column is cut
from what survived the left-hand one, so handing it the raw 0.21 yields 0.17 — narrow enough to look
like a style choice rather than a bug. `splitRatioWithin` does the division, and a test pins the
result.

### The window opens maximised unless a size was asked for

`WindowDesc::maximised` maps to SDL's `MAXIMIZED` creation property, which the application sets when
`--size` was *not* given. Maximising after creation would work, but the window would appear at one
size and jump to another, having already configured its Metal surface at the first — two
configurations and a visible flash for nothing.

`--size` still wins outright. That flag is how a run is made reproducible for a screenshot or a bug
report, and a window that quietly ignored it would not be. True fullscreen, borderless and the
multi-display output windows are untouched.

While there: the start-up log printed the size that was *asked for*. It now prints the size SDL
gave, because otherwise a maximised window logs "1440x900".

### The status bar carries measurements only

A one-line bar across the foot, outside the dockspace: frame rate, frame interval, CPU frame time,
GPU frame time, resolution, draw calls, triangles, the current selection, the armed placement asset,
the status message, and the adapter. Every one of those already existed as real instrumentation and
is read straight from `FrameStats` or the engine. The GPU time says `gpu n/a` when the timer has not
reported rather than showing a zero, because a zero is a number somebody will act on.

## Consequences

- `ControlPanel` no longer owns nine `show*_` booleans; it owns an `EditorLayout`. Adding a panel is
  now one row in the registry and one line in `drawPanels`.
- The status bar's resolution is the canvas's, not the window's, because that is what was rendered.
- `ImGuiLayer::create` takes the ini path and reports whether one already existed. Passing nothing
  keeps the old behaviour, which is what a headless or offline path wants: a render should not
  rearrange somebody's editor.
- The **Render** window is reachable again.
- Two scripted-input probes, `AVGEN_VIEWPORT_PROBE` and `AVGEN_VIEWPORT_DRAG`, push real SDL
  events at points expressed as fractions of the canvas. Nothing about the path is bypassed. They
  exist because an editor's interaction cannot be checked by reading it, and they are what proved
  the click conversion (same canvas fraction, two panel widths, identical world position) and the
  drag rule (the eye keeps moving after the cursor leaves the canvas).
- `src/ui/editor_layout.cpp` is deliberately free of ImGui so the unit tests can compile it —
  `avgen_tests` links `avgen_core` and has neither ImGui nor Dawn. The rules about where panels go
  are the part that goes quietly wrong, and a rule that needs a window and a GPU to check is a rule
  nobody checks.

## What this does not do

Everything else in the editor brief. No gizmos, no asset thumbnails, no drag-and-drop placement, no
painting, no undo, no camera or music editor, no world map, no project system. A panel that was a
wall of sliders before is the same wall of sliders now, in a better place.

Three limits worth stating:

- Resizing a panel stretches the world for one frame, because the canvas's new size is only known
  after the frame it appears in. Visible if you look for it while dragging a splitter; invisible
  otherwise.
- `--capture` and the offline render paths still size themselves from the window and the project's
  render settings respectively, not from the canvas. That is right — a render is not a screenshot of
  an editor — but it does mean the live picture and the rendered one are framed at different aspects
  unless you match them.
- A panel added to the registry after somebody has an ini gets the region hint, but a *region* added
  later would not exist in their saved tree. Restore Default Layout is the answer, and it is in the
  menu.
