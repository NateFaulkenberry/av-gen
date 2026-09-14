# Phase 6 — the hero materials, and a measurement that was not one

Two results, and the second is worth more than the first.

---

## 1. The materials

Two new programs, and the heroes now have translucency *and* their own emission — the regression
Phase 5 recorded is closed.

**`glowmere2TissueWarm`** (`examples/materials/glowmere2-tissue.material.json`) — `glowmereTissue`'s
graph with two changes. The emission constant is the palette's reserved accent instead of cyan-green,
and the underside mask is widened from 0.12 to 0.9.

That second change is the one that mattered and it was not obvious. `glowmereTissue`'s emission is
gated by a **downward-facing gradient**, which is exactly right for a cap's underside and exactly
wrong for a gill blade: a blade's normal is *tangential*, so the mask read ≈ 0 and the elder's gills
came out as a thin amber line rather than a glowing fan. A material written for one surface does not
transfer to a structure standing perpendicular to it.

**`glowmere2Cap`** — mottling from two noise octaves into a violet ramp, roughness that follows the
mottle so the surface catches light unevenly, and a `fresnel` edge term so the rim carries a little of
the organism's own flesh light. The emission is deliberately faint: the gills are the structure the
light comes out of, and a cap glowing as brightly as its own gills is a lamp again.

### The engine finding underneath

The first attempt made the tissue's emission constant **white**, so the hue could come from
`instanceEmissive` and the *material* could decide — which is the clean fix and would have needed one
program instead of two.

It came back white. **A `distribution: single` procedural's `instanceEmissive` is not its material's
emissive colour.** So a material program that wants a hue has to carry one, which is the exact
mechanism that caused Phase 5's green gills, and the mitigation is structural rather than a fix:
there are now two tissue programs, one per colour family, instead of one that is wrong for five of six
heroes. The five cool heroes use the scene's own `glowmereTissue`, whose cyan-green already *is* their
colour; only the elder needs a warm variant.

Program slots: 7 of 8. `paintedGround` was dropped — `paintedGround2` replaced it in Phase 2 and
nothing named it any more.

**Honest assessment.** The surface is genuinely richer — mottling, roughness variation, an edge term,
grazing translucency. The emission reads less punchy than the flat version it replaced even after
raising the program's intensity to 6.0, because it is now modulated by masks rather than applied flat.
At hero distance it reads as a warm rim rather than a glowing fan. That is a real trade and it is
recorded rather than described as a win.

## 2. What the materials did *not* get

The brief's §7 also asks for emissive veins, a subsurface approximation and controlled wetness. None
of those is here. The `palette` op that draws `paintedCrown`'s noise-warped radial gill rhythm is the
obvious tool for veins and was not reached in this pass.

## 3. The measurement that was not one

The coordinator asked me to understand Phase 5's +1.77 ms valley-axis asymmetry before spending any
budget. The answer is that **there was no +1.77 ms**, and finding that out is the result.

An A/B was run: one invocation with the heroes, one with them hidden. It reported that **hiding six
mushrooms made the frame slower** — 11.14 → 14.22 ms on the opening. A causally impossible result is
a free diagnostic, so three identical runs of one byte-identical scene:

| run | frame ms | triangles |
|---|---:|---:|
| 1 | 10.945 | 252,996 |
| 2 | 11.272 | 252,996 |
| 3 | 13.697 | 252,996 |

**A 25% spread with nothing changed.** Every run held the GPU lock and passed `pgrep avgen` on both
sides, which is everything ADR-170 requires. So ADR-170's clause is a floor and not a protocol, and
the protocol is the one ADR-150 has used all along and I failed to carry over:

> **Arms must be interleaved inside one process.** A comparison between two invocations of this binary
> has a noise floor of about 3 ms — larger than most effects worth measuring.

Interleaved properly, in one process, three runs, the same question answers cleanly:

| view | with heroes | no heroes | the six heroes cost |
|---|---:|---:|---:|
| opening | 11.67 ms | 11.47 ms | **+0.20 ms** |
| valley axis | 10.22 ms | 9.57 ms | **+0.66 ms** |

The asymmetry is real and is a third of what was reported: the valley axis pays about 3× the opening,
which is consistent with more heroes covering more pixels and with nothing else. **Six procedurally
generated hero mushrooms cost under 0.7 ms**, and Phase 5's table is corrected in place.

The confounder behind the 25% is still uninstrumented — thermal state is the obvious candidate and
nothing records it — and it is now the third time it has surfaced: 15.93 vs 13.57 in Phase 3, this,
and ADR-151's deletion arm disagreeing with its own published figure.

## 4. What Phase 6 did not do

- **The bare upland** is untouched. It is a composition judgement I still think is right, and it
  wants a measured pass rather than a tweak, because that band is where a third of Phase 3's budget
  came from.
- **Veins, subsurface and wetness**, above.
- **Every frame time in this project's docs should be read with a ±3 ms band** unless it says it was
  interleaved. The conclusions all survive it — everything is under the ceiling by more than the
  noise — but the individual numbers do not deserve three significant figures.
