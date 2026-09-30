# Liminal Euclidean World: engineering research and feasibility review

*Engineering agent, 2026-09-30, 18:25-19:10, from the code at `ea3f436a` (main `31a1e42e` plus the brief and the
owner's addendum). This covers the procedural-graphics part of §14, the feasibility review of §1 and §17, and the
design the infrastructure follows. The art research, the song analysis and the director plan are the art agent's
(ART-RESEARCH, SONG-ANALYSIS, DIRECTOR-PLAN). The addendum (`01-addendum-emotion.md`) governs where it differs: the
music video and LIVE are separate deliverables, the camera is a wanderer that may stop, repetition is a first-class
tool, and "if a geometrically impressive effect does not improve the emotional communication, do not use it".*

## Verdict

**Feasible tonight, on the existing SDF path, with five small generic additions and no new renderer.** The Procedural
Space POC left a working implicit-architecture pipeline: SDF trees as scene data, compiled to WGSL, ray-marched with
depth, lights, fog, AO and edge light, every node a named routable parameter, offline renders bit-identical. What it
does not have is exactly what the owner complained about and what this brief is about:

| the brief needs | the POC has | gap |
|---|---|---|
| continuous evolution (§4) | `morph` driven by **scene states** (forward-only, bar-quantised switches) and **sample-and-hold** `random` sources: the "stuck between states and rapidly switching" architecture | no spring response, no rate integration, no domain warp |
| a journey (§3, addendum "wanderer") | a still camera; composition camera modes orbit / free / spline | no camera that walks an infinite world, stops, hesitates and looks around |
| liminal architecture (§2) | boxes, booleans, repeat, fold, recurse | no stairs; no way to chain a room, a stair and a landing into a world that continues (the repeat is per-axis only) |
| impossible continuity, repetition (§3, addendum) | infinite axis repeat | no "climb forever", no Penrose loop, no invisible wrap for a travelling camera |
| a palette (§7) | colours as separate parameters, moved by scene states | no palette states, no perceptual interpolation, no saturation/value control |
| objects in the world (§8) | depth composition already works (REPORT §5) | nothing places a mesh relative to a repeating, travelling world |

None of these needs a new subsystem. Each is a node kind, a chain stage, a camera mode or a project block in the file
that already owns that concern, which is the POC's own rule (ADR-1000).

## 1. What the existing system can already do toward the brief

- **Plain architecture as data.** `box`, `roundedBox`, `difference` (doorways cut from walls), `union`, `repeat`
  (finite or infinite, per axis), `mirror`, `fold`, `translate`/`rotate`. A room is `difference(box, box, doorway...)`;
  a corridor is a room stretched and repeated. Named nodes make every dimension a parameter
  (`sdf/world/node/hallWidth/size`).
- **Compiled marching** (ADR-1003) at 8-13 ms for a hall-sized tree at 960x540, 20-34 ms at 1080p; structure changes
  recompile in about 80 ms, parameter changes are free.
- **Fog** with one law (ADR-705) and horizon density, the volumetric march, bloom, grade: large empty spaces fading to
  a void are a scene setting, not work.
- **Determinism and seeks.** Offline renders are bit-identical (ADR-091). A seek replays the audio bus and every
  route whose source is a function of time, carrying chain state in checkpoints (ADR-870, ADR-901). Timeline tracks
  are pure functions of the clock. **So anything I put inside a route chain, or make a pure function of time, is
  seek-exact for free.** This decides where the continuous-evolution state lives.
- **Audio features** on the bus (`audio.bass/lowMid/mid/highMid/treble/energy/onset`, `beat.*`, `music.*`, `section.*`)
  and the Sonic character (`sonic.*`, ADR-1020) when a project asks for it. Routes carry them into any parameter
  with gain, curve, threshold, attack/decay, envelopes, delay and depth.
- **Depth composition** of meshes, particles and characters with the SDF (REPORT §5): the hybrid is already the
  architecture.

## 2. What is missing, and what I build

In priority order (the coordinator's a-f), with the decision records they get:

| # | addition | where | ADR |
|---|---|---|---|
| a | **`stairs` primitive, `screw` repeat, `warp` domain op** | `src/spatial/sdf.*`, `shaders/sdf.wgsl`, the WGSL compiler, `src/scene/sdf_object.cpp` | 1040 |
| a | **a vocabulary library** (room, corridor, doorway, stairway, landing, platform, cell) that emits named SDF JSON | `tools/liminal_sdf.py` | 1040 |
| b | **`spring` and `integrate` route-chain stages** | `src/params/processor.*`, serialisation | 1041 |
| c | **camera mode 3, the journey** | `src/scene/composition.cpp`, `src/scene/journey.*` | 1042 |
| d | **the project `palette` block** | `src/scene/palette.*`, the engine's frame, the project file | 1043 |
| e | **nodes anchored to the journey** (a hero or a figure that walks the path ahead of the camera) and a cell-placement helper for props | `src/scene/journey.*`, `tools/liminal_sdf.py` | 1042 |
| f | **`examples/liminal/`**, generated by `tools/make_liminal_example.py` | | |

## 3. Continuous deformation: which techniques, and why

The owner's requirement is "one continuously evolving world, not a sequence of visual states". Structurally, that
means **no parameter the viewer can see may be a step function of the music.** Every visible parameter is

```text
value(t) = authored trajectory(t)           (a pure function of time: timeline keys, LFO, noise)
         + spring( musical feature(t) )     (a damped second-order response, never a jump)
```

and anything that must *travel* (a camera, a flow through noise space) is the **integral** of a rate, so that the
music changes the speed of a change rather than its position.

Techniques considered, and the verdicts:

| technique | verdict | why |
|---|---|---|
| **Domain warping** (`p + A·noise(p·f + phase)`) | **adopt**, as the `warp` node | The one deformation that bends architecture without melting it. At low frequency (a wavelength of several rooms) walls stay planar locally and bow globally: "the building breathes". A per-axis gain (`size`) lets the warp act only horizontally, so floors stay floors and objects on them stay grounded. The phase is a **parameter**, not `speed·time`, so the flow's speed can be driven (integrated) by the music and stops dead in silence. Lipschitz grows by about `A·f·2`; the march is relaxed (`stepScale` 0.7-0.85) as for twist and bend. |
| Displacement noise (`d += A·fbm`) | keep, but not for architecture | It turns planes into blobs, which is the "geometry that merely jitters" failure (§20). It is right for a surface tremble at a very small amplitude (the addendum's "subtle unstable vibration" for distorted synth). |
| Curl noise / flow fields | reject for geometry | Divergence-free flow matters for advected particles, not for a static domain warp; it costs 6 fbm evaluations per sample against the warp's 3 value-noise lookups. Particles already have it (`curlNoise`). |
| Twist, bend, fold (existing) | adopt, driven continuously | They are already continuous in their amounts. The fault was never the operators, it was switching them by scene state. |
| `morph` between cell designs | adopt, driven by a slow timeline curve only | A convex blend of two fields is a valid bound, so a room can become a corridor over eight bars. Driven by a smooth key it is continuous; driven by a state or a quantised route it is the owner's complaint. The example uses it only on a timeline. |
| **Damped spring response** (critically damped second order) | **adopt**, as the `spring` chain stage | A one-pole (attack/decay) already exists, but its output has a corner at every input change: a kick gives a visible kink. A second-order system `x'' = ω²(u − x) − 2ζω x'` has a continuous first derivative, so motion eases in *and* out: weight, not twitch. ζ = 1 is critical (no overshoot); ζ < 1 gives the "breathing under the weight of the low end" the addendum asks for; the natural frequency sets how slowly the world responds. Fixed internal sub-steps make it frame-rate independent to within the sub-step, and exact against a seek (the replay steps the same 60 Hz grid as a 60 fps render, ADR-901). |
| **Rate integration** | **adopt**, as the `integrate` chain stage | `phase = ∫ rate dt`: a warp's flow, a noise phase, the camera's distance. The music sets the rate, so loud passages flow faster and silence stops the flow, and the value never jumps whatever the input does. The integral lives in the chain state, so seeks replay it (ADR-901). |
| Low-pass / envelope smoothing | existing | Attack/decay remain the first line; the spring goes after them. |
| Sample-and-hold `random`, scene states, quantised `count` | **do not use for visible geometry** | These are the "states" the owner rejected. `count` changes are integers and cannot be continuous; the example never routes one. Scene states stay in the engine for the POC's presets (untouched), and are not used here. |
| Interpolating transforms and orientations | existing where needed | The camera's orientation comes from a look target on the path, which is continuous by construction; the journey never interpolates Euler angles. |

The art agent's controls therefore become: **timeline tracks** (smooth keys) for authored trajectories, **LFO and
noise sources** for autonomous drift, and **routes with `spring` or `integrate`** for musical modulation. All three
are seek-exact.

## 4. Camera traversal through an effectively infinite world

### The journey camera (composition camera mode 3)

A periodic path through one **cell** of the world:

- `path`: control points in the cell's frame, joined by a centripetal Catmull-Rom spline and resampled by arc
  length. The point after the last is the first point carried into the next cell by the cell transform `S`, so
  the path continues forever, with continuous tangents across the seam.
- `S` (the **screw**) is the same transform the world's `screw` node repeats by: a translation `T` (a corridor, or
  a stair that climbs forever), or a rotation by `360/n` degrees about Y plus a rise (a spiral or a Penrose loop).
- `camera/journey/distance` (metres along the path) places the camera. It is an ordinary parameter, so the director
  keys it on the timeline (**holds are two equal keys; hesitation is a small backwards dip; slow turns are a
  slowed distance through a bend**), and audio can add to it through a route with `integrate` (a pace). With the
  audio muted the timeline still carries the journey, so it keeps moving (or pauses where the director held it).
- Look controls, all parameters: `lookAhead` (metres), `yaw` and `pitch` (degrees, relative to the path: "look into
  the empty room", "look up the staircase"), `height` above the path, `sway` (a slow pure-noise drift of the
  aim, the searching gaze) and `bob` (a walk bob that is a function of *distance*, so it stops when the walker stops).
- **Collision guard:** the composition evaluates the named SDF object's live tree on the CPU at the eye. If it is
  closer than `radius` to a surface, it is pushed out along the field's normal. The guard is a pure function of this
  frame's parameters, so it is deterministic and seek-exact. It is a safety net; the guarantee is by construction:
  the path is authored in the free space of the cell, and `tools/liminal_sdf.py` plus a unit test sample the CPU
  field along the whole path, with the deformations at their maxima, and require clearance.

### The invisible wrap: the camera never leaves one stretch of the world

Every visible thing is periodic under `S`, and after `m` cells (`m = n` for a rotation of `360/n`, 1 for a pure
translation) `S^m` is a pure translation. So the camera is placed at `distance mod (m·L)`, where `L` is the path's
length per cell: at the wrap it jumps back by exactly `S^m`, and the frame it sees is the same frame. Consequences:

- **Precision:** the camera stays within a few cells of the origin however long the film is.
- **Everything else can be static:** lights and props are placed in the cells `-2 .. m+N` once, and the camera
  never outruns them. No treadmill, no pop-in.
- **The rule it imposes:** anything that is not periodic under `S^m` shows the seam. Deformations must be inside the
  screw (in cell coordinates), so every cell deforms alike. That is a feature here: "the same hallway appearing
  again", and the addendum's repetition. A light or a prop outside the placed range pops at the wrap; the generator
  places them.
- **Directional light and sky** are invariant under a translation, so with `S^m` a translation they never jump.
  Under a single `S` with rotation they would, which is why the wrap waits for `m` cells.

### Why not a spline camera, or a free camera keyed per shot

The spline camera (mode 2) wraps only for closed splines, and a closed spline cannot climb. A free camera with
keyed position and target is exactly as expressive but asks the art agent to hand-author 250 s of 3D keys against
geometry it cannot see while authoring. The journey reduces the camera to one scalar (distance) and three angles,
all keyable, on a path that is valid by construction.

## 5. Impossible continuity and repetition

What the SDF can do cheaply, and what I will build or leave:

| trick | how | status |
|---|---|---|
| **Endless climb** | `screw` with `T = (L, H, 0)`: every `L` metres forward the world is `H` higher. A stair in each cell connects to the next cell's landing. The camera climbs forever and the wrap hides it. | build |
| **Penrose loop** | `screw` helix, `n = 4`, rise `h` per quarter turn: four corridors round a square well, each a stair up. Walk four sides, climb four flights, arrive "where you started", one storey higher, and see the other flights across the void, from other directions (the addendum's "the same staircase from a different direction"). | build |
| **"Have I been here before?"** | the wrap itself: the same cell recurs every `L` metres. | free |
| **"A doorway that subtly changes each time"** | the doorway's parameters drift on slow timeline curves or LFOs. The camera returns to the cell later, so it finds it changed. Nothing within one view changes discontinuously. | free (parameters) |
| **Rooms that change kind** | `morph` between two cell designs that share the path's free corridor; driven by a slow key. | existing |
| **Gravity turning** (a floor becomes a wall) | a fold, or a rotated sub-cell reached by a stair; a screw roll about the travel axis would need a camera up vector (the pose has none). | partial: fold and rotate; no roll screw |
| **True portals** (a doorway into a room larger than its building) | needs the ray to be transformed when it crosses a portal surface, i.e. state in the marcher. | **not tonight**; documented as the next step. The wrap is the portal the camera uses. |
| **Per-cell variation** (a row of near-identical rooms) | a cell index fed into the content | not tonight; time drift covers the addendum's examples |

## 6. The palette (§7, addendum colour story)

A project block, `palette`, holds an ordered list of **named palette states**. Each state names colours by role
(`wall`, `floor`, `accent`, `light`, `fog`, `sky`, `emissive`, `grade`, ...) and optional scalars. **Bindings** map
roles to parameters (`sdf/world/material/baseColor`, `scene/fogColor`, a light's colour, `post/grade/tint`, ...).
Three parameters drive it:

- `palette/position`: a float through the ordered list (2.4 = 60 % of the way from state 2 to state 3);
- `palette/saturation`: a chroma multiplier (0 = grey, 1 = as authored);
- `palette/value`: a lightness multiplier.

Interpolation is in **OKLab** (Ottosson 2020): a straight line in a perceptual space, so a blend from teal to amber
passes through a quieter middle rather than sweeping round the hue wheel (no rainbow, §7, §20). Saturation scales
OKLCh chroma; value scales OKLab lightness. The palette writes the bound parameters' finals **after the timeline and
the routes**, so the director keys `palette/position` on the timeline and may also route a smoothed signal into it or
into saturation. It is a pure function of this frame's parameter finals, so it is seek-exact whenever they are.

## 7. Objects in the world (§8, addendum's figure)

- **Composition** is already solved: meshes compose with the SDF through depth.
- **Placement relative to the architecture:** `tools/liminal_sdf.py` places a prop at a cell-local anchor in every
  cell of the covered range (`S^j · anchor`), so props repeat with the world and survive the wrap.
- **A journey anchor** puts a composition node on the path at its own distance parameter
  (`<node>/journey/distance`), with an offset in the path frame, facing along the path. A hero object can hover
  ahead of the camera ("relatively stable"); a figure can walk ahead of the camera on its own keys, reach a corner,
  and be left behind or turn off it. The wrap applies to it exactly as to the camera.
- **Deformation response per layer** (§8): the architecture takes the warp at full strength; props sit on floors
  that the horizontal-only warp does not move; the hero rides the path, untouched. Different response gains are
  simply different route amounts.
- **The figure** (addendum): an animated GLB needs a walk clip. The astronaut prototype's GLBs are local-only and
  generated; whether one has a usable walk loop is checked at (e), and if not the art agent gets a static
  figure or environmental scale cues. Nothing generated is committed.

## 8. Determinism and seeks (ADR-091)

| state | where it lives | seek-exact because |
|---|---|---|
| authored trajectories | timeline tracks, LFO/noise/timeline sources | pure functions of the clock |
| spring and integrator state | route chain state | ADR-901 replays and checkpoints chain state |
| the camera, anchors, collision guard | functions of this frame's finals | stateless |
| the palette | a function of this frame's finals | stateless |

The one caveat is ADR-901's: routes are replayed on the 60 Hz grid, so a render at 60 fps is bit-exact after a
seek, and at 30 fps a spring converges to the played value within its time constant. Final renders should be 60 fps,
or rendered from 0.

## 9. LIVE and the music video (§19, as the addendum revised it)

The addendum makes them separate deliverables. What they share is *techniques*, not a runtime:

- **Behaviours are parameters.** Both modes drive the same named parameters (the journey's distance and look, the
  warp's amount and phase, the palette's position, saturation and value).
- **Music video:** the director owns trajectories on the timeline; audio adds spring-damped modulation.
- **LIVE:** no timeline. The same parameters are driven by routes alone: `integrate` turns a live energy into
  camera pace and flow (with an `offset` that keeps a floor so it never stops unless asked), `spring` shapes
  every live band, MIDI (`control.*`) moves palette position and look. The chain stages work on live input
  unchanged; only the seek guarantee is lost, which live mode never had.

So nothing in the music video is compromised for LIVE, and LIVE later reuses every stage built here.

## 10. Performance

Cost is pixels × steps × evaluation. The additions' costs per evaluation:

- `stairs`: one zig-zag distance over three candidate steps and a box: comparable to a `cone`.
- `screw`: one division and a rotation: free.
- `warp`: three value-noise lookups (24 hashes). It is the expensive one; one warp per world, at low frequency.
- The journey's collision guard: one CPU tree evaluation and a normal (5 evaluations) per frame: microseconds.

Budget: a hall-sized compiled tree was 8-13 ms at 960x540. Big empty rooms help: rays reach far walls in few steps
unless they graze; the far wall falls into fog, and `maxDistance` should be set at the fog's reach. Measured
numbers go in PROGRESS-eng.md once the example exists.

## Sources

- Quilez, "Domain warping": https://iquilezles.org/articles/warp/ (the technique; low-frequency warps keep
  structure).
- Quilez, "Distance functions" and "Domain repetition": https://iquilezles.org/articles/distfunctions/,
  https://iquilezles.org/articles/sdfrepetition/ (repeat of asymmetric content, neighbour cells).
- Ottosson, "A perceptual color space for image processing" (OKLab), 2020:
  https://bottosson.github.io/posts/oklab/ (palette interpolation; chroma as saturation).
- Game-programming damped springs: Ryan Juckett, "Damped springs",
  https://www.ryanjuckett.com/damped-springs/; Daniel Holden, "Spring-It-On",
  https://theorangeduck.com/page/spring-roll-call (critically damped smoothing as a C1 filter).
- Keinert et al., "Enhanced Sphere Tracing", STAG 2014 (relaxation for non-exact fields).
- Centripetal Catmull-Rom (Yuksel, Schaefer, Keyser 2011, "Parameterization and applications of Catmull-Rom
  curves"): no cusps or self-intersections through unevenly spaced points, which matters for a camera
  path through doorways.
- The repository: ADR-027, ADR-091, ADR-870, ADR-901, ADR-1000-1005, `docs/sdf.md`,
  `docs/prototypes/procedural-space/{ARCHITECTURE,RESEARCH,REPORT}.md`.
