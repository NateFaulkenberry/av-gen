# The world editor

Everything you can currently do to build and edit a world, and how to do it.

This describes what is **implemented and reachable**, not what is planned. Where something exists in
the engine but is not yet wired to a control, it says so.

Related: [world-performance.md](world-performance.md) for what a world costs,
[glowmere-world-builder-audit.md](glowmere-world-builder-audit.md) for the art direction the profiles
are read from, and [asset-library.md](asset-library.md) for asset provenance and licensing.

---

## The two ways in

**From the app.** Launch `avgen`, then **View → World Builder**. That one window holds the recipe,
the Generate button, asset placement, the contents of whatever you last generated, and the job
monitor.

**From the command line.** `avgen --generate <recipe.json>` composes a world at start-up and installs
it, with or without a window:

```sh
# Look at it
./build/release/src/avgen --generate examples/recipes/glowmere.recipe.json --play

# Render it
./build/release/src/avgen --headless --generate examples/recipes/glowmere.recipe.json \
  --render out --format png --range 0:10 --fps 30 --size 1280x720
```

Both paths go through the same two calls — `world::composeWorld` then `app::installWorld` — so a
world generated from the command line and one generated from the button are the same world. There is
no second implementation, deliberately (ADR-066).

---

## 1. Recipes: what a world is made of

A **recipe** is a world's intent with no objects in it. It says a world should be dense underfoot,
sparse on the ridge, mostly flora, lit by itself, and a third empty. It does not say where anything
goes — that is the composer's job.

Two shipped examples: `examples/recipes/glowmere.recipe.json` and
`examples/recipes/bioluminescent-valley.recipe.json`.

Every weight is `0..1` and every field has a default, so a recipe naming three fields is legal and
means "the rest as usual".

### The fields

| Group | Field | What it does |
|---|---|---|
| | `world` | Name. Required, non-empty. |
| | `seed` | The whole composition is a pure function of this. |
| | `extent` | Metres across. The panel's slider runs 50–2000. |
| | `assetLibrary` | Which manifest to compose from, relative to the recipe. |
| `composition` | `foreground` / `midground` / `background` | How densely each depth band is planted. |
| | `negative_space` | How much is deliberately left empty. A *positive* instruction, not the absence of the others. |
| | `focalStrength` | How far the focal subject outranks everything around it, and how many heroes you get. |
| `ecology` | `flora`, `fungi`, `rock`, `crystals`, `creatures`, `structure` | What lives here. **A weight of 0 means absent, not rare.** All zero is rejected. |
| `atmosphere` | `fog`, `spores`, `floating_elements`, `depthHaze` | The air and what is suspended in it. |
| `lighting` | `moon`, `bioluminescence`, `volumetric`, `bounce` | Proportions of the world's illumination, not intensities. |
| `art` | `profile` | Names a built-in art direction. See below. |
| | `palette`, `contrast`, `saturation`, `organicMotion`, `chaos` | Overrides on top of the profile. |

The recipe JSON accepts both spellings for a few fields (`negative_space` / `negativeSpace`,
`crystals` / `crystal`, `creatures` / `creature`, `floating_elements` / `floating`, `moon` / `key`),
so a hand-written recipe reads as prose.

### In the panel

**Recipe** section: name, seed, extent, then collapsible **Composition**, **Ecology**, **Atmosphere**
and **Lighting** groups of sliders — one per weight above. Hover a slider for what it means.

Press **Generate World**. Composition runs on a job worker, so the UI stays responsive; the result is
installed on the main thread on a later frame. Watch progress in **Jobs** at the bottom of the
window. A job that cannot measure itself shows a striped bar and the word *indeterminate* rather than
a number you might act on.

---

## 2. Art-direction profiles: what a world looks like

A recipe says how much of a world there is; a **profile** says what it looks like — palette, emission
ladder, atmosphere, light rig and post-processing restraint (ADR-070).

Name one with `art.profile`. Three ship:

| Profile | What it is |
|---|---|
| `glowmere` | A cool alien night valley lit by its own flora, with one warm hero. Read out of the authored Glowmere scene. |
| `emberwaste` | A hot dry plain under a low sun. Almost nothing emits; the accent is *cool* against a warm world. |
| `palefen` | Flat overcast over standing water. Desaturated, nothing luminous, ambient brighter than the key. |

**A profile is a starting point, not a cage.** Anything the recipe states explicitly wins — write
your own `art.palette` and you get your own palette, and your own accent with it.

**An unknown profile name is an error**, not a silent fallback, and the message names the profiles
that do exist. A typo that quietly returned Glowmere is a typo that ships.

### Two rules the profile enforces

These are validated, not merely documented, because both are what a well-meaning edit destroys first:

- **The emission ladder must have a gap.** The step from ordinary vegetation to the first bright rung
  must be at least 4× (Glowmere's is 13×). That gap is what makes bright things read as a different
  *kind* of thing rather than the top of a gradient. A ladder edited toward "a bit more glow
  everywhere" closes it, and the profile is rejected.
- **The accent is reserved.** No scatter layer may wear the hero's colour while `reserveAccent` is
  set. One warm light in a cool world is what makes a hero findable from anywhere in frame, and a few
  species wearing it is the cheapest way to lose that.

A world where nothing glows at all is still legal — `palefen` is one.

---

## 3. Asset libraries

The composer can only place what a manifest describes. Two ship:

- `assets/manifest.json` — the Kenney Nature Kit plus the Quaternius vegetation.
- `assets/glowmere.manifest.json` — the Quaternius species Glowmere is built from, alone.

**Prefer a single-pack manifest for a world with a look.** A library spanning two packs mixes two
artistic languages, which is the opposite of what a controlled palette is for. That is why Glowmere's
recipe names its own manifest.

A manifest entry says what a species *is*: its real-world height, how important it looks, how densely
it grows, what it is made of, and how much it varies. Crucially, `material.emissive` is a **weight**
(0..1) — how far up its own rung of the profile's ladder it sits — not an absolute intensity. So a
canopy tree can legitimately carry 1.0 and still be faint, because the rung it stands on is faint.

Tags steer which rung a species lands on: `rare` / `accent` take the top, `beacon` the next, `special`
the first rung across the gap. `foreground` / `midground` / `background` override the depth band that
height would otherwise imply.

> **Note:** the Quaternius pack is gitignored and downloaded by hand. A fresh checkout has the
> manifest entries but not the files, and the composer will warn per missing asset.

---

## 4. Moving around: the viewport

Mouse input reaches the scene whenever no panel wants it. A drag that starts on the scene keeps the
mouse until you release, even if the cursor crosses a panel — otherwise an orbit stops mid-swing.

| Gesture | What it does |
|---|---|
| **Left drag** | Orbit around the camera's target |
| **Shift + left drag**, or **middle drag** | Pan — eye and target move together |
| **Right drag** | Look around — the eye stays put |
| **Wheel** | Dolly toward or away from the target |
| **Left click** (without dragging) | Pick: select the node under the cursor |
| **Shift + left click** | Add to / remove from the selection |
| **Alt + left click** | Select the object inside a group rather than the group |
| **Left drag a box** (Select mode) | Select everything the box covers |
| **Left click on the sky** | Deselect |

In **Place** mode the left button paints instead, and while the pointer is over a gizmo handle it
drags that handle. The other buttons are always the camera's, so you can look around in any mode.

Panning is scaled by distance, so a drag moves the same amount of *picture* whether you are two
metres from your subject or two hundred.

**The first gesture switches the camera to free mode and says so in the log.** `camera/position` and
`camera/target` are only read in free mode, so a drag in orbit mode would move two parameters and
change nothing on screen — indistinguishable from a dead input.

The camera's pose lives in those two parameters and nowhere else, so it saves, loads, automates and
routes like everything else.

### Picking

A click reads the identifier and linear-depth targets the scene pass already writes, so picking costs
nothing per frame. It resolves the entity under the cursor back to the **node** you placed and selects
it; its parameters are then at `nodes/<name>/…` in the inspector.

Clicking terrain or procedural instances reports a world position but selects no node — those are
drawn outside the entity list. That is expected, and the position is still what placement uses.

---

## 5. Building by hand: the Edit panel

Everything you do to a world by pointing at it is in one panel, and it shows the controls for the
mode you are in (ADR-092). Two modes, and the viewport says which one you are in at all times:

| Mode | Key | What a click does |
|---|---|---|
| **Select** | `Q` | Picks the object under the cursor and gives it a transform gizmo. |
| **Place** | `B` | Paints the armed asset. |

### 5.1 The palette

Filter by name, or click a category chip. Each asset is a swatch whose **bar height is its real
height** against the tallest thing in the library, so the palette shows scale at a glance — which is
the fact a placement gets wrong most often. A ring means the species glows. Hover for its name,
category, height, footprint, triangle count and importance.

Click a swatch to arm it and switch to Place.

### 5.2 The ghost

**With an asset armed, the viewport shows exactly what a click would make**, before you make it:

- a **footprint circle** on the ground for every instance, drawn on the surface so it follows a
  hillside;
- a **bounding cage** and an **orientation line** for each one, so height and facing are visible
  before they are committed;
- the **brush disc** itself, so the radius is something you can see rather than a number you have to
  imagine;
- a **terrain-contact cross** and the **surface normal** at the exact point under the cursor;
- a heads-up panel giving the **ground height**, the **slope in degrees**, and warnings for **water**
  and **outside the world**.

The panel's border is **green when the click will place something and red when it will not**, and
when it is red it says *why*, with the numbers in it:

| It says | What it means |
|---|---|
| `too steep: 47 degrees, limit 30` | The ground is steeper than **Max slope**. |
| `under 1.4 m of water` | **Keep out of water** is on and this is a lake. |
| `blocked by elder_tree` | **Keep clear of other objects** is on and something is already there. |
| `outside the world` | Past the terrain's extent. |
| `no ground under the cursor` | The sky, or past the far edge. |

Instances are checked **one at a time**: a stroke on a shoreline places the half that is on the bank
and refuses the half that is in the water, and shows you which is which while you are still holding
the button. Each instance is dropped onto the ground under *itself*, so a stroke across a hillside
follows the hillside.

### 5.3 Brush modes

| Mode | What a stroke makes |
|---|---|
| **Single** | One object where you clicked. |
| **Scatter** | A spaced scatter filling the brush. Drag to keep painting. **Radius**, **Spacing**, **Density**. |
| **Cluster** | A few objects tight together, as one thing that grew there. **Count**, **Spread**, **Clumping**. |
| **Landmark** | One, deliberately enormous. **Times normal size**. |
| **Eraser** | Drag over what you want gone. |
| **Replace** | Removes what the brush covers, then paints over it. |

**Variation**: scale range, random turn, sink (negative lifts it clear of the ground), lie along the
slope, and a seed — `0` means a new arrangement every stroke, anything else repeats exactly.

**Where it may land**: max slope, keep out of water, keep clear of other objects, and how much
clearance to demand.

**A stroke makes a group** turns everything one drag lays down into a single group, so a thicket
painted in one gesture is one thing to move afterwards.

Three things worth knowing about how these behave:

- A **scatter throws darts** against the spacing radius rather than scattering uniformly, because a
  uniform scatter over a disc clumps — and you are already deciding where the density goes by moving
  the mouse. **Density** thins the stroke without ever letting two things get closer than the
  spacing.
- A **cluster** fills its disc evenly by default, so it reads as a patch of something growing rather
  than as a target. **Clumping** pulls it toward the centre when you want that.
- Every placement is deterministic given its seed, and placed objects are **ordinary glTF nodes** —
  select, move, rename and delete them like anything else.

### 5.4 Selecting and moving

Click to select. **Shift-click** adds. **Drag a box** over the viewport to catch several. Clicking
something inside a group selects **the group**; **alt-click** reaches the object inside it.

A selection gets a gizmo in the middle of its bounding box:

| | |
|---|---|
| `W` | **Move** — three axis arrows and three plane handles; the centre dot slides in the screen plane |
| `E` | **Rotate** — three rings |
| `R` | **Scale** — three axis handles and three plane handles; the centre scales uniformly |
| `X` | **World / local** space |

Snapping is three fields in the panel: a **grid** in metres, an **angle** in degrees and a **step**
for scale. `0` is off, which is the default. While you drag, the value is printed next to the
cursor: `+4.00 m along X`, `-30.0 deg about Y`.

Several objects rotate and scale about the **shared pivot**, not each about its own, so turning a
group of rocks looks like turning an arrangement.

The panel also gives numeric **Position / Rotation / Scale** for the active object, and its size in
metres. Typing in them is one undo step, the same as a drag.

**Arrow keys** nudge the selection by the snap grid (or 0.1 m when there is none); hold shift for ten
times as far. With nothing selected they still scrub the transport, as they always did.

### 5.5 Groups

Select two or more things and press **Group** (`Cmd+G`). A group is an empty node the members are
parented to: it **moves, rotates and scales as one unit**, and the members stay ordinary nodes you
can still select and edit individually. Grouping moves nothing, and neither does **Ungroup**
(`Shift+Cmd+G`).

Deleting a group deletes its contents, because a group whose deletion left twenty rocks scattered at
the origin would be a trap. Undo brings all of it back, still grouped.

### 5.6 Duplicate, copy, paste

`Cmd+D` duplicates the selection beside itself and selects the copies, so duplicate-move-duplicate
works. A duplicated group copies its whole hierarchy — the copy's children hang off the *copy*.
`Cmd+C` / `Cmd+V` do the same through a clipboard.

### 5.7 Undo

`Cmd+Z`, `Shift+Cmd+Z`. The Edit panel names what the next undo will take back, and so does the
status bar.

It covers **everything in this panel**: placement, painting (a whole stroke is one step), erasing,
deletion, moves, rotations, scales, numeric entry, nudges, grouping, ungrouping, duplication and
paste. A drag is one step however many frames it took, and a drag that ends where it started is no
step at all. Pressing escape during a drag puts it back.

Undo restores **what was selected** at the time, so undoing a delete hands you back the things that
came back rather than leaving you with nothing chosen.

What it does **not** cover: Generate World (it replaces the composer's nodes wholesale, and the
history is cleared), and anything done through the Parameters panel or the timeline.

### 5.8 Keyboard

| | |
|---|---|
| `Q` / `B` | Select / Place mode |
| `W` / `E` / `R` | Move / rotate / scale gizmo |
| `X` | World / local space |
| `F` | Frame the selection |
| `Cmd+A` | Select everything |
| `Cmd+D` | Duplicate |
| `Cmd+C` / `Cmd+V` | Copy / paste |
| `Cmd+G` / `Shift+Cmd+G` | Group / ungroup |
| `Delete` / `Backspace` | Delete the selection |
| Arrows (+shift) | Nudge |
| `Cmd+Z` / `Shift+Cmd+Z` | Undo / redo |
| `Escape` | Cancel the drag in progress |

---

## 6. Inspecting what you generated

**World Builder → World contents**, populated after a Generate.

- **Heroes** — listed in the order the composer ranked them, which *is* the composition: that order
  is what a camera director sorts by. Each shows its height, stand-off, activation radius and reaction
  profile. **Frame it** points the viewport at that hero, fitted to its own size at the current field
  of view. Transforms are edited through `nodes/<name>/…` in the inspector rather than through a second
  editor here, so nothing fights a drag.
- **Zones** — the world's visual chapters and their per-category density emphasis.
- **Art direction** — the resolved profile: its ladder span and gap, its key-to-ambient ratio, and its
  reserved accent colour.

---

## 7. What a generated world actually contains

Generating installs, in one step:

- a **terrain** sized to the recipe, with the five-biome vocabulary the composer places against
  (`forest`, `meadow`, `marsh`, `scree`, `rim`) and enough moisture in its basins for marsh to exist;
- one **scatter layer per eligible asset**, banded by height into foreground / midground / background,
  each with its emission taken from the profile's ladder;
- **heroes**, placed on ground that supports a composition and cleared around;
- **ecological zones** anchored on those heroes;
- a **negative-space corridor** from the viewpoint to the subject — mandatory, and canopy-only so the
  lane keeps its floor;
- the **environment**: fog, volumetrics, sky, stylized hemisphere and ground glow;
- a **light rig** carrying the profile's key-to-ambient ratio;
- **post-processing** restraint — bloom threshold and intensity, tonemap, chroma retention;
- **camera position and target** framing the composition, for a world that had no terrain before.

All of it is ordinary scene data. Save the project and it round-trips.

---

## 8. Limits worth knowing

- **Generating twice replaces rather than appends.** The composer owns the terrain's ecology, so a
  second Generate overwrites the layers and the hero nodes rather than doubling them. Hand-placed
  objects are *not* touched.
- **Generating into a scene that already has a terrain** keeps that terrain and its camera; only a
  world with no terrain gets a framed camera, because a camera in an existing scene is somebody's
  decision.
- **A hero is currently a single library asset scaled up.** `HeroPoint::assembly` — a hero as an
  authored group of nodes, which is what Glowmere's elder actually is — is a declared field with no
  implementation yet.
- **The camera director is not driven by the editor.** `app::camera_director` will direct heroes from
  a musical structure and install the result on the timeline, but nothing in the UI calls it yet.
- **Generate World is not undoable.** The editor's undo covers everything you do by hand (§5.7),
  but a generate replaces the composer's own nodes wholesale and clears the history. Save before
  generating over something you want.
- The generated world is roughly four times sparser per square metre than the authored Glowmere
  scene, for reasons partly diagnosed and recorded in [world-performance.md](world-performance.md).

---

## 9. The workspace: menu bar, dockspace, panels, status bar

Everything above is unchanged. What changed is the room it all happens in (ADR-076).

### What you see on launch

The window opens **maximised** on the display you last used, and the world fills the middle of it.
Around the world:

| Where | What | Open at first launch |
|---|---|---|
| Top | Menu bar — **File** and **View** | always |
| Left | **World Builder**, **Edit**, **Assets** | World Builder, Edit |
| Right | **World**, **Parameters**, **Render** | World, Parameters |
| Bottom | **Control**, **Analysis**, **Modulation**, **Graph** | Control, Analysis, Modulation |
| Foot | Status bar | always |
| Centre | **the canvas** — the world | — |

Panels sharing a region share a dock node, so they arrive as tabs. Build on the left, inspect on the
right, run underneath, and the world in the middle takes about 60% of the width and 74% of the
height.

**The canvas is its own rectangle, not the window with panels laid over it.** The frame is rendered
at the canvas's size and shown inside it; the rest of the window is opaque editor chrome with no
world behind it. Nothing else can be docked into the centre — no tab bar, no drop target — because a
panel there would cover the thing you came to look at.

`--size <w>x<h>` still does exactly what it says and suppresses the maximising, because that flag is
how a screenshot or a bug report is made reproducible. True fullscreen and the multi-display output
windows are unaffected.

### Showing and hiding panels

**View** lists every panel, grouped by the region it belongs to, with a line of description on
hover. A tick is a panel that is open; a panel's own close box does the same thing.

A panel you open for the first time drops into its own region rather than floating over the world.
One you deliberately drag out to float stays floating — it has a saved position of its own by then,
and the shell does not overrule it.

### The layout is remembered

Two files, in avgen's preferences directory beside `recent.json`:

- `editor-layout.ini` — ImGui's own: the dock tree, the split ratios, the tab order and every
  window's size and position.
- `editor-layout.json` — avgen's own: which panels are open, and the dock nodes the default layout
  built.

Both are written as you go and again on exit. Move a splitter, tear a panel off, close three of
them — it comes back that way.

**View → Restore Default Layout** puts it back: the dock tree is rebuilt on the spot and the
panels this editor ships with are reopened. No restart. Use it after a layout gets away from you, or
after an update adds a panel your saved tree has never seen.

**View → Save Layout Now** writes both files immediately, for when you are about to do something to
the machine that the ordinary two-second write might not survive.

### The status bar

One line across the foot. Every number on it is measured:

`fps · frame interval · CPU frame time · GPU frame time · resolution · draw calls · triangles ·
selection · what the next click does · the last edit · status message`, with the GPU adapter on the
right.

The GPU time reads `gpu n/a` when the timer has not reported yet, rather than showing a zero.

### The viewport

Every gesture in section 4 works exactly as it did, including the rule that a drag begun on the
canvas keeps the mouse until you release even if the cursor crosses a panel.

Three things now follow the canvas rather than the window, and the status bar's resolution is the
quickest way to see it:

- **What is rendered.** The frame is exactly the canvas's size in pixels. Widen a panel and the
  render gets smaller and cheaper; the status bar's `w x h` changes with it.
- **The camera's aspect.** It comes from the canvas, so the picture is never stretched by the panels
  around it. What you frame is what the canvas shows.
- **Where a click lands.** Picking and placement are measured from the canvas's own top-left corner,
  not the window's.

Resizing a panel stretches the picture for a single frame — the canvas's new size is only known once
the frame it appears in has been laid out. You will see it while dragging a splitter and never
otherwise.

An offline render is still framed by the project's own render settings, not by the canvas. A canvas
at an unusual shape and a 16:9 render will not frame identically; check against a render when it
matters.
