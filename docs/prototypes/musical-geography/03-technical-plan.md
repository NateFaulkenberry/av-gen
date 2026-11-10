# Musical Geography: technical plan (Phase 1, engineering)

Engineering agent, 2026-11-08, branch `scene/musical-geography`. This plan covers the brief's §2, as amended by
`01-coordination.md`. It stops at Gate 1. Phases 2 and later are proposals for the owner to accept or change.

**In one paragraph.** The square-terrain frame (`IMG_4270.PNG`) was never terrain. It is the navigation grid's debug
overlay, a set of 2D ImGui quads drawn over Glowmere. Its useful parts survive: the square cell as the unit of walking,
and the cell flags (walkable, water, steep, blocked). Its rendering is thrown away. The engine already has nearly
everything the scene needs:
- instanced grids with GPU per-instance field effectors and GPU culling;
- CPU/GPU-parity fields, including per-element delayed audio and onset rings;
- node-graph material programs;
- a pure-function day/night controller;
- jump arcs, A* and skeletal characters;
- a terrain-following, banking flight camera;
- the live profiler and the Critic.

What is new is specific:
1. **one tile-terrain model**, the same function on the CPU and in WGSL, that both rendering and walking read;
2. **a camera-anchored lattice**, so the world never ends;
3. **closed-form landing springs**;
4. **a ground seam**, so navigation can stand on a moving surface;
5. **a generic version of the Strange Attractor flight builder**;
6. after the spike: **a tile fast path, a far field and an underlay** (§6.1).

**The spike changes one assumption** (§6). On the existing generic procedural path, instancing *is* a risk:
- each visible tile costs about **0.2 µs of GPU** for scene plus depth;
- the shadow views draw every tile, culled or not.

At 1080p, 65k slabs cost 27 ms of GPU with a shadowed sun, and 360k visible slabs cost 70 ms without one. So Phase 2
opens with a measured **tile fast path** and a **far field** (§6.1), with a go/no-go number. The other risks are
aliasing at grazing angles, the sky showing through the gaps between thin slabs, and keeping "looks like terrain"
ahead of "looks like a grid".

## 0. The owner's art decisions of 2026-11-08, and what they change here

These came through the coordinator during Phase 1. They override the brief, and the art direction's first draft
(`04-art-direction.md` at c0e936d1), wherever they disagree.

| Decision | What it changes in this plan |
|---|---|
| **Thin slabs everywhere, no columns** | A tile is a thin box (`thickness`, a parameter) whose top sits at `H`, and nothing goes down to a floor. This adds a constraint: on a slope, a neighbour step larger than the slab's thickness opens a see-through crack under the higher tile. Smooth rolling land keeps the steps small, and a test enforces it (the 99th-percentile neighbour step ≤ `thickness` over the windowed world, §4.1). Side shading runs over the slab's normalised local height. Over water, the slabs float visibly by construction, which is what the landing response wants. |
| **Rolling hills are the main landform; no canyon walls, pillars, mesas or cliffs** | The formation set shrinks to sea, shore and shallows, rolling hills (dominant), gentle flat crowns (the characters' "pads", from a flatness mask, not terraces with walls), and the path. The canyon, mesa, terrace-cliff and strata terms are **dropped** from TileTerrain, and so are their channels and palette keys. A slope ceiling (a maximum neighbour step) is a generator parameter, with a test, so no cliff can appear by accident. |
| **Sharp tile edges, no chamfer** | `bevel = 0`, which is the procedural box's default: no bevel segments, and fewer triangles. Edge light becomes a band on the **top face** along its border, computed from `localPosition` (request 1970). |
| **Characters: the Glowmere aliens** (`assets/aliens/*.glb`) | **Commercial assets, never committed, cached or uploaded.** The clips that matter are `Idle`, `Walking`, `Walking_low_grav`, `Running`, `Jumping`, `Jump_running`, `Fall_loop`, `Landing` and `Floating`. `Floating` and `Walking_low_grav` give the buoyant idle and gait with no new animation. Phase 6 plans navigation, jumping and the slab landing around them (§4.5, §4.6). Known: `Landing` carries −0.567 m of root drop that is ignored (ADR-337). |
| **A short top-down camera pass is allowed** | An owner-approved exception to "look where it travels". `scene::Flight` gets a leg type whose aim pitches to the nadir and back, with screen-up taken from the travel direction, so there is no gimbal flip at −90° (§4.7). |
| **Daylight brighter and more saturated** | Art's job. The engine side is unchanged: DayNight terrain palette keys interpolated in OKLab. |

---

## 1. The original experiment: where it lives and what it was

| | |
|---|---|
| **What it is** | The navigation grid overlay. `World panel > Debug > Navigation > Grid` draws every `NavGrid` cell within a radius of the camera's aim point as a projected quad, coloured walkable / water / steep / blocked / edge or by region, with shore and vista points as rings and the selected walker's destination as a yellow ring. The HUD label `wanderer \| move in world space` is the overlay's selection label. |
| **Commit** | `95712f9c` "The navigation layer gets an appearance", merged in `57453f66` on 2026-09-14 at 18:57. The reference PNG is dated 18:58 the same evening. Decision record: `docs/decisions/ADR-197-the-navigation-layer-gets-an-appearance.md` (committed as ADR-194, renumbered on merge). |
| **Code** | Overlay: `src/ui/viewport_overlay.cpp`, `src/ui/world_editor.{hpp,cpp}` (`navStatus`, cell collection), `src/ui/world_panel.cpp`. The grid: `src/entity/nav_grid.{hpp,cpp}` (ADR-093, ADR-195/196, ADR-295/296). The headless rasteriser that very probably made the dark frame: `tools/overlay_shot.cpp`. |
| **How the squares are generated** | `NavGrid::build(navigator, cellSize = 4 m)` samples `Navigator::sample` once per cell centre over the world's bounds: `NavCell{ground, flags, slope, obstruction, wade, vault}`, 12 bytes. The overlay projects the four corners of each cell at `ground` height and fills a quad, with a gap. The visible elevation differences in the frame are the Glowmere heightfield, seen through the cells. |
| **Instancing?** | No. These are CPU ImGui draw-list quads with no lighting and no depth, and the cost is linear in the number of cells. Measured in the commit: 7,856 cells for 1.24 ms CPU, capped at 12,000 cells. It cannot be the renderer for this scene. |
| **Audio** | None. The overlay and the grid take no modulation. |

**Keep:**
- the idea that a tile is a navigation cell, which now becomes literal (§4.6);
- A* with deterministic tie-breaks, the cell flags vocabulary, region labelling and `PathStatus`;
- the overlay itself, as the debug view of the new tile navigation.

**Replace:**
- the 2D quad drawing, with GPU instancing;
- the baked static `ground`, with the tile-terrain function;
- the 4 m cell, with the tile pitch;
- the muted debug palette (the art agent owns colour).

## 2. What exists, and the verdict on each

| Area | What is there | Verdict |
|---|---|---|
| **Instancing** | `procedural` node, `distribution.grid` (`gridCount`, `gridSpacing`), beveled box source (`bevel`, `bevelSegments`). 96-byte `InstanceRecord`. GPU effector compute pass (`shaders/points.wgsl`, `docs/gpu-fields.md`): per-instance `positionOffset / scale / color / emission` driven by fields. GPU frustum/distance cull and LOD with indirect draws (`shaders/cull.wgsl`, ADR-029). RD Pins (`examples/rd-pins`) is the precedent: a 190x190 grid of boxes moved by fields. | **Reuse, with a fast path.** This is the tile renderer, and it is in the lit pass, so tiles get shadows, AO, SSR, fog and bloom for free. The spike measured its generic vertex path at about 0.2 µs per visible tile, so it needs a slim path for rigid slabs and the cull in the shadow views (§6). Extend it with the camera-anchored lattice and lattice-keyed randoms (§4.2). |
| **Terrain query** | `world::TerrainQuery` (ADR-090) joins `WorldMap` and `ClearanceField`: `at(p)` gives height, slope, normal, water and the reject reason, under `WalkRules`. `Navigator` wraps it. `WorldMap` is an analytic noise world and has no time. | **Extend with a seam** (§4.6). TerrainQuery gets its ground from an interface, so the tile terrain can answer for a time `t`. Its rules and vocabulary stay. |
| **GPU deformation / fields** | `FieldKind`: Noise, Voronoi, Wave (planar/spherical, travelling), Compound, Grid (simulated), **Spectrum** (per-element delayed band energy, ADR-1116), **Onset** (rings travelling outward from a point at `audioSpeed`), Resonance (ADR-1400). The CPU `spatial::sampleScalar` and the WGSL are held to 1e-4 by `test_fields_gpu`. The CPU reference for the whole effector stack is `spatial::applyEffectorsToRecords`. Limits: 16 fields per scene, 8 effectors per object. | **Reuse, and add one field kind** (`geography`, §4.1) for the formations. Wave interference, audio bands and transient rings are already expressible as fields today. |
| **Characters / animation** | GPU skinning (ADR-086: 8 skinned characters for 2.03 ms GPU and 29 µs CPU). `Gait` / `locomotion` states Idle, Walk, Run, Jump, Fall, Land. `entity/airborne.hpp`: `JumpArc`, `planJump`, and `checkArc(arc, groundAt)`, which already takes a ground *function*. `explore` behaviour has an audio-triggered hop and a gap jump. Assets: Quaternius `Mannequin_F` with UAL1/UAL2 (CC0; `Jump_Start/Loop/Land`, `Walk_Loop`, idles, `NinjaJump_*`); six aliens with `Jumping / Landing / Fall_loop`. | **Reuse.** Add a tile-to-tile jump-target picker. A neutral mannequin is the likely body; the art agent decides the silhouette and material. |
| **Navigation** | `NavGrid` A*, regions and `PathStatus`. It bakes a static `ground` (about 170 ms build on Glowmere) and self-checks against the world (`vouches`, ADR-295). | **Extend** (§4.6). A grid at one cell per tile over a moving window, with walk/jump/block edges from step heights. |
| **Springs and ripples** | `bounce_effect.cpp`: the closed-form damped spring `H·e^(−ζωa)·sin(ω_d·a)` since a trigger. `ground_pulse_effect` / `wave_effect` (ADR-207/702): a radial ring from an entity. `TriggerClock` is a pure function of the event times. | **Reuse the maths** (§4.5). No physics engine is needed. |
| **Modulation** | Params, routes and processor chains (ADR-011). Timeline (ADR-018). Sources (LFO, envelope, noise, timeline). `response.low/mid/high/onset` (ADR-1510, 1723). Beat clock with `beat.source` (ADR-1780..1785; the drum classifier is cut). Stems and feeds: `stem.<key>.level/onset`, `assets.stems` (ADR-1520, 1580). Presets and preset morph. | **Reuse as-is.** Every new terrain number is a registered parameter, so routes reach it. No hard-coded audio mapping. |
| **Camera** | Spline camera (`camera/mode 2`, `splineBank` ADR-1166, `lookAhead`). `CameraRig` follow and clearance (ADR-245, 911). Journey (ADR-1042), with no bank. The best motion is Strange Attractor's `buildFlight` (ADR-1853/1854, `src/scene/attractor_world.cpp`): a path precomputed at 30 Hz and lifted over the ground with a cone envelope (fall 45 m/s, climb 24 m/s), aim 2.5 s ahead along the smoothed path, tanh-limited bank ≤ 11.5°, seekable. It is coupled to the attractor's gorge. | **Lift it into a generic flight builder** over any `heightAt` (§4.7). The Auto-director (`camera_director`) is built on cuts and does not fit. |
| **Environment / palette** | `DayNight` (ADR-343, `src/scene/day_night.hpp`): one `dayPhase`, a pure function of time, drives sun direction/colour/intensity, moon, sky zenith/horizon/ground, haze, fog colour and density, and stars, through circular smoothstep keyframes. `env/dayNight/dayPhase` is a parameter (pause it and key it on the timeline). Looks (ADR-1886) are discrete snapshots. | **Extend.** Add terrain palette curves to the same phase (§4.4). The palette then runs on the timeline, independent of the audio routes, which act on finals afterwards. |
| **Materials** | Material programs (ADR-030, `rpPin` in RD Pins): a per-fragment op graph with `localPosition`, `worldPosition`, `time`, `field` reads, `gradient`, cosine `palette`, `remap`, `power`. Instance colour and emissive come from effectors. Refraction (ADR-1603) exists for any glass regions. | **Reuse.** Edge lines, height gradients, crest light and path tint are all expressible in JSON. Add `geography` field channels so a program can read crest, path and formation masks. |
| **Post** | Bloom, halation, grade (contrast, saturation, temperature, hue), DOF (physical or not), tilt-shift, anamorphic, outline post, exposure metering, tonemap. | **Reuse.** Tilt-shift and DOF answer the reference's shallow focus. The art agent owns post. |
| **Evaluation** | `avgen --render --range`, `--live-profile` (headless/live, `--json`, `--trace`, `--capture`), `tools/live_perf_baseline.py` (1080p30 T1–T4), `tools/contact_sheet.py`, `tools/camera_stability.py`, `tools/live_scene_profile.py`. The Critic through `AVGEN_CRITIC` / `directing_evaluate.cpp`. | **Reuse as-is.** |

The Environment seam (ADR-1200) was considered and rejected for the tiles. It draws after the lit pass, so tiles there
would lose shadows, AO and character shadows, and would need a second lighting model.

## 3. Proposed scene architecture

```
scene file (art agent)                       engine (this agent)
-----------------------                      ---------------------------------------------
"geography": { lattice, formations,   --->   world::TileTerrain  (pure C++; height(i,j,t), masks)
  waves[], breathing, rolling,               shaders/geography.wgsl (the same function, parity-tested)
  springs, palette curves }                  FieldKind::Geography (channel = height | base | crest |
fields: geography channels, Spectrum,          water | path | canyon | plateau | landing)
  Onset, Noise ...                    --->   procedural "tiles": grid, anchor = camera, lattice-keyed
procedural "tiles": box + bevel,               randoms, effectors (height, emission, colour), cull + LOD
  effectors, materialProgram                 material program (edges, gradients, crest light)
dayNight curves incl. terrain palette --->   DayNight resolve -> sky, sun, fog, terrain palette params
camera: flight { route, clearance,    --->   scene::Flight (generic buildFlight) -> seekable pose + bank
  altitude bands, lookAhead }
entities: walkers, behaviour "hop"    --->   world::GroundSource seam -> TerrainQuery -> Navigator ->
                                             TileNav (A* per tile, step/jump edges), planJump/checkArc,
                                             landing events -> TileTerrain springs (CPU and GPU)
routes / timeline / stems             --->   parameters on all of the above (no hard-coded mapping)
```

There is **one surface**. The height of tile `(i,j)` at time `t` is:

`H(i,j,t) = base(i,j) + dyn(i,j,t) + extra(i,j,t)`

- `base(i,j)`: the formations.
- `dyn(i,j,t)`: waves, breathing, rolling, and the radial and landing events.
- `extra(i,j,t)`: whatever other field effectors the art agent stacks on the height.

The renderer evaluates it in the effector pass. Every CPU consumer (characters, the camera's clearance, tests)
evaluates it with `TileTerrain::heightAt(i,j,t)`. That call runs the same effector stack on one record through the
existing CPU reference, `applyEffectorsToRecords`, so any field the art agent adds is in the walking surface too, with
nothing kept in step by hand.

Tops are flat. Height is per tile and constant over the tile, which gives the stepped geometric read. Deformers that bend
vertices (sine, noise) are not used on the tiles: they would tilt the tops and break the "tile = cell" query.

## 4. New functionality required (engineering)

### 4.1 `world::TileTerrain` and `FieldKind::Geography` (Phase 2)
- **The lattice.** Pitch `spacing`, tile footprint `tileSize` (the gap is `spacing − tileSize`), the slab
  `thickness`, and a stable integer index `(i,j)` in world space. A position always sits exactly at `(i,j)·spacing`:
  no deformation moves a tile horizontally (brief §4). Tiles are **thin slabs with sharp edges** (box, `bevel = 0`;
  §0).
- **Formations** are coherent functions with masks, not per-tile noise. Rolling land is the landform (§0):
  - **sea:** a broad low region, flattened, carrying the wave trains (§4.3);
  - **shore and shallows:** a distance-to-shore channel, a gentle ramp with no cliff;
  - **rolling hills, dominant:** domain-warped low-octave fBm at long wavelengths, with authored warp and octave
    count, and a slope ceiling so no neighbour step exceeds `maxStep`;
  - **crowns (pads):** where a hill top is broad, a flatness mask eases it level, so the characters can gather there.
    These are smooth flats, not mesas, and have no walls;
  - **path:** a spline band with a small lift, and a mask channel for colour and light.

  The canyon, mesa, terrace-cliff and strata terms of the first draft are dropped (§0). A macro "region" field varies
  the hills' parameters along the flight (wavelength, height, warp, and the sea's wave directions), so the endless
  world does not repeat. The art agent asks for it to be route-aware, which the flight's named targets cover (request
  1979).
- **The field kind.** `geography`, with `channel ∈ {height, base, crest, water, shore, hill, flat, path, landing}`, so
  effectors, the emissive field and material programs read the terrain with no new consumer code. The GPU data is a
  small uniform block (formation parameters, up to 4 wave sets, up to 32 events). It sits beside the existing field
  block, and each channel takes one of the 16 field slots.
- **Tests:**
  - determinism;
  - uniform pitch;
  - no overlap (`tileSize ≤ spacing`, and positions exactly on the lattice);
  - finite values;
  - neighbour coherence and **no cracks**: the 99th-percentile neighbour step, over a windowed world with the dynamic
    terms at their bound, is ≤ `thickness`, and the maximum is ≤ `maxStep` (no cliffs);
  - CPU↔GPU parity at 1e-4 (the `test_fields_gpu` pattern);
  - `heightAt` agrees with the rendered record (read back with `readInstanceRecords`).

### 4.2 The camera-anchored lattice (Phase 2)
- **The anchor.** A `grid` distribution gains `anchor: "camera"`. Each frame the object's translation snaps to
  `round(cameraXZ / spacing) · spacing`. Tiles therefore never slide: the window recentres by whole tiles, and the ring
  that enters or leaves is beyond the fog.
- **Lattice-keyed randoms.** Per-instance randoms are today hashed from the record index (`PointCloud::random`), so a
  tile's colour would change every time the window moved. The randoms must be hashed from the world lattice index in
  the effector pass.
- **Float precision.** Positions are camera-relative inside the window, and the world index is an integer, so float
  precision holds over tens of kilometres. There is a test for this at 20 km.
- **LOD and pop.** The far ring takes LOD 1 (the same sharp slab, or its top quad alone). Fog and atmospheric perspective hide the window's
  edge, and a test checks that the window radius exceeds the distance at which fog reaches 0.98.

### 4.3 Dynamic terrain (Phase 3)
Each behaviour is a parameterised term inside `dyn`, and each is a pure function of `t` and the event lists, so seeking
equals playing (ADR-091):
- **travelling waves:** up to 4 sets (planar or circular; wavelength, speed, direction, amplitude, crest sharpness), and
  they interfere by summing;
- **breathing:** a very-low-frequency regional scale;
- **rolling formations:** the hills' domain advected slowly along a direction;
- **frequency bands:**
  - low → broad amplitude;
  - mid → rolling and secondary amplitude;
  - high → small per-tile jitter amplitude and edge light.

  Each is a route onto a registered parameter. Per-tile delayed band energy is an existing `spectrum` field, if the art
  agent wants a band to travel across the ground.
- **transients:** the existing `onset` field gives rings from a point at a speed. A new **event ring** in TileTerrain
  places radial disturbances at chosen world positions (ahead of the camera, or on a feature), triggered by
  `response.onset` or `stem.<key>.onset` routes. Time since the trigger is closed-form, and events older than the decay
  are dropped.
- **the crest channel:** the normalised wave crest, for the "wavefront of illumination following a crest" (brief §6).
  The material and emission read it.

**Tests:**
- continuity in `t` (a bounded derivative);
- additive composition;
- radial decay below ε after N decay times;
- seek equals play over a scrub;
- no NaN under extreme route values (a clamp on every parameter's hard range).

### 4.4 The environment controller (Phase 4, built for the art agent)
- **Terrain palette curves** (request 1972, without its wall and strata keys). Extend `DayNightSettings` with:
  - a 5-stop land ramp with its own height stops, from **absolute** height and never renormalised;
  - a 4-stop water ramp, by wave height;
  - the shallows colour and distance;
  - the path and pad colours and gains;
  - the drift colour and amount;
  - the edge colour and gain, and the crest gain;
  - the emissive gain;
  - a fog density multiplier and an exposure offset.

  `resolveDayNight` already gives sky, sun, fog and haze from one phase. The four palette states (dawn, day, sunset,
  night) are keys at four phases.
- **Driving the phase.** `dayPhase` is paused and keyed on the timeline, so a minutes-long sunset-to-night is one
  track. It is independent of audio by construction: routes act on finals after the timeline.
- **Interpolation.** It is linear-RGB today. Add an opt-in **OKLab** interpolation for the colour curves, so the
  in-between states do not go muddy (the brief: "do not simply swap presets").
- **Tests:** interpolation at and between keys, the ring seam, every resolved value within range, and audio routes on
  top producing no invalid values.

### 4.5 Landing springs (Phase 6, with the infrastructure in Phase 3)
- **The event.** A landing is an event `(tileIndex, t0, strength)` in TileTerrain's event ring.
- **The response.** `dyn` adds the closed-form under-damped spring:

  `−A·e^(−ζω(t−t0))·cos(ω_d(t−t0))`

  The tile goes down, rebounds, oscillates and settles. Neighbours get a delayed, attenuated copy: a radial falloff
  with delay `r/c`, which is the ripple. Because tiles are thin slabs (§0), the depressed tile drops visibly below its
  neighbours, with no column to hide it. The neighbour falloff keeps the sprung step inside the no-crack bound, or it
  is allowed to open briefly by design; that is the art agent's call, and it is a parameter (`spring/depth`).
- **The trigger.** The trigger is the alien's `Landing` state entry, at the moment its feet reach the tile top, not
  at the end of the arc's maths. That is when the landing reads, and the clip's ignored root drop (ADR-337) would
  otherwise put the impulse a few frames early.
- **One function, two readers.** The same function runs on the CPU and the GPU, so a character standing on a sprung
  tile rides it.
- **Tests:**
  - settles below ε by a computed time;
  - the first extremum is downward;
  - neighbour amplitude is below the source;
  - energy is non-increasing after the first period.

### 4.6 The ground seam and tile navigation (Phase 6)
- **The seam.** `world::GroundSource`, an interface (height, normal and water at a point, at the current sim time),
  for `TerrainQuery`. Adapters:
  - `WorldMap` (no behaviour change; the existing suites prove it);
  - `TileTerrain`.

  Tile normals are flat. Slope is replaced by the **step** between neighbours.
- **`TileNav`** reuses NavGrid's A*, its flags and `PathStatus`, at **one cell per tile**, over a window that
  recentres on the characters. A neighbour is:
  - walkable if `|Δh| ≤ stepHeight`;
  - jumpable if the drop or rise is within the jump envelope and `checkArc` clears it against `heightAt(·, t)`
    (with dynamic terrain, checked at the predicted landing time);
  - blocked otherwise.

  The A* cost runs over base heights plus a margin for the dynamic amplitude. The replan is local, and the arc is
  re-validated at take-off, so "terrain changes during movement" is handled.
  On rolling land almost every neighbour is walkable, so jumping is a **choice**, not a necessity. The `hop`
  behaviour jumps to a target 2–5 tiles ahead to make the slabs perform: across sea slabs riding the waves, and up
  onto a crown.
- **The `hop` behaviour** drives the Glowmere aliens (`assets/aliens/*.glb`, §0). It picks a tile-centre target
  (gathering on crowns, running the path, a pause to look). Gait and state come from the clips already there:
  - `Walking_low_grav` and `Running` for travel;
  - `Jumping` or `Jump_running`, then `Fall_loop`, then `Landing`, for the arc. The arc is `planJump` with a buoyant
    gravity scale and a high apex;
  - `Floating` and `Idle` at rest, with a small bob that is added to the grounded height.

  The scale is the art agent's call (it proposed about 0.8 of a tile). The body skinning and per-character cost are
  ADR-086's measured numbers, at 3–6 characters.
- **Tests:**
  - a reachable crown is found;
  - a jump over the envelope is rejected;
  - landing tiles are valid at the landing time;
  - grounding is stable on a sprung tile (no penetration, no jitter beyond ε);
  - queries see deformation.

### 4.7 The flight camera (Phase 5)
- **The builder.** `scene::Flight`, lifted from `attractor_world.cpp::buildFlight` with no attractor types:
  - an authored or generated route of control points;
  - altitude bands (low, terrain-following, high reveal);
  - the clearance lift: a cone envelope over `max(base + dynamic amplitude bound)` sampled along the path;
  - smoothing;
  - aim along the smoothed path ahead (it looks where it travels);
  - a tanh-limited bank on the turn rate;
  - a 30 Hz table, interpolated, so it is seekable.

  Strange Attractor then reads this same module (a refactor, no behaviour change, tested by its existing camera
  tests).
- **Named targets** (request 1979). Control points can name a feature rather than a coordinate: follow the path
  spline, cross the shore at a point, climb to N tiles over a crown. Each leg has a pitch offset and a focal-length
  curve (focal changes at most 1 mm/s).
- **The top-down leg** (owner exception, §0). A leg type whose aim eases from the look-ahead to the nadir and back
  over a few seconds, while the camera keeps travelling. Screen-up is the travel direction, so there is no roll flip
  at −90°. It is the only place the aim leaves the travel direction, and a test pins its duration and continuity.
- **Continuity.** No cuts: the module has no cut concept. The route closes or extends forever, so a long song never
  ends the world.
- **Tests:**
  - continuity of position, velocity and bank;
  - clearance ≥ the minimum at every sample against the dynamic bound;
  - forward·travel ≥ cos(max look-off), everywhere outside the top-down leg;
  - no orientation discontinuity through the nadir;
  - determinism.

### 4.8 Audio wiring (Phase 7)
- Default routes live in the project file, not in code.
- Feeds point `beat` and `levels` at stems when a song has them (`assets.stems`, ADR-1580).
- Tested on Feline Footwear, Trench and one more materially different track (Night Shift or Hyperborea), and never on
  All You Got.
- A quiet passage keeps moving: the autonomous terms have no audio factor.

## 5. Parameters the art agent gets without touching code

All of these are registered parameters (routable, presettable, timeline-keyable), and they are in the scene JSON:

| Group | Parameters |
|---|---|
| Lattice | `geography/spacing`, `tileSize` (gap), `thickness` (the slab), `windowRadius`, `lodDistance`. Sharp edges: the bevel stays 0 |
| Formations | hills: `wavelength`, `height`, `warp`, `octaves`, `roughness`, `maxStep`; crowns: `flatThreshold`, `flatten`; sea: `level`, `flatten`, shore `distance`/`ramp`; path: `route[]`, `width`, `lift`; region: `scale`, `variation` (how far the hill and sea parameters wander along the world); `seed` |
| Dynamics | per wave set (×4): `amplitude`, `wavelength`, `speed`, `direction`, `shape`, `crestSharpness`; `breath/amplitude, period, scale`; `roll/speed, direction`; `jitter/amplitude` (the high band); `events/strength, radius, speed, decay, placement` |
| Springs | `spring/stiffness (ω)`, `damping (ζ)`, `depth`, `neighbourRadius`, `neighbourDelay` |
| Surface (material program, JSON) | fields readable: `geography.height/base/crest/water/shore/hill/flat/path/landing`; per-tile `contour` side mask and `crest` weight (1970, 1971); inputs `localPosition` (top-edge band, side shading over the slab), `worldPosition` and the lattice index (1978); palette ops; per-instance roughness, emission and colour |
| Environment | the DayNight curves including the new terrain palette keys (1972); `env/dayNight/dayPhase`; OKLab toggle; fog, sky, sun (existing `env/*`, 1977) |
| Camera | `flight/route[]` with named targets, `altitudeBands`, per-leg pitch offset and focal curve, the top-down leg, `minClearance`, `lookAhead`, `bankGain`, `bankLimit`, `speed` profile |
| Characters | which aliens, count, scale, `jump/apex`, `gravityScale`, `stepHeight`, `idleFloat`, the clip per state, behaviour weights |
| Post | existing `post/*` (AgX is already the default tonemap; bloom threshold and intensity, DOF, grade, halation, outline) |

### 5.1 The art agent's engine requests (`04-art-direction.md` §10, c0e936d1), as folded in

The wall and strata items are dropped (§0). Everything else becomes part of a phase above:

| Request | Status | Where it lands |
|---|---|---|
| **1970** contour edges: per-side weights, lit where `floor(h/interval)` drops across a side; `contour/interval`, `contour/offset` | **Accepted**, adapted to sharp edges: the band is drawn on the top face's border, not a chamfer | Phase 3, in the effector pass (four neighbour heights per tile; cost measured and limited to the visible set) |
| **1971** crest weight per wave set, `crestSharpness`, brightening where trains cross | **Accepted** | Phase 3, the crest channel |
| **1972** wider terrain palette | **Accepted, minus** the canyon wall colour and the strata amplitude | Phase 4 (§4.4) |
| **1973** region channels for the material | **Accepted, reduced** to water, shore distance, flatness (pads), path. Terrace index, canyon wall and river are dropped with their formations | Phase 2 |
| **1974** per-instance roughness (water 0.16, land 0.62) | **Accepted.** Check whether a program can already write roughness from a field; if so, it is only a test | Phase 2 |
| **1975** top-only emission and side shading | **Accepted, for a slab**: emission masked to the top face; the side fades over the slab's normalised local height instead of a column's | Phase 2 |
| **1976** distance fade of the gap and edge band by pixel footprint | **Rewritten by request B** (02-, owner-reviewed): a separation that survives distance (a ≥1 px footprint-aware border and/or a far ring of 2×/4× tiles), not a fade | Phase 2 |
| **1977** fog: an exp²-like curve, height falloff, a sun-facing tint; plus output dither (request A) | **Mostly there already.** The single medium (ADR-705) has `horizonDensity` (density grows with distance, so the near ground stays clean), `fogHeightFalloff` and `fogHeightCurve`, and anisotropic sun scattering. The window radius is tied to the 0.98 distance by a test. Any gap found is fixed in Phase 4 | Phase 2 check, Phase 4 fix |
| **1978** lattice coordinates in material programs | **Accepted**, with the lattice-keyed randoms (§4.2) | Phase 2 |
| **1979** flight route through named features, per-leg pitch and focal curves | **Accepted**, plus the top-down leg | Phase 5 (§4.7) |

The smaller asks need no engine work: plateau levels become crown flatness (the levels go with the mesas); the path
spline is exposed to the camera and the characters; AgX is already the default tonemap (ADR-039); and the bloom
threshold and intensity are already `post/*` parameters that DayNight's phase can key.

The art agent asks for anything else in `02-requests.md`.

## 6. The spike: tile instancing at 1080p (throwaway)

**What it is.** A generated scene: an `N×N` grid of thin sharp slabs (0.9 × 0.15 × 0.9 at pitch 1) on the existing
procedural path, with a noise hill, two crossing travelling `wave` fields and a colour gradient, all as GPU effectors.
GPU cull is on, the camera is low and grazing toward the horizon (the worst case for this scene), and one
directional key light with 3 cascades is either on or off. It was measured with
`avgen --live-profile --size 1920x1080 --quality ultra` under `tools/gpu-lock.sh`, headless, on AC power on the M2
Max, with 300 measured frames per arm. The scripts are kept in `docs/prototypes/musical-geography/spike/`; the scenes
are not committed.

| Grid | Visible / culled tiles | Triangles | GPU p50 | scene + depth | shadow | effectors | CPU work |
|---|---|---|---|---|---|---|---|
| 256² (65k), shadow | 40,266 / 25,270 | 0.48 M | **27.4 ms** | 8.0 + 3.1 | **15.1** | 0.18 | 1.4 ms |
| 256², no shadow | 40,266 / 25,270 | 0.48 M | **12.0 ms** | 7.6 + 3.8 | — | 0.22 | 0.7 ms |
| 512² (262k), shadow | 160,570 / 101,574 | 1.93 M | 66.0 ms | 18.8 + 12.6 | 33.1 | 0.46 | 1.2 ms |
| 512², no shadow | 160,570 / 101,574 | 1.93 M | 32.9 ms | 18.0 + 13.9 | — | 0.51 | 0.9 ms |
| 768² (590k), shadow | 360,880 / 228,946 | 4.33 M | 109.5 ms | 40.1 + 27.8 | 39.5 | 0.93 | 1.5 ms |
| 768², no shadow | 360,880 / 228,946 | 4.33 M | 69.7 ms | 38.9 + 29.3 | — | 0.98 | 1.1 ms |

**What it proves:**
1. **GPU deformation is free.** The effector pass that sets every tile's height and colour costs 0.18–0.98 ms for
   65k–590k tiles, and the CPU does nothing per tile (1–1.5 ms CPU work in total). The "deformation on the GPU"
   architecture stands.
2. **The draw is the cost, and it scales with visible tiles.** Scene plus depth is about **0.19–0.29 µs per visible
   tile**, and the triangle count is not the limit: 4.3 M triangles is modest for this GPU. The tiles go through
   the generic procedural vertex path, which carries the deformer chain, the previous-frame clip position and the
   fiber hooks. A 12-triangle instance pays all of it. The depth prepass draws everything a second time (about 40% of
   the geometry cost).
3. **Shadows draw every tile, culled or not**, in all three cascades: 15 ms at 65k tiles. The GPU cull feeds the main
   view only.
4. **The current path's budget.** At about 20 ms for tiles inside a 33 ms frame, it affords about **70–90k visible
   tiles without shadows**. That is enough for the near and mid field at pitch 1 (a 256² window), not for a
   high-altitude reveal to the horizon.
5. **What the frames show** (`01-gate1/eng-spike-*.png`):
   - the gaps between thin slabs show **the sky through the ground**, everywhere: there is nothing under a slab;
   - **moiré** appears in the mid distance;
   - **the window's edge is plainly visible** where there is no fog. (The spike's fog key was a wrong name and was
     ignored, which made this a fair test of the edge, if by accident.)

   All three were predicted as risks. They are now seen.

### 6.1 What the spike changes in the plan (Phase 2 opens with this)
- **A tile fast path**, in the procedural renderer rather than as a new renderer. When an object is a lattice of
  rigid slabs (no deformers, the `anchor: camera` grid), it draws with a slim vertex program:
  - the record is read once;
  - no deformer chain, and no previous-frame position unless motion blur needs it;
  - only the three faces that can face the camera (the top and two sides), selected per instance;
  - the visible list from the cull.

  It goes through the same lit fragment path and material programs, so nothing changes for the art agent. Shadow
  views get the cull too (frustum per cascade plus `shadowCasterReach`, which exists already).

  **Go/no-go at the start of Phase 2:** ≤ 0.06 µs per visible tile for scene plus depth, and the 256² window under
  12 ms of GPU with shadows, measured with this spike's scenes. If the fast path misses, the fallback is a coarser
  pitch at the same screen density, plus the far field below.
- **A far field that stays tiles** (request B, folded into ADR-1976; owner-reviewed, 2026-11-08). The squares must
  read at reveal altitude: 70–100 tiles up, with a 35–50 mm lens, across all the land in frame. So the gap does
  **not** fade with distance, and the continuous far mesh first proposed here is dropped. Instead:
  - a footprint-aware dark border on each top, held at least about 1 px wide;
  - and/or a far ring of 2× and 4× tiles, in whole lattice blocks, each the mean of its children's heights, drawn
    through the same slab path, with fog over the seams.

  Which of the two is chosen is decided by measured cost and moiré on the high-reveal still.
- **An underlay for thin slabs.** A coarse continuous sheet of the same height function, offset below the slabs, closes the see-through gaps
  with a dark, coloured shadow tone (the art agent chooses the colour) instead of the sky. Owner's slabs, without the
  holes.
- **The depth prepass for tiles** is measured on and off once the fast path exists. The fog and SSR need linear depth,
  so it stays unless a cheaper source of depth for the tiles is proven.

## 7. Rendering and performance plan

**Budget at 1080p30 on the M2 Max:** 33.3 ms per frame, and the repo's live gates T1–T4 (`tools/live_perf_baseline.py`:
≤ 5 misses per 900 frames, interval p99 ≤ 40 ms, ≥ 90% of frames at Medium or better, headless Ultra GPU p50
≤ 29.3 ms). The scene aims for GPU p50 ≤ 20 ms at Ultra, to leave room for post, characters and art.

**Levers, in order** (from the spike):
1. the tile fast path (§6.1);
2. visible tile count: pitch × window radius, with the far field beyond it;
3. GPU cull in the shadow views, and `shadowCasterReach`;
4. shadow range and cascades (one directional, no other shadow casters);
5. the slab's side faces (a top-quad-only LOD far away);
6. render scale on the live ladder.

There is no transparency on the tiles except optional glass regions. Selective emission costs nothing extra; bloom is
already paid for.

**What is measured each checkpoint** (`--live-profile --json --trace`, through `tools/gpu-lock.sh`, on AC power):
- CPU frame time and GPU frame time (p50, p95, p99);
- pass breakdown: scene, depth, shadow, cull, effectors (the deformation cost), post;
- terrain rendering cost: scene, depth and shadow tile draws vs a tiles-off arm;
- character simulation cost and terrain-query cost (CPU timers, per frame and per query);
- memory: instance buffers and GPU resident;
- frame-time stability: interval p99, deadline misses per 900 frames, quality-level residency;
- the live mode (present, Fifo) as well as headless.

**The Windows GPU environment.** There is none documented: `docs/build.md` says Windows is "architecturally supported,
not built yet". It is reported as not available, and no numbers are invented for it.

## 8. Implementation phases and the owner's checkpoints after Gate 1

| Phase | Engineering builds | Art follows with | Checkpoint (owner) |
|---|---|---|---|
| 2 Terrain foundation | **First, the tile fast path and its go/no-go** (§6.1); then the far field and the underlay; TileTerrain and the geography field (sea, shore, rolling hills, crowns, path; lattice; parity; the no-crack test), camera-anchored lattice, lattice randoms, region channels, roughness, top-only emission, the reveal-altitude tile separation (1976 as rewritten), output dither (1977 A), (1973–1975, 1978), a first material program, a static debug camera; perf of the static world | formation layout, first palette, materials on stills | **CP-A "the ground"**: 6–8 stills (close, mid, high) of the static landscape in two palettes, plus a perf line. `02-terrain/` |
| 3 Dynamic terrain | waves, interference, breathing, rolling, the event ring, the crest channel and contour sides (1970, 1971), spring infrastructure, audio parameters; seek = play | crest light, emission language | (no stop; clips go to `03-dynamic/` for the record) |
| 4 Visual identity (art) | DayNight terrain palette curves and OKLab (1972), any fog gap (1977); any requests | the four palettes, edge light, selective illumination, post | |
| 5 Camera | `scene::Flight`, the attractor refactor, clearance against the dynamic bound, named targets and the top-down leg (1979) | the route's composition and altitude story | **CP-B "the flight"**: one continuous 90–120 s render on Feline Footwear, dynamic terrain, a palette transition, no characters; plus live T1–T4. `05-camera/` |
| 6 Characters | GroundSource seam, TileNav, the `hop` behaviour on the Glowmere aliens, buoyant jumps, slab landing springs and ripples | which aliens, scale and count (3–6) | **CP-C "inhabitants"**: a 30–45 s clip of aliens running, hopping across sea slabs and landing on crowns, with the slab spring visible. `06-characters/` |
| 7 Audio | project routes, stems feeds, three tracks | mapping taste | |
| 8 Evaluation and optimisation | full profile, optimisation, the tagged tests | Critic and cinematic evaluation | **CP-D "the piece"**: full renders on three tracks, the live profile, the Critic report, and the merge request. `07-audio/`, `08-evaluation/`, `10-full/` |

Four stops after Gate 1. CP-A is the important one: the brief says the landscape must already be compelling at the end
of Phase 2, and the camera and characters build on whatever ground it approves.

## 9. Testing strategy

- **TDD** for every new deterministic system, Catch2, under the tags `[geography]`, `[geography][gpu]`, `[flight]`,
  `[tilenav]` and `[daynight]`. The §4 bullets are the test list; together they cover the brief's §13.
- **CPU↔GPU parity** for the geography field and the spring (the `test_fields_gpu` pattern, 1e-4). Render agreement
  is checked by reading back instance records, not by comparing pixels.
- **Benchmarks** (`[performance]`, release build): `heightAt` per query, TileNav window rebuild, flight build, and the
  effector pass for N tiles.
- **Seams:** the `WorldMap` adapter behind `GroundSource` must leave every existing navigation test unchanged, and the
  attractor flight refactor must leave Strange Attractor's camera bit-identical.
- Per `01-`, the full suites are run only when the owner approves the merge; tagged tests are run as each piece lands.

## 10. Risks

1. **The instancing cost (the spike's finding, §6).** About 0.2 µs per visible tile on the generic path. The fast
   path's go/no-go is the first measurement of Phase 2. The fallback is a coarser pitch plus the far field, which
   costs density at the horizon, not the look up close.
2. **The grid reading as a grid.** Distant tiles are a regular pattern smaller than a pixel. That gives moiré and
   shimmer, and the "generic grid visualisation" look the brief forbids. Mitigations:
   - fade the gap and edge mask with screen-space tile size, so far tiles merge into a surface;
   - fog and atmospheric perspective;
   - LOD;
   - large-scale colour structure owned by the art agent.

   There is no TAA to lean on. CP-A must look at motion, not only stills.
3. **"Noise hills."** The brief rules out random noise as the source of elevation, and repetitive hills. The
   With rolling hills now the main landform (§0), this is the sharpest risk to the look: hills are exactly what noise
   makes. The defences are long, domain-warped wavelengths, the region field varying the hills along the route, the
   sea and shore as the contrast, the path and crowns as authored structure, and colour from absolute height. The art
   agent's layout decides it, and CP-A judges it.
4. **The anchor window and the pop line.** If fog is too thin for the art's clarity, the window edge shows. Mitigation:
   the window radius is a parameter, tied to the fog distance by a test. If a far reveal from the high-altitude camera
   needs more than the window, the far field (§6.1) carries the same surface to the fog line. The spike showed the edge
   plainly with no fog.
5. **Shadows.** Measured at 15 ms for 65k tiles in 3 cascades with no cull (§6). The cure is the cull in the shadow
   views and a short caster reach. Fallback: fewer cascades, or the far field casting no shadow.
6. **Characters on a moving floor.** Jitter and sinking when heights change under a planted foot. The single surface
   and the re-validated arcs address it. Known engine defects to step around: the alien `Landing` clip's root drop
   (ADR-337) and the heading-dependent slope lean.
7. **The 16-field cap.** The geography channels, the audio fields and the art's own fields share 16 slots. If the art
   needs more, the channels can pack into a vector or colour field. To be watched, not fixed early.
8. **Determinism with characters.** Behaviour state resets on seek (ADR-091), and the landing events derive from the
   simulation. Seek equals play needs the existing checkpoint re-simulation, so an offline render is checked against
   a live play.
9. **GPU contention.** Another agent renders on this GPU. Every measurement is taken under `tools/gpu-lock.sh`, and a
   number taken without it is not reported.

10. **Cracks between thin slabs.** With no columns (§0), a neighbour step larger than `thickness` opens a gap under
   the higher tile, and the sky or fog shows through it. Waves at full audio amplitude and a landing spring both
   enlarge steps. Defences: the generator's `maxStep` and slope ceiling, a test on the 99th-percentile step with the
   dynamic terms at their bound, and the hard ranges on the audio parameters. If the art agent wants thinner slabs
   than rolling land allows, the underlay (§6.1) sits beneath them, so a crack reads as a shadow rather than a
   hole.
11. **The aliens are commercial assets.** They must never be committed, cached in CI or uploaded (the repo is public;
    `assets/aliens/ATTRIBUTION.md`). The scene references them by path only, and the tests use a synthetic skinned
    body, so CI does not need them. A clean checkout without the aliens must still load the scene, and does so with
    no characters and a warning.

## 11. ADR numbers reserved

Engineering 1950–1969. The planned ones:
- 1950 TileTerrain, one surface (thin slabs, rolling land, the no-crack bound);
- 1951 the geography field kind;
- 1952 the camera-anchored lattice and lattice randoms;
- 1953 dynamic terms and the event ring;
- 1954 landing springs;
- 1955 the DayNight terrain palette and OKLab;
- 1956 `scene::Flight` (named targets and the top-down leg);
- 1957 the GroundSource seam;
- 1958 TileNav and `hop` on the aliens;
- 1959 the tile fast path and the shadow-view cull;
- 1960 the far field and the underlay.

The art agent's 1970–1979 are written by this agent when each request is built (§5.1), under the art agent's
numbers.

None is written yet: an ADR is written when its decision lands in code.

## 12. Phase 6 plan, revised 2026-11-09 (after CP-A, Phase 3 and Phase 5; not built until the owner reviews CP-B)

What changed since §4.5–4.6:
- **4 m tiles.** The aliens (about 1.8–2.2 m) now stand on a tile about two body-heights wide. A "hop" is a jump of
  one to three tiles (4–12 m), not 2–5 small tiles. This is buoyant, low-gravity jumping, so `JumpSettings::gravity`
  is cut to about 6 m/s² for a 1.2–1.8 s arc. The scale question is now settled by the tile, not by a 0.8 ratio.
- **The ground moves** (ADR-1953). Everything that stands on it must read the moving surface at its own time. That
  means `tileHeightAt(spec, dyn, pitch, x, z, t)`, which already exists and is parity-tested against the GPU.
- **The camera flies past, never at** (the owner's rule, ADR-1956). There is no hero framing. Characters are placed
  where the flight's route passes: on the path legs, on crowns under a top-down leg, and on sea slabs beside a shore
  crossing.

### 12.1 The ground seam (`world::GroundSource`)

`world::TerrainQuery` and `entity::Navigator` are built on `WorldMap` (height, normal, water and bounds from baked
chunks). The seam is a small interface with two implementations:
- one over `WorldMap`, with no behaviour change, proved by the existing navigation and character suites;
- `TileGround`: the height of the tile under a point at time t (`tileHeightAt` with the scene's dynamics, the live
  rings included), water from the tile's water channel, the normal flat (a tile is level), and bounds unbounded (the
  lattice is endless).

`TerrainQuery::at` takes slope from the normal. On tiles, slope is always 0, and the step between neighbours replaces
it in the navigator (§12.2). Without that change, a 2 m riser would read as walkable flat ground.

### 12.2 `TileNav`

NavGrid's A*, at one cell per tile, over a window of about 64 × 64 tiles that recentres on the characters. The flags
are the existing ones plus edge kinds:
- **walk** where |Δh| ≤ `stepHeight`, about 0.6 m;
- **jump** where the rise or drop sits within the jump envelope and `checkArc` clears it against the ground at the
  predicted landing time;
- **blocked** otherwise.

Costs use base heights plus a margin for the dynamic amplitude, the same bound the flight uses. The replan is local,
and the arc is re-validated at take-off. On rolling land almost every edge is a walk, so a jump is a choice the
behaviour makes.

### 12.3 The `hop` behaviour on the Glowmere aliens

Targets are tile centres:
- gathering on crowns (the flat channel);
- running the path (the path channel);
- crossing sea slabs (the water channel) where the waves are high;
- pausing to look.

The clips are the ones in the GLBs: `Walking_low_grav`, `Running`, `Jump_running`, then `Fall_loop`, then `Landing`,
then `Idle` or `Floating`.

The landing is triggered on `Landing` state entry, at the moment the feet reach the tile top, which is the moment the
spring is applied (§12.4). At rest, a small `Floating` bob is added to the grounded height.

There are 3–6 characters, with per-character cost taken from ADR-086's numbers. The aliens are commercial assets:
never committed, cached or uploaded. Tests use a stub skeleton.

### 12.4 The floating-platform response (landing springs)

A landing is an event in the moving ground: a new event kind beside the rings, `(tile centre, t0, strength)`. Its
response is the closed-form under-damped spring of §4.5, with a delayed, attenuated copy on the neighbours (the
ripple):

    h(t) = −A · e^(−ζω(t−t0)) · cos(ω_d (t−t0))

It is evaluated by the same `tileMotion` on the CPU and GPU, so a character standing on a sprung tile rides it.

Spring events come from characters, not from routes. They are recorded with their time like live rings, so seeking
drops the ones that haven't happened yet. A render plays forward, so it reproduces them; a scrub replays the
characters' deterministic behaviour.

The crest channel can take a spring's glow, so a landing lights the tile.

**Parameters:** `env/tiles/spring/{depth, frequency, damping, ripple, rippleSpeed}`, with `depth` the art agent's
call (§4.5).

### 12.5 Tests (TDD, `[geography]` and `[characters]`)

- **Springs:** the response settles below ε by a computed time; the first extremum is downward; the neighbour's
  amplitude is below the source's; energy is non-increasing after the first period; CPU/GPU parity.
- **Ground seam:**
  - the `WorldMap` adapter changes nothing (the existing suites, bit-identical);
  - `TileGround` agrees with the rendered records at time t.
- **TileNav:** a reachable crown is found; a jump over the envelope is rejected; landing tiles are valid at the landing
  time; a riser over `stepHeight` is never walked.
- **Characters:** grounding is stable on a sprung and waving tile (no penetration, no jitter beyond ε); seek equals
  play for positions over a 30 s scrub.

### 12.6 Order and CP-C

1. the seam, with the `WorldMap` adapter and its regression run;
2. `TileGround`, and springs in the moving ground (CPU, GPU, parity);
3. `TileNav`;
4. `hop`, with clips and landing events;
5. the characters placed along the flight's route;
6. **CP-C:** a 30–45 s clip of aliens running, hopping across sea slabs and landing on crowns, with the slab spring
   visible, plus a perf line with 3–6 characters.

**Budget:** the flight at 4 m tiles with the horizon fill and the far ring is 17–23 ms GPU median today. Characters
add skinning plus their shadows. The p99 tail is §5's work, in hand.

**Risks:**
- grounding jitter on fast waves (mitigated by sampling at the character's own time with a 2-frame-smoothed height);
- clips that don't match buoyant arcs (time-scale the fall loop);
- the camera passing too far from the characters to read them (route authoring).

## 13. Phase 8 engineering list (added 2026-11-10, during CP-D r2; not started)

Queued by the coordinator after the ADR-1991 feet fix. Do these in Phase 8, not before.

1. **Measure the CPU cost of the per-vertex lift** (`Composition::groundCharactersOnTiles`, ADR-1991): three aliens,
   about a third of each mesh skinned on the CPU every frame. Report it at 60 fps live and offline, and cut it if it
   matters (e.g. the bind-pose low-vertex set thinned, or the lift computed only for rigs in view).
2. **The hop simulation depends on the frame rate.** A 2 fps render started at 140 s puts alien0 somewhere else at
   142 s than a 30 fps render does; 30 fps renders started at 140, 141 and 142 s agree, so seek is not the issue, the
   step size is. Live runs at a variable frame rate and can diverge from the offline render. Find out whether the hop
   (or the entity world) needs a fixed step, and whether the divergence is visible: landing positions and times
   against their cues, at 30, 60 and a jittered live rate.
