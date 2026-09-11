---
id: start/interface
title: The Interface
category: Getting Started
summary: The dockspace, the twelve panels, the menu bar and the status bar.
order: 12
tags: interface, panels, layout, docking, status bar, menus
keywords: where is everything; how do i open a panel; what are the panels; i closed a panel by accident; the world fills the whole window
related: reference/panels, start/projects, troubleshooting/projects-and-assets
features: panel.world, panel.control, panel.parameters, panel.analysis
---

# The Interface

The editor is one window. The world is rendered into the middle of it; everything else docks around
the edges.

## The dockspace

Panels live in four regions: **left**, **right**, **bottom** and the **centre**. The centre belongs
to the world and nothing else can be docked there. Panels can be dragged between regions, torn off
to float, tabbed together and closed.

**View ▸ Restore Default Layout** rebuilds the arrangement the editor ships with and reopens the
default panels. **View ▸ Save Layout Now** writes the layout immediately; otherwise it is saved
periodically and on exit.

> [!NOTE]
> If you close the last panel in a side region, that region collapses and the world expands into
> the space. AV Gen notices this on the next launch and reopens one panel in each empty region,
> because a canvas that has eaten the whole window is almost never what was wanted. Within a single
> session the region stays empty if you emptied it deliberately.

## The panels

Every panel is listed in the **View** menu, with its one-line description as a tooltip. Five open
by default on the right and bottom; the rest you open when you need them.

| Panel | Region | What it holds |
|---|---|---|
| World Builder | left | recipe, Generate World, asset placement and the job monitor |
| Assets | left | everything the asset scan found, by kind |
| World | right | layers, inspector, scene states, direction, macros and debug draw |
| Parameters | right | every exposed parameter, by group |
| Composition | right | the 2D layers over the frame: text, shapes, timing and keys |
| Sequence | bottom | the piece in time: shots, scene cuts, character cues, lyrics and markers |
| Render | right | offline render settings, progress and the queue |
| Control | bottom | transport, audio response and performance |
| Analysis | bottom | bands, spectrum, onsets and the waveform |
| Modulation | bottom | routes, sources, presets, shaders, scene, timeline, control and outputs |
| Graph | bottom | the procedural graph editor |
| ImGui Demo | floating | the Dear ImGui widget gallery |

See [Panel reference](help://reference/panels) for what each one actually does.

## The menu bar

**File** opens and saves. **Camera** hands the camera to the music and takes it back. **View**
toggles panels and manages the layout. See [Menu reference](help://reference/menus).

## The status bar

One line across the foot of the window. Every number on it is measured; an unmeasured one says so.

| Field | Meaning |
|---|---|
| `N fps` | frames counted over windows of at least half a second |
| `N ms frame` | wall time between frames, including the wait for the display |
| `N ms cpu` | main-thread work only, excluding the waits |
| `N ms gpu` | real GPU timestamps for the whole frame, or `gpu n/a` if the adapter cannot measure them |
| `W x H` | the size of the **canvas** in pixels, not the window |
| `N draws / N tris` | draw calls and triangles submitted |
| selection | the selected object, or `no selection` |

The adapter and backend sit at the far right and are the first thing clipped when the window is
narrow.

> [!TIP]
> The `W x H` field is worth a second look. The editor renders the world at the canvas's size times
> the display's backing scale, so a window you think of as "1440x900" typically renders a canvas of
> about 2880x1166 — around 2.6 times the pixels of a 1440x900 benchmark. This is the single most
> common reason a scene feels slower in the editor than its published numbers suggest. See
> [What costs what](help://performance/what-costs-what).

## The authoring layer

The World panel carries a **Beginner / Intermediate / Advanced** selector. It filters which
parameter groups the Parameters panel shows:

- **Beginner**: `macros/`, `scene/`, `env/`, `post/`, `camera/`, `root/`, `shader/`
- **Intermediate**: the above plus `procedural/`, `field/`, `spline/`, `sdf/`, `material/`,
  `particles/`, `nodes/`
- **Advanced**: everything

This is the control that hides parameters. If a parameter you expect is missing, raise the layer.
