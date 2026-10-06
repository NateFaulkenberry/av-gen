# DIGITAL MOSH: Implementation

How the design in `02-design.md` is built in the engine, what each system costs, and why it is built that way. The
scene source is `examples/digital-mosh/build.py`; the generated files beside it should never be edited by hand.

## Files

| File | What |
|---|---|
| `examples/digital-mosh/build.py` | Source of truth: palette, layout, the tree's skeleton, SDF trees, voxel shells, fields, the contagion grid, material programs, stages, the arc and the audio routes |
| `digital-mosh.scene.json` | The world (generated) |
| `digital-mosh.json` | Feline Footwear (the primary development track) |
| `digital-mosh-trench.json` | Trench (the contrasting track) |
| `digital-mosh-live.json` | LIVE: live input (`sonic.live`), the `live` block (LIVE AUTO), no file |
| `examples/digital-mosh/arc.py` | Any track's stage timeline through the real engine, no GPU (about 2 s per song) |
| `tests/integration/test_digital_mosh.cpp` | `[digital-mosh]`: every preset path, route target and macro target is a parameter |

## The world

| Element | Representation | Why that representation |
|---|---|---|
| Plain | Procedural box, 1000 m scaled ×6 (a size parameter clamps at 1000), with material program `plain` | One draw. The program carries the stain, its blocks and its glow |
| Haze | Volume `0.0011` with `fogSky 1` over 1.4 km, low anisotropy | The plain fades into the sky's own colour, so there is no edge and no line (S10, the owner's dead-space rule) |
| Tree | Raymarched SDF, compiled: about 30 capsules in three limb groups, each in its own frame | `bend` per limb (wax, §8), `translate` per limb (detach, §4), `displaceNoise` with `speed` (the bark flows: it liquefies), `displaceField(rot)` (the surface recedes where it is eaten). Exact shadows (ADR-1160) |
| Stone | SDF: three spheres, smooth union | `displaceField(rot)` too; a glossy material that picks up the magenta light |
| Double | The same stone, parked 400 m below the plain until a preset moves it | Appears without a pop: the shadow and the haze treat it like the original |
| Headland | SDF: a smooth union of spheres with low-frequency noise, 760 m away | The scale reference the plain needs (S1, S5), and the proof that the world is a place |
| Blocks | Two `points` procedurals (tree 573 cubes, stone 522), a one-cell voxel shell of each object's own SDF computed in Python | The surface's own representation: they appear exactly where the surface recedes, then lift, tumble and float. One system serves as blocks, fragments and pixels |
| Motes | Particles (disc, 40 m) | Dust in the dream light; later the infected ground's spores |
| Fracture light | Point light, magenta, at the stone; `volumetric 1` | The contagion's colour in the light, on the plain, on the tree, in the haze, and in the stone's specular |

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
