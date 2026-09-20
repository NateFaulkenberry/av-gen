# ADR-399: The particle and volumetric labs get fixtures, and one of them finds something on its first frame

- Status: Accepted (2026-09-20)
- Builds on ADR-261 (the lab suite's charter), ADR-182 (a probe that cannot fail proves nothing),
  ADR-387 (a correct value is not a reached value), ADR-015 (GPU particles), ADR-032/139/140 (the
  volume pass), ADR-395 (the particle determinism mitigations).

## Problem

Two of the suite's fifteen labs were entries in `src/labs/lab.cpp` with status `Planned`, no cases,
and a fixture pointing at somebody else's scene — the Particle Lab at `examples/qa/renderer-qa.scene.json`,
the Volumetric Lab at `examples/world/glowmere-atmospherics.json`. `docs/engineering-labs.md` §9 has
named Particle as a ready-to-staff Wave 2 lab since the suite was designed, and the World Effects
brief wants an effect-authoring and preview lab, which would otherwise be built from scratch beside
these two.

## Decision

Both are built, to the shape the eight genuinely implemented labs already have: a registry entry
with a `file.cpp:symbol` decision site, a dedicated fixture with its design argument in its own
`_note`, a `cases.json` of numbered cases each carrying a question and a prose expectation, an
overlay profile with a justification for every switch, a GPU test file, and a document.

### The fixtures, and the one rule both follow

A lab fixture's job is to make **one** subsystem's decision readable, and the hard part is what to
leave out.

`examples/labs/particle-vfx-lab.scene.json` — four systems, each isolating one decision of the
compute pipeline. No field forces, no wind, no attractors, no curl turbulence on the two systems the
counting cases read: this lab does not own the fields that push particles about (those are authored
scene data and the World Effects work's), and a fixture that stirred the counts with a noise field
would make every reading a joint measurement.

The design that matters is that **`metered` and `saturated` are the same emitter** — identical rate,
identical lifetime, capacities 8192 and 256 — so the control shares the frame with the measurement.
A control in the same frame cannot be explained away by anything about the frame: the exposure, the
tone curve, the camera, the seed. A control that needs a second run always can be.

`examples/labs/volumetric-atmosphere-lab.scene.json` — five identical unlit slabs at 4, 12, 24, 40
and 60 m in a homogeneous volume. Five samples of one exponential, and the near slab is the control:
at 4 m the air should barely touch it, so an arm in which the near slab moves as much as the far one
is measuring something that is not distance.

Height falloff, noise and anisotropy are all **off, deliberately rather than by omission**. Each is
multiplicative on density; a fixture carrying them cannot state the density at a point, and a lab
whose fixture cannot state its own input measures nothing. The arms that want them switch them on
through `scene/volume*` and read against this file as the zero.

## What the labs measured

### The Particle Lab, case 2

| system | capacity | alive at 1 s |
|---|---|---|
| `metered` | 8192 | ~1500 = spawnRate × lifetime |
| `saturated` | 256 | **256** — the same emitter, bound by `cs_emit`'s clamp to the free slots |

### The Volumetric Lab, case 2

Scene-linear luminance of the subject at each distance, density 0.02:

| 4 m | 12 m | 24 m | 40 m | 60 m |
|---|---|---|---|---|
| 0.7057 | 0.5647 | 0.3947 | … | … |

Monotone, and the far slab is below 80% of the near one, so the ladder is a ladder.

### The Volumetric Lab, cases 3 and 4 — the one worth quoting

Two independent routes to "the volume did not run": `--disable volume` removes a pass that exists,
and `volumeDensity = 0` means `VolumeRenderer::enabled` is false and no pass is encoded at all.

> pass disabled vs density zero: **identical (921600 bytes)**
> fog on vs pass disabled: 691200 of 921600 bytes differ

ADR-387 as a frame: the only proof a gate is a gate is that the gated frame is the ungated one. And
the second line is the guard that stops the first from being an agreement about nothing — a renderer
that had quietly stopped encoding the volume under every condition would pass the first assertion.

## The defect the Particle Lab found on its first render

The `burst` system drew nothing.

`registerParticleParameters` (`src/scene/particles.cpp`) registered `particles/<name>/burst` with a
hard-coded default of `0.0f`, while `spawnRate`, `spread` and `position` beside it were all seeded
from the scene's authored value. So `applyParticleParameters` overwrote the file's `burst` with 0 on
the first frame. The key parsed, was serialised back out by `particlesToJson`, and appeared in the
panel as a control — and did nothing.

The comment three lines below it, about `extent`, already describes this exact defect:

> It was a multiplier over the authored value, defaulting to 1.0, and that is what this defect was.

Same defect, one line up, unfixed. It is also `docs/engineering-labs.md` §8 item 6's shape — a
scene-file value silently overruled by the parameter registered for it — in a third parser.

Seeded from `s.burst` now. **Every scene that ships authors `burst: 0`**, so no existing frame moves;
the change is free today and correct tomorrow.

Measured, with only that one seed reverted: `burst alive after 30 frames: 0`. With it:
`> 1500`, and the control — the neighbouring system whose rate goes through a parameter that *was*
seeded correctly — non-empty in the same frame.

### What it cost to find, and what that says

Nothing. The fixture was written, rendered once, and one of its four systems was missing from the
frame. That is what a lab fixture is for and it is the argument for building the remaining three
placeholders: a scene whose every number is written down makes a wrong number visible, and no
amount of reading the parameter registration found this in the eighteen months it was there.

## Consequences

- `avgen --labs` now reports thirteen of fifteen labs as built. The three still `Planned` are
  Temporal, AOV and Integration; Animation and Visibility remain `InProgress`.
- The Volumetric Lab enters a post chain that Lighting and HDR have already measured, which is the
  serialisation `docs/engineering-labs.md` §9's Wave 3 asked for, kept.
- Two things are named as next arms rather than run: the step count against a **non-uniform**
  density (a homogeneous medium is the easy case for a raymarch — the integral is analytic and any
  step count lands near it), and the volume march against the shadow atlas, whose trigger ADR-360
  already set.
- One non-defect is written down rather than left as a question: the particle trail stride is still
  `frameIndex % stride`, so a ribbon's sample phase depends on where the render started. It is
  inside ADR-360's relaxation, it is a rate rather than a seed, and the alternative needs a frame
  rate the shader does not have.
