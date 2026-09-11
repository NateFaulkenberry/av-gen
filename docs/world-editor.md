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
| **Left click on the sky** | Deselect |

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

## 5. Placing things by hand

**World Builder → Place assets.**

Filter and pick an asset from the library list to **arm** it. While something is armed, clicking in
the viewport places rather than selects; press **Stop** to go back to selecting. "None" is the resting
state on purpose — a viewport that places something every time you click is one you cannot look
around in.

### Modes

| Mode | What a click makes |
|---|---|
| **Single** | One object where you clicked. |
| **Brush** | A scatter filling the brush radius, spaced apart. Set **Radius** and **Spacing**. |
| **Cluster** | A few objects tight together, as one thing that grew there. Set **Count** and **Spread**. |
| **Landmark** | One, deliberately enormous. Set **Times normal size**. |

Shared settings: **Scale jitter**, **Yaw jitter**, **Sink** (metres pushed into the ground), and **Lie
along the slope** (stand along the surface normal rather than upright — right for rocks and fallen
logs, wrong for anything that grows toward the sky).

Three things worth knowing about how these behave:

- A **brush throws darts** against the spacing radius rather than scattering uniformly, because a
  uniform scatter over a disc clumps — and you are already deciding where the density goes by moving
  the mouse.
- A **cluster** fills its disc evenly rather than bunching at the centre, so it reads as a patch of
  something growing rather than as a target.
- **Brush and cluster lay out on the surface**, using the picked normal. A stroke on a 45° hillside
  lands on the hillside instead of putting half of itself underground.

Every placement is deterministic given its seed, and placed objects are ordinary glTF nodes — select,
move, rename and delete them like anything else.

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
- **There is no undo.** Save before generating over something you want.
- The generated world is roughly four times sparser per square metre than the authored Glowmere
  scene, for reasons partly diagnosed and recorded in [world-performance.md](world-performance.md).
