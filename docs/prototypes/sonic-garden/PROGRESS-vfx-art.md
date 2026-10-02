# Sonic Garden VFX expansion: art progress (sonic-art)

**STOPPED 2026-10-02: art pass rejected by the owner, restart under 03-brief-art-restart.md**

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). The engineer's notes are `PROGRESS-vfx-eng.md`, their
architecture `VFX-ARCHITECTURE.md`. Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. Commit only my own
paths (`git commit -- <paths>`). Review media: `~/Desktop/av-gen-review/25-sonic-vfx/` (look-dev stills in
`lookdev/`).

## Resume here (cold)

- **Done and committed:**
  - research reports 1 and 3; `SCENE-CATALOG.md` (as-built notes for Salt Flat, Storm Cell and tier B);
  - the kit and tools in `tools/sonic_vfx/`;
  - **all 16 scenes**, listed in `examples/index.json` in set-list order (`scenes/__init__.py` SCENES);
  - the perf pass (`4423e4da`): static SDFs as surface-nets meshes (`kit.sdf(..., mesh=128)`), Cathedral without its
    Voronoi fracture, Ferrofluid without the shimmer noise, no surface-pass march in Salt Flat and Lantern Lake;
  - the matrix report (`1d079577`): set-list order, an at-a-glance grid, verdicts from `tools/sonic_vfx/matrix_notes.py`
    (`VERDICTS`, filled after the run); `live.py` runs the probe's `tour` with per-scene live stats (`tour.md`).
- **The pin is `96bc0214`** (ADR-1069 voronoiEdge, ADR-1070 the live sky): `$S/vfx/bin-96bc0214`. The matrix's first
  seven scenes ran on e6978f28; ADR-1070 changes only live rendering, so their numbers stand.
- **State at 14:40 (2026-10-02):**
  - all 16 scenes final in examples (commits through `66c3c8d5`), audited clean; `[sonic]` passed (207060
    assertions, 39 cases, exit 0) with the engineer's build at 14:11;
  - captures in `~/Desktop/av-gen-review/25-sonic-vfx/`: `NN-<id>.mp4` (1080p30, the full mix, with audio) and
    `NN-<id>.png` for all 16, `00-tour.mp4` (10 s of each, cross-faded, one piece of music through sixteen worlds),
    `00-live-tour.mp4` (the live switcher through all 16 with live input, captured at 960x540, the probe's audio);
  - the evaluator on every capture: `critic/NN-<id>.critic.md`;
  - perf: `perf-realtime-1080p.md` and `perf-preview-1080p.md` (final);
  - the live tour without capture: `live/salt-flat-mirage--tour--nocap/tour.md`, pasted into `matrix_notes.LIVE`;
  - the matrix: scenes 1-10 and Datascape done; the main loop runs on through Event Horizon; re-runs for
    Lantern Lake, Aurora Tundra, Abyssal Bloom, Silk Theatre, Ferrofluid Crown and Tesla Choir (changed after their
    first run). Breathing Deep's and the Cymatic Plate's rows are from their first run (their later changes were a
    bloom cap and a key light, not a response).
- **Next:** when the matrix finishes, fill `VERDICTS` for Datascape to Event Horizon (`tools/sonic_vfx/matrix_notes.py`),
  `python3 tools/sonic_vfx/matrix.py report` (writes `TEST-MATRIX.md`), commit; re-capture any scene changed after
  14:16 (`capture.py clip <id>` then `still <id>`, then `capture.py tour`); re-run `[sonic]`; hand back.
- **Pending decisions:** Datascape's nearer numerals (`look-ds2`, module edited, not built); a meshed Tesla hall
  (`abl/tc-mesh`: 24.1 -> 22.4 ms) and a milder Ferrofluid tower twist (`abl/ff4-*`), both awaiting a look.
- **GPU etiquette:** one job per lock hold. The lock is shared with the engineer and other agents (a full
  `avgen_tests` run held it for 45 minutes at 12:25); do CPU work meanwhile.
- **The set list** is `scenes/__init__.py`'s SCENES order, which is also the index order. The live switcher
  (ADR-1063) steps Sonic Live, then the SCENES in order; MIDI program n opens scene n mod count:
  1 Salt Flat, 2 Lantern Lake, 3 Aurora Tundra, 4 Breathing Deep, 5 Abyssal Bloom, 6 Cymatic Plate, 7 Silk Theatre,
  8 Ferrofluid Crown, 9 Feedback Mirror, 10 Tesla Choir, 11 Datascape, 12 Ember Forest, 13 Cathedral, 14 Storm Cell,
  15 Stellar Nursery, 16 Event Horizon.
- **Each scene carries:**
  - `sonic.response`: the performer's baseline (sensitivity, transient, sustain, attack, release). The switcher carries
    the performer's offsets from it across a switch.
  - `sonicScene.regions`: screen boxes for the evaluator, projected through the camera at t = 0 (`kit.region*`).
  - `sonicScene.vocabulary`: rows whose second entry is exactly one bus id the trace carries. The evaluator checks
    the first id in a row: hits are named by their `response.*` event (it reads the `...Env`), notes by
    `response.note`. A combined string is skipped silently.
- **The pinned engine:** `$S/vfx/bin-96bc0214` (binary, probe and shaders), used through the wrapper
  `$S/vfx/avgen.sh`. `S` is the scratchpad:
  `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  - Each pin is built in an isolated `git archive` export, `$S/vfx/src-<sha>` (`cmake --preset release
    -DCPM_SOURCE_CACHE=../av-gen/.cache/cpm`, targets `avgen avgen_sonic_probe`). Never build or touch
    `build/release`: the engineer owns it.
  - The evaluator `tools/sonic_vfx_critic.py` is python only. Its `trace` mode calls `build/release`, so
    `review.py eval` runs the trace with the pin instead.

### The workflow

- **Build the scenes:**
  - `python3 tools/sonic_vfx/make.py [module ...]` writes `examples/sonic-vfx/<id>.json` and `.scene.json`.
  - With no arguments it also rewrites the Sonic VFX entries of `examples/index.json` in SCENES order.
  - For look development, use `--out $S/vfx/look` and render from there.
- **Check without the GPU:** `$S/vfx/avgen.sh --project <p.json> --audit-routes out.json 2>&1 | grep -E "warn|error"`.
  It must print no warning. A scene that fails to load silently renders the default orb. Causes seen so far:
  - tube segments over 512;
  - box subdivisions over 64;
  - a procedural size over 1000 m, which is clamped (scale the transform instead).
- **Test material:** `python3 tools/sonic_vfx/make_test_material.py` writes 13 classes: WAV in `assets/audio`
  (gitignored), MIDI in `examples/sonic-vfx/test/`.
- **Review** (all through `tools/gpu-lock.sh` with the pin):
  - `python3 tools/sonic_vfx/review.py stills <id> <class> --projects <dir> --out <dir> --at 2,6,10 --size 1280x720`;
  - `clip`;
  - `matrix` (all classes as clips);
  - `eval <id> <class>`: a clip, the pin's `--sonic-trace`, and the evaluator's `measure` report (`.critic.md`).
  - The GPU lock can be held for 20+ minutes by the engineer's test suite. Do CPU work meanwhile.

## Engine facts that shape the art (verified)

- **Interpret source:** after ADR-1062, `bias` is clamped to ±64 and `gain` to ±256. Before it they were ±4 and ±16,
  and `kit.place_bumps` still builds three stages inside those old limits.
  - Combines: `sum` and `min/max` multiply by the weights (0..100); `mean` divides by them; `product` uses them as
    exponents.
  - A source reads its own outputs one frame late. Later sources read earlier ones the same frame.
- **Route depth** scales the route's OUTPUT every frame (`modulation.cpp:171-180`). A decaying flash through a place
  depth is cut off when the place moves. Carry the place into the chain's input instead (the `Hit` mappings).
- **A particle `burst` is truncated to a whole count each frame** (`particle_renderer.cpp:607`). An event of
  strength 0.7 times amount 1 emits nothing. For exactly one particle per event, use
  `threshold="binary", thresholdLevel=0.01` with amount 1.
- **The route chain threshold** modes are `gate`, `binary` and `subtract` (`params/processor.hpp`).
- **Effect Library:**
  - Instances go in the project's `effects`. A trigger is the effect's top-level `"trigger"` with
    `"activation": "trigger"`.
  - Bolt and arc endpoints are `parameters.source/target` (`{"kind": "world", "position": [...]}`).
  - FXL surface effects reach mesh and procedural owners, not SDF. An owner must be a scene `entities` entry
    driving the node.
  - Pulse travelling mode: rate 0 plus a routed phase locks it to anything.
  - Light Beam points down (tilt 0) to horizontal (±89): it cannot point up.
  - A World-owned `groundPulse` needs a `source` of kind `world` with a position. Its default `owner` source never
    fires on the World.
  - Heat Shimmer is a DF column (radius, height, base at the offset). Its strength is metres at the column, so a
    horizon shimmer needs a column round the camera, out to the horizon, strength ~1.
  - Meteor Shower uses the comet bucket (six at once, sky-anchored). Per-note meteors are particles instead.
- **`addDefaultPostRoutes`** adds `audio.rms → bloom` and `audio.onset → CA` on a scene swap unless a route already
  targets them. Every scene has its own routes on both.
- **The Cosmic Ocean was removed** (ADR-441). Nebulae are matte-painted planes with an emissive program, far
  behind (the DF lens bends them).
- **Procedurals are opaque** (Blend draws opaque).
  - Additive light is particles or SDF emission.
  - A Mask material with a PROGRAM's opacity is discarded in the colour pass, but the depth prepass still writes the
    whole shape, so the clear colour shows through. The crescent moon failed this way.
- **Material programs:**
  - op constants are routable at `material/<program>/op/<i 1-based>/<kind>/constant|value`, per component;
  - polar coordinates are possible with gradient → power(0.5) → power(−1) (see `event_horizon.disk_program`);
  - `|p|` is multiply(p, p) → power(0.5); `localPosition` is in the mesh's metres (the monolith's outline uses it);
  - `localPosition` travels with a twist-spun mesh;
  - `voronoi` is a 3D F1, so a slice through a plane gives soft blobs, not crisp cells. There is no F2 or edge op.
    Salt polygons and mud cracks are not possible in a program without an engine op (`worleyF1F2` exists in
    `noise.wgsl` for the Effect Library).
- **Light:**
  - **Contact shadows dither a whole lit plane under a grazing sun:** a regular dot pattern at 4.5 degrees. Set
    `contactShadow: false` on low suns. `shadowBias` and `softness` do not fix it.
  - The procedural sky is the IBL when there is no environment map.
  - Sky model: `haze = exp(-dir.y / haze)`, so `haze` 0.17 keeps the zenith colour to about 20 degrees. The sun's
    aureole is `exp(-theta/sunGlow) * 0.02 * sunIntensity`, so a wide `sunGlow` greys the whole sky.
- **Post:**
  - AgX (the default tonemap) greys a deep blue zenith. PBR Neutral (`post/tonemap/operator` 3) keeps authored dusk
    hues.
  - Halation, or the bloom chain it rides, lifted a dusk sky's top by ~30 levels. Keep it ≤0.06 in bright-sky
    scenes.
  - Volumetric in-scatter toward a low sun fills a black object in front of it. Keep the march short
    (`volumeMaxDistance` ~60 m) and let `fogSky` carry the distance.
  - `fogSky` 1 fades far surfaces to the sky's own radiance behind them: a range melts into its horizon.
- **Camera:**
  - Tracks work, and `camera/mode` comes from the scene's camera block.
  - The vertical field of view comes from a 24 mm sensor height (`fovY = 2 atan(12/f)`), so 18 mm is 67 degrees tall.
  - `kit.Scene.project()` reproduces the framing (checked against the Salt Flat monolith to a pixel).
  - Judge thirds on the full-size frame, not a scaled sheet: I misread one.
- **Palette** (ADR-1043): `palette/position` in a project's `parameters` is ignored at load (the palette registers
  later). Set the palette block's own `position`. Routes onto it bind.

- **Rates are hazards:**
  - A route on a rate whose phase is time x rate jumps the pattern by t x d. The auditor flags it as
    `phase-rate`. Examples: an aurora's `driftSpeed` and `flowSpeed`, `scene/volumeNoiseSpeed`.
  - Route a phase instead: the chain's `integrate` stage turns a pace into a distance (Datascape's glide) or a
    phase (Stellar Nursery's nebula churn, on a noise op's constant).
- **A procedural's scale cannot be negative** (clamped to 0.001). To mirror a shape (Lantern Lake's reflection),
  turn it 180 degrees about X with its deformers in LOCAL space: that flips height and depth and keeps the
  left-to-right shape.
- **Procedural limits:**
  - box size at most 1000 m (scale the transform instead);
  - box subdivisions at most 64;
  - no negative scale.
- **A colour route per component with a zero weight is a dead route** (the auditor's `zero-amount`). Skip the
  component.
- **The volume march** runs uniformly from the camera to `volumeMaxDistance`. With 0 and a placed medium (a
  tornado), it carries the media alone out to their farthest reach. A distant funnel gets coarse steps, so a
  tornado has to be near or the steps raised.
- **Live (`--live`) needs the editor window:** there is no headless live mode. `live.py` opens one per run.
- **Diagnose a look by bisecting the PROJECT too.** Ember Forest's orange trunks were not their material, a light
  or the scene's fog. A route (`bass → scene/volumeDensity +0.02`) multiplied the fog six-fold whenever the bass
  played. A stripped scene under a plain project rendered correctly. Sample the same pixels in every arm: one wrong
  sample point sent me after the material for an hour.
- **Particle trails are short ribbons:** at most 32 points, recorded per simulation step, so their length in
  seconds depends on the frame rate. For a long ribbon, lay particles where the emitter moves and leave them
  (Silk Theatre's stroke).
- **Light direction is the direction of travel** (verified with primitives: `[1,0,0]` lights left-facing
  surfaces).
- **A scripted variant build can import stale bytecode.** This Python keeps its caches in
  `~/Library/Caches/com.apple.python` (`sys.pycache_prefix`), keyed by the source's mtime in whole seconds and its
  size. Two `sed` flips of a module within one second that leave its size unchanged (`True False` to `False True`)
  rebuilt the previous variant. Give each build its own `PYTHONPYCACHEPREFIX`.
- **The GPU lock times out after an hour of waiting** (`AVGEN_GPU_LOCK_TIMEOUT`, default 3600 s, exit 75). Behind a
  long suite a queued render fails; set the variable higher for long queues, and re-run a matrix with
  `--skip-existing`.
- **SDF cost is per step and per tree:** a part every ray must evaluate at every step costs the whole frame, even
  when it fills a tenth of it. The Cathedral's rose (twelve-fold polar tracery) was half its 61 ms SDF pass; as its
  own object with tight bounds it marches only its rect, and both objects together cost 16 ms. Static parts that
  need no line look can also leave the tree as lit meshes (the Cathedral's floor became a plane with tile lines).
  Meshing is not always cheaper: the Ferrofluid's dish meshed cost 8 ms in the lit pass under three rect lights
  against 6 ms raymarched.
- **Volume steps:** 16 held the look in Ember Forest, Silk Theatre, Abyssal Bloom, Tesla Choir and the Cathedral
  (7-8 ms saved each at 1080p); the tornado speckles at 16, so Storm Cell keeps 24.
- **Material program ops added this pass:** `voronoiEdge` (ADR-1069) gives `vec4(F2-F1, F1, hash, F2)`; flatten the
  position with a multiply by (1, 0, 1) first for ground patterns (Salt Flat's polygons, the aurora ice's plates).
  A palette binding can target a program op's constant (`material/<p>/op/<i>/constant/constant`): the aurora ice's
  reflection colour follows the palette's low role.

## Scenes built (set-list order)

All 16 are listed in `examples/index.json` and audit clean (0 warnings, every route live).

| # | id | the image | notes |
|---|---|---|---|
| 1 | salt-flat-mirage | a black monolith at dusk, the sun past its edge, its shadow a wedge to the camera | The dusk palette states move with tension. A meteor (with an ignition flare) falls per note at the pitch's place. The kick lights the monolith's foot and sends a ring out; the snare flares its outline. |
| 2 | lantern-lake | a dusk lake, a jetty, lanterns rising and mirrored | The mirror is the sky's gradient on an unlit plane below the reflected world, with twins of the hills, the jetty and the lanterns (the engine has no planar reflection). Each note releases a lantern at the pitch's place. |
| 3 | aurora-tundra | an aurora band with rays over a spruce line and black Baikal ice | The aurora's own spectrum response is off; routes drive it. Tension and polyphony move the palette (green, teal, violet, rose). The snare lights the ice's fractures. |
| 4 | breathing-deep | an SDF cavern, a glowing organism, threads | The gill ring is the only warm light. 11 threads with travelling Pulse. |
| 5 | abyssal-bloom | a siphonophore diagonal in black water, jellyfish with tentacles | The light runs along the stem to the pitch's place (a gradient-op band by world position). The jellyfish at the pitch's depth answers. Marine snow; the kick sends a shock. |
| 6 | cymatic-plate | Chladni figures in sand | A 43-op program; the pitch sets the mode numbers. |
| 7 | silk-theatre | a red silk stroke drawn in the air under a spot, a velvet curtain | Silk is laid while notes sound (particles left where the hand was), pitch is height, a chord adds a gold stroke, the kick shudders it. |
| 8 | ferrofluid-crown | black magnetic liquid, a crown of spikes | After Kodama; the field's copies are made unequal by a faint noise. |
| 9 | feedback-mirror | a gold sigil whose echoes curl into a nautilus spiral | temporal/feedback with 24 taps. The interval turns the spiral; the bass deepens it; the kick punches the zoom. The hue walks gold, rose, magenta. |
| 10 | tesla-choir | 12 Tesla towers on the circle of fifths, arcs per pitch class | A chord's arcs draw its shape. |
| 11 | datascape | barcode lines to a vanishing point, red crests, numerals | Lines are summed cosines thresholded (exact, never organic). The glide is integrated level. The kick sends a shock; the snare tears. |
| 12 | ember-forest | black trunks against a fire front, coal seams, one dancing ember | The ember's height is the melody. The snare makes the bark flare; the bass stirs the smoke (its structure only). |
| 13 | corrupted-cathedral | the line-look SDF nave, the rose window | Mosh and sweep; the kick splits, the snare tears, roughness pixel-sorts (ADR-1065). |
| 14 | storm-cell | a dark tornado on the clear slot, a wheat field, power poles, rain | The bass winds the funnel; the snare strikes (lightning, a live trigger); notes light the cloud base at their place. |
| 15 | stellar-nursery | dust pillars with magenta rims against a teal-rust nebula | A chord lights as many embryonic stars as it has voices, coloured by its pitch. The kick sends a shock from the nest. |
| 16 | event-horizon | the DF black-hole lens, the disk | The kick sends a shock ring from the hole. |

## Plan

1. Research reports 1 and 3 (done).
2. `SCENE-CATALOG.md` (done).
3. Build the scenes as data (done: 16 of 16, all listed).
4. `TEST-MATRIX.md` from the test material (file renders) and probe runs (live).
5. Evaluate with the Critic and the engineer's evaluator, iterate, and kill weak scenes.
6. Captures: a still and a short clip with audio per scene, and a tour.
