# DIGITAL MOSH: Implementation

How the design in `02-design.md` is built in the engine, what each system costs, and why it is built that way. The
scene source is `examples/digital-mosh/build.py`; the generated files beside it should never be edited by hand.

## Files

| File | What |
|---|---|
| `examples/digital-mosh/build.py` | Source of truth: palettes, layout, vantages, fields, the contagion grid, material programs, stages, the arc, the audio routes, MIDI |
| `examples/digital-mosh/forms.py` | The olive (five SDF objects and their voxel shell) and the Tanguy object |
| `examples/digital-mosh/land.py` | The geography (near and far terrain over one description); engine-backed height queries through `tools/gv3/ground.py` |
| `examples/digital-mosh/palette_extract.py` | k-means in CIELAB over painting reproductions (`05-palettes.md`) |
| `examples/digital-mosh/critic_inputs.py` | Creative Critic `intent.json` and `shots.json` for a render, from the engine's own arc trace |
| `examples/digital-mosh/play_live.py` | Plays a track into BlackHole for live-input tests and logs its host start time |
| `digital-mosh.scene.json` | The world (generated) |
| `digital-mosh.json` | Feline Footwear (the primary development track) |
| `digital-mosh-trench.json` | Trench (the contrasting track) |
| `digital-mosh-live.json` | LIVE: live input (`sonic.live`), the `live` block (LIVE AUTO), no file |
| `examples/digital-mosh/arc.py` | Any track's stage timeline through the real engine, no GPU (about 2 s per song) |
| `tests/integration/test_digital_mosh.cpp` | `[digital-mosh]`: every preset path, route target and macro target is a parameter |

## The world

| Element | Representation | Why that representation |
|---|---|---|
| Land | Two `terrain` nodes over one geography (`land.py`). **Near**: 900 m, 30 m chunks at 24 quads (1.25 m). **Far**: 8 km, 400 m chunks; under the near land it is cut 25 m down and sits 0.8 m lower | The camera travels over the near land. The far land holds the escarpment, the mesas and the ranges. The two agree exactly at the seam (probed), because they share every layer |
| Ground | Material program `ground` on the near land (47 ops: the stage's painting by slope, mottled, plus the infected surface); `groundFar` (8 ops) on the far land | One painting per stage reaches both lands through the ramp constants, so the seam never shows |
| Haze | Volume 0.0006, `fogSky` 1 over 3.2 km, max distance 4 km | The land dissolves into the sky's own colour, so there is no edge and no black |
| Olive | Five raymarched SDF objects (`trunk` with braided strands and roots, `limb0..2`), compiled, 90-94 nodes each | Each limb is in its own frame: `bend` (wax), its node position (detach), `displaceField(rot)` (eaten), `displaceNoise` with `speed` (the bark flows). Exact shadows (ADR-1160) |
| Tanguy object | One SDF, 27 nodes: a smooth union pierced by a cylinder, a filament, a bead, a needle | Hovers 0.11 m above its shadow (S8); glossy, so it picks up the strain's light |
| Double | The same form without `eaten`, parked under the land until a preset places it | P2 |
| Blocks | Two `points` procedurals: the olive's shell (900 cubes of 0.16 m) and the Tanguy object's (584 cubes of 0.2 m), computed in Python from the same skeletons | The surface's own representation: they appear where the surface is eaten, then lift, tumble and float |
| Motes | Particles (disc, 45 m) | Dust in the dream light; spores later |
| Liquid skin | A `procedural` box 34 x 0.02 x 34 m, 64 subdivisions, lying 1 cm under the pan's flat heart; two world-space field deformers: `liquidKick` (the kick's onset front x the `pool` mask) and `liquidSwell` (slow noise x `pool`, amount on the bass) | Terrain cannot deform (fact 11), so the brief's liquid land (§8) is a skin the land covers at rest: only crests rise out of the ground. The `pool` mask (1 inside 9 m of the centre, 0 at 16 m) stays inside the skin's 17 m half-width, so its own edge stays under the land. Parked 30 m down in the stages where land is solid |
| Fracture light | Point light in the strain's colour at the Tanguy object, `volumetric 1` | The strain reaches the light, the land, the tree, the haze and the object's specular |

## The camera

The camera is free (mode 1). `VANTAGES` gives each stage two or three (eye, target, fov, roll) placed on the land.
Eyes are lifted onto the engine's terrain height, and `check_moves()` probes the land along every straight move
between them. A stage's vantages are states (`Dream`, `Dream 2`, ...), and the music moves the camera between them:

- in the Dream, Uncanny and Infection, on every phrase;
- in the Corruption, every 4 bars;
- in the Nightmare, every 2 bars.

Moves get longer early and abrupt late. Each one is `idle` (ADR-1164), so it never interrupts a stage's own morph,
and quantised to the beat. Three LFOs (0.031, 0.0197 and 0.047 Hz) float the eye and the aim between moves, more
deeply as the energy grows.

## Corruption as a world property

There is one field, `rot`, behind every corruption:

```text
contagion (scalar grid 128 x 1 x 128 over 128 m, ceiling 1 -- ADR-1163)
   injected at the stone (field `infect`, strength staged and pulsed by the snare)
   advected along curl noise + outward from the stone (field `flow`), diffused, dissipated (the heal)
stain = grid(contagion)                    -- the plain reads this
climb = plane at height h, inverted        -- h rises with the stage: the infection climbs the tree from the root
rot   = stain x climb                      -- one compound level (a compound in a compound is 0 on the GPU)
```

| Consumer | Reads | Effect |
|---|---|---|
| Plain program | stain (+ stuck) | A cell 0.45 m square fails when the stain passes its own random (ADR-1162 `quantize`): a stain with a macroblock edge. Base colour toward dark magenta; emission on hats and the kick's front |
| Bark and skin programs | rot (+ stuck) | The same rule, at 0.17 m and 0.2 m |
| Tree and stone SDF | rot, via `displaceField` (`eaten` amount) | The smooth surface recedes where it is eaten |
| Blocks | rot (Scale × Multiply) | The blocks appear where the surface recedes |
| Fracture light | stage, kick | The magenta spills onto everything near the stone, including the haze |
| Motes | stage, treble | The stained ground releases spores |

## The arc (layer 4)

The arc was tuned by simulation, then verified through the engine with `arc.py` on both tracks.

```text
drive    = clamp(3 x mean(audio.energy, audio.trebleLevel) - 0.95)      loudness-independent energy (ADR-897) + air
energy   = follower(drive, attack 1.5 s, decay 3 s)                     macros/energy
baseline = follower(drive, 30 s)                                        macros/baseline
lift     = clamp(0.5 + 3 (energy - baseline))                           the music against its own last half minute
pace     = clamp(0.3 energy + 0.8 lift - heal - 0.05)                   heal: 0.45 in a respite, 1 in the keyframe
dose    += (-0.05 + 0.0605 x pace^0.25) dt, bounded to [0, 1]           ADR-1161
depth    = dose x (1 - keyframe)                                        what the ladder reads
```

Why this shape:

- **Mastered music carries structure in relative change, not absolute level.** Over 10-second means, both tracks
  move within about 0.44-0.65. `lift` is the song against itself, so a breakdown is a fall below the song's own
  baseline, whatever its loudness.
- **Any music must corrupt the dream eventually.** Typical music advances the dose (Dream to Collapse in about three
  minutes), intense music advances it faster, and a breakdown heals it. The power curve makes pace 0 a hard drain
  (the keyframe) while ordinary pace still advances.
- **Hold ladder (ADR-1164).** Every rung fires while `depth` is above its threshold (`from` the previous stage), so
  the machine cannot strand after a respite unmasks the dose.

**The stages, as the engine ran them** (`arc.py`, 2026-10-05):

| Feline Footwear | | Trench | |
|---|---|---|---|
| Dream | 0-50 | Dream | 0-21 |
| Uncanny | 50-81 | Uncanny | 21-37 |
| Infection | 81-110 | Infection | 37-63 |
| Corruption | 110-130 | **Respite** (its first breakdown) | 63-79 |
| Nightmare | 130-154 | Uncanny → Infection (re-infection) | 79-118 |
| Collapse (four strata) | 154-185 | Corruption | 118-137 |
| **Recovery** (its outro begins at ~185) | 185-210 | Nightmare | 137-168 |
| | | Collapse (four strata) | 168-200 |
| | | **Recovery** (its outro, ~198) | 200-208 |

Both songs collapse at their own climax and recover in their own outro, and nothing names either song.

## Live: the instrument

`digital-mosh-live.json` is the same scene and arc on live input (`sonic.live`) at LIVE AUTO. A performer has two
controls, bound in `control.midi`:

- **SENSITIVITY** (CC 1 → `macros/sensitivity`) scales the drive's depth (0 to 4). It trims the arc to the input level, the
  way a gain knob trims a channel: a quiet room never leaves the Dream at 0.25 and corrupts quickly at 1.
- **Stage pads** (General MIDI drum notes 36 to 43) force a stage: Dream, Uncanny, Infection, Corruption, Nightmare,
  Collapse, Respite, Recovery. They are ordinary signal triggers on the states (`control.pad<Stage>`). The arc then
  continues from the forced stage, because the dose is still the music's own.

`play_live.py` plays a track into BlackHole and records the host time it started, so a capture
(`--live-capture`) and a sonic log (`--sonic-live-log`) can be joined to the music afterwards.

## The art passes

| Pass | What changed | Why (what the review saw) |
|---|---|---|
| Build 1 | Flat plain, primitive tree, static camera, all corruption systems wired | Proved the systems; the owner found it crude and static |
| 1 | Terrain (dunes, pan, riverbed, mesas, ranges), the braided olive and the Tanguy object as SDF, painting palettes, the travelling camera, ADR-1164 `idle` | The owner: travel, landscape, real palettes |
| 1b/1c | The Bee's land band; a narrow growth front; mosh restraint; the collapse keeps the nightmare's colour; the zenith reaches the frame | Pass 1's full-song review: too much glitch too early, collapse went grey |
| 2 | Footprint-faded cells (no shimmer), moves on the beat, a shorter white, varied vantages (a tiny tree in a vast land), less drift; the liquid skin; the double's scale on lifts; MIDI; the front's glow faded under the lens | The Critic on pass 1 (shimmer, wobble, repeated compositions, cuts off the beat, clipped white) and the review of pass 2 (a floor of lit tiles under the camera, the skin's edge line) |
| 3 | The Dream's haze warm and thin (the Bee's horizon colour at 0.38 of the density) instead of a grey-blue wash; the liquid heaves gently, its mask tapering from 9 m to 16 m inside the 17 m skin (it had risen as a dark plateau, and a mask reaching past the skin lifted its edge); the Pixels stratum without posterize and with less sort | Pass 2's contact sheet: a grey Dream, a slab in the collapse, a garish poster-red frame at the Pixels stratum |
