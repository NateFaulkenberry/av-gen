# Phase 5 — the heroes in the scene

Placement first, as the coordinator asked: a hero library nothing in the scene uses is a library.

**The original elder is gone.** A squashed sphere with two displacement deformers — which
`docs/stylized-glowmere.md` already listed as "an overly regular hero" — is replaced by six searched
mushrooms standing in habitat pockets along the river.

---

## 1. Placement

Six heroes × four parts = **24 generated nodes**, each a `PrimitiveKind::Generated` source carrying
the 18 parameter values, so a hero can be nudged in the editor and regenerate rather than being a
frozen mesh.

| hero | site | HAR | height |
|---|---|---:|---:|
| `elder-2` | the pool's east bank | 2.5 m | 16.0 m |
| `lantern` | upstream bend | 4.7 m | 6.5 m |
| `spire` | east bank, upper valley | 5.4 m | 4.2 m |
| `bloom` | west bank, mid valley | 5.2 m | 9.0 m |
| `veil` | east bank, lower valley | 5.3 m | 3.4 m |
| `umbra` | west shelf, lower valley | 6.3 m | 5.5 m |

Every site sits in the mesic riverbank band the Phase 3 ladder defines (HAR 2.5–6.3 m) on near-flat
ground, and they are spread down the whole course rather than clustered — the brief's §7 asks for
habitat pockets, so the river is a route past a series of them rather than one set piece.

The four routes that reached the old elder are **repointed, not dropped**: the section-scale swell to
the cap, the downbeat to the gills, which is the same split the original had between its crown and
its filaments. One is dropped honestly — `procedural/elder-filaments/distribution/radius` drove a
radial ring's spread on a drop, and a generated mushroom's gills are not a radial distribution, so
there is no equivalent parameter and inventing one would be a control with no effect.

## 2. Two art-direction overrides of the search's own output

The coordinator's prediction — *"the placement pass is where you will find out whether the weights
are good or the reviewer is not looking"* — resolved immediately, and in both cases the search was
not wrong so much as unable to see the question.

**One warm hero, and it is the elder.** Glowmere's reserve-accent rule — the hero is the only warm
light in the world, which is why the eye finds it from anywhere in the frame — does not survive six
warm mushrooms. The search chose each candidate's emission structure on its own merits; a scorer sees
one candidate at a time, and "is this the second warm thing in the frame" is a property of the *set*.
So the elder keeps its structure's warm accent and the other five are recoloured to the cool half of
the palette. Recorded here rather than folded in silently.

**The emitting parts carry no material program.** The first placed render came back with the elder's
gills glowing **green**, because `glowmereTissue` writes its own cyan-green emission constant and a
material program's emission overrides the material's. That is the reserve-accent rule broken by a
material rather than by a decision. The program's fresnel translucency is a real loss and is named as
one; colour control is worth more.

## 3. ADR-173's falsifier, run

The provisional sampler decision named its own overturning test and it has now been run.

| valid candidates | behaviour cells occupied of 81 |
|---:|---|
| 165 | 59 (72.8%) |
| 660 | 73 (90.1%) |
| 1,322 | 80 (98.8%) |

**The decision stands and the population changed.** At 165 samples coverage is *below* what uniform
sampling in uniform bins would give, which reads as a case against Sobol — but the behaviour
descriptors are ratios and products of the parameters, so their distribution is not uniform even when
the parameters are. The question ADR-173 actually posed was whether the empty cells are *reachable*,
and coverage climbing monotonically to 98.8% answers it: **they are reachable by sampling, merely
rare.** MAP-Elites would find them with fewer evaluations, which is a much weaker claim than the one
that would have justified building it.

The useful result is not about samplers at all: **at 200 candidates a quarter of behaviour space is
never sampled, and a diversity selection can only choose from what it was shown.** The default
population is now 800. That is a finding no amount of arguing about algorithms would have produced.

It also paid off the exact property Sobol was chosen for: growing 200 → 400 → 800 → 1600 cost nothing
and invalidated nothing, because every earlier candidate keeps its identity. Under Latin hypercube
each row of that table would have been a different sample and the comparison could not have been made.

## 4. The budget

Three interleaved runs, GPU lock held, `pgrep avgen` clean either side, 1280×800.

| | frame | scene pass |
|---|---:|---:|
| Phase 3 (no heroes) opening | 13.44 ms | 10.81 ms |
| **Phase 5 opening** | **13.70 ms** | 10.68 ms |
| Phase 3 valley axis | 12.78 ms | 9.63 ms |
| **Phase 5 valley axis** | **14.55 ms** | 11.21 ms |

**These two rows are not comparable and the deltas below were withdrawn in Phase 6.** They come from
separate process invocations, and `10-phase-6.md` §3 measures a 25% spread between invocations of one
binary over one byte-identical scene. Interleaved properly, the six heroes cost **+0.20 ms** on the
opening and **+0.66 ms** on the valley axis — a third of what this table implies. Every reading is
inside the 16 ms ceiling either way, which is why the conclusion survived a measurement that did not.

## 5. What Phase 5 did not do

- **Materials are still flat palette colours with an emissive structure.** The brief's §7 asks for
  cap colour variation, emissive veins, a subsurface approximation and wetness. What is there is the
  *structure* the light comes out of, which is the part that decides whether these read as organisms
  — but it is not the designed material the brief describes, and losing `glowmereTissue`'s fresnel
  (§2) moved backwards on translucency.
- **No audio-reactive behaviour is specific to the new heroes** beyond the two repointed routes.
- **The upland reads bare** in wide shots. The Phase 3 ladder took groundcover off the upper slopes
  and the negative space is now arguably too generous on the east side.
