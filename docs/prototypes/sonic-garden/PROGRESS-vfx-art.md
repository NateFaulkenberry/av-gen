# Sonic Garden VFX expansion: art progress (sonic-art)

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). The engineer's notes are `PROGRESS-vfx-eng.md`, their
architecture `VFX-ARCHITECTURE.md`. Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. Commit only my own
paths (`git commit -- <paths>`). Review media: `~/Desktop/av-gen-review/25-sonic-vfx/` (look-dev stills in
`lookdev/`).

## Resume here (cold)

- **Done and committed:**
  - research reports 1 and 3 (`research/01-vfx-scene-construction.md`, `research/03-authoredness.md`);
  - `SCENE-CATALOG.md`: 16 scenes, tier A (8) first;
  - the scene kit `tools/sonic_vfx/` (kit, signals, make, review, variant, test material);
  - scenes 1-6: Event Horizon, The Breathing Deep, Tesla Choir, The Corrupted Cathedral, Ferrofluid Crown, Cymatic
    Plate. All are in `examples/index.json` under "Sonic VFX".
- **In progress:** scene 8, Salt Flat Mirage (`scenes/salt_flat.py`): the look is close. Still to do: a clip and the
  evaluator (`review.py eval`), then the look-dev still and the commit.
- **Next:**
  - Storm Cell (7);
  - tier B, now unblocked: Feedback Mirror needs `temporal/feedback`, Datascape `post/glitch|sort|display`
    (ADR-1065/1066);
  - adopting the post instruments on the Cathedral (glitch, tear) and Event Horizon (shock on the kick);
  - `TEST-MATRIX.md`, the evaluator pass on every scene, the captures.
- **The set list** is `scenes/__init__.py`'s SCENES order, which is also the index order. The live switcher
  (ADR-1063) steps Sonic Live, then the SCENES in order; MIDI program n opens scene n mod count. The order runs:
  1. Salt Flat
  2. Breathing Deep
  3. Cymatic Plate
  4. Ferrofluid Crown
  5. Tesla Choir
  6. Cathedral
  7. Event Horizon
- **Each scene carries:**
  - `sonic.response`: the performer's baseline (sensitivity, transient, sustain, attack, release). The switcher carries
    the performer's offsets from it across a switch.
  - `sonicScene.regions`: screen boxes for the evaluator, projected through the camera at t = 0 (`kit.region*`).
- **The pinned engine:** `$S/vfx/bin-337903f0` (binary, probe and shaders), used through the wrapper
  `$S/vfx/avgen.sh`. `S` is the scratchpad:
  `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  - Each pin is built in an isolated `git archive` export, `$S/vfx/src-<sha>` (`cmake --preset release
    -DCPM_SOURCE_CACHE=../av-gen/.cache/cpm`, targets `avgen avgen_sonic_probe`). Never build or touch
    `build/release`: the engineer owns it.
  - 337903f0 has:
    - ADR-1060/1061: live kick, snare and hat, and signal triggers;
    - ADR-1062: `response.*` and per-note `notes.*`;
    - ADR-1063: the scene switcher;
    - ADR-1064: the publish source;
    - ADR-1065: `post/shock|glitch|split|sort|radial|display`;
    - ADR-1066: `temporal/feedback|slit`.
  - The evaluator `tools/sonic_vfx_critic.py` (0f809c90) is python only, so there is no re-pin. Its `trace` mode calls
    `build/release`, so `review.py eval` runs the trace with the pin instead.

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

## Scenes built

| # | id | state | look-dev | notes |
|---|---|---|---|---|
| 1 | event-horizon | built | `lookdev/01-event-horizon-lookdev.png` | The DF black-hole lens gives the *Interstellar* image. A polar-streak disk program; note-stars by pitch radius. |
| 2 | breathing-deep | built | `lookdev/02-breathing-deep-lookdev.png` | SDF cavern; the gill ring is the only warm light. 11 threads, each with a travelling Pulse. |
| 3 | tesla-choir | built | `lookdev/03-tesla-choir-lookdev.png` | 12 arcs (`fx/arc<i>`) on the circle of fifths, following `notes.class.<k>`. Seen from above, so a chord reads as a shape. |
| 4 | corrupted-cathedral | built | `lookdev/04-corrupted-cathedral-lookdev.png` | The line-look SDF nave, a Voronoi fracture and a warp. Mosh and sweep on phrases. |
| 5 | ferrofluid-crown | built | `lookdev/05-ferrofluid-crown-lookdev.png` | After Kodama. Graded polar rings of cones, 12 note spikes on springs, a twisted tower. |
| 6 | cymatic-plate | built | `lookdev/06-cymatic-plate-lookdev.png` | A Chladni program (43 ops); the pitch sets the mode numbers. Sand leaps on hits. |
| 8 | salt-flat-mirage | look close | (pending) | A monolith, with the sun peeking past its edge and a shadow wedge to the camera. Dusk palette states; meteors per note; a mirage line; the kick sends a ring out, the snare flares the outline. |

## Plan

1. Research reports 1 and 3 (done).
2. `SCENE-CATALOG.md` (done).
3. Build the scenes as data (in progress: 7 of 16).
4. `TEST-MATRIX.md` from the test material (file renders) and probe runs (live).
5. Evaluate with the Critic and the engineer's evaluator, iterate, and kill weak scenes.
6. Captures: a still and a short clip with audio per scene, and a tour.
