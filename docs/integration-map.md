# Integration map

Written before any integration code, per the phase directive. What the application already has,
what it does not, and the shortest path from the tested-but-unreachable subsystems to something a
person can use.

## What already exists

**The application shell.** `app::Application` owns an SDL window, an ImGui layer, an `app::Engine`
and a `ui::ControlPanel`. Eight windows are already drawn: Control, Parameters, Modulation,
Analysis, Assets, Graph, Render, World.

**A scene mutation API, and it is already the shared one.** `app::Engine` exposes
`addNode(CompositionNode)`, `removeNode(name)`, `newComposition()`, `loadComposition`,
`saveComposition`, `loadProject`, `saveProject`, `loadEnvironment`, `addShaderLayer`,
`removeSource`, `removeWorldMacro`, plus accessors for `composition()`, `params()`, `post()`,
`lens()`, `exposure()`, `focus()`. This matters more than it looks: the future agent addendum asks
for one API that both the UI and an agent drive, and it already exists. Nothing in this phase
should add a second path.

**An asset browser.** `app::AssetBrowser` already scans directories and catalogues Project, Scene,
Graph, Preset, Model, Environment, Shader and Audio entries with name, category, description and an
optional thumbnail beside the file. Dynamic discovery from disk — the phase's §6 — is largely done;
what is missing is the semantic layer (`assets::AssetLibrary`, milestone 1) being wired to it, and
a way to *place* what you select.

**A world panel.** `ui::WorldPanel` has a layered parameter view (Beginner / Intermediate /
Advanced), a world overview tree, an inspector that answers "why is this moving" by listing every
route, timeline track, cue, state and macro writing to a parameter, a states and macros panel, the
World Director's knobs and look presets, and debug visualisation options. It carries a
`WorldSelection` with kinds Procedural / Field / Spline / Sdf / Particles / Material / Environment /
Camera, selected from the tree by name.

**Ecology.** `world::Ecology` places `ScatterLayer`s with biome densities, slope and altitude
limits, water avoidance with a shore offset, clustering, proximity relations, view distance, screen
culling, instance ceilings and mesh budgets. It is the authoritative placer and must stay so.

**Timeline, routes, macros, states, cues.** All present, all serialised, all able to drive any
parameter by path.

## What does not exist

| Gap | Consequence |
|---|---|
| **No viewport camera control.** Mouse events go to ImGui only; the camera moves by parameter or timeline. | You cannot look around a world you just generated. This is the first thing to fix. |
| **No viewport picking.** Selection is by name from a tree. | "Click the thing" is not possible yet. |
| **No placement.** The Assets window opens files; it cannot put an asset into the world. | `Engine::addNode` exists, so this is UI, not architecture. |
| **`assets::AssetLibrary` is not wired to `app::AssetBrowser`.** | The semantic layer is invisible. |
| **`WorldRecipe` / `WorldComposer` are not reachable.** | The critical one: no Generate World. |
| **`JobSystem` is not in the application.** | No job monitor, and no way to run generation off the UI thread. |
| **`Sequence` / shots are not reachable.** | Camera authoring is still hand-written keys. |
| **`MusicalEventDetector` is not fed.** | Eleven event kinds classified by nobody. |

## The shortest path

Two things make the whole architecture tangible at once, and neither needs picking or a new
renderer:

1. **Generate World.** Recipe → `composeWorld` → `ScatterLayer`s → a terrain node's scatter list →
   the existing ecology places them → the existing renderer draws them. Every step exists; what is
   missing is the twenty lines that connect them and a panel to press.
2. **The Job Monitor**, because generation should not block the UI thread and because the job
   system's honest progress is only worth having if somebody can see it.

Then a viewport camera, so the result can be looked at.

## Decisions this phase must not violate

- **One placer.** The composer emits `ScatterLayer`; ecology places it. A test asserts the type.
- **One scene API.** UI and any future agent both go through `app::Engine`. No parallel path.
- **One event system.** Musical events become *inputs to* the existing modulation machinery — a
  signal the route system can already consume — not a competing automation architecture.
- **AI stays optional and out of the frame loop**, and the built-in backend stays labelled analytic.
