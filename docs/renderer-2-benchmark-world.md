# Renderer 2.0: the benchmark world

**What this is:** four recipes that compose the same valley at four densities, a harness that
measures them, and the numbers the pair produced on one machine on one build. It exists so that a
renderer change can be *shown* to help or hurt (§60), and it uses Glowmere rather than a grid of
spheres because the brief says to (§68) and because the geometry a real world submits looks nothing
like the geometry a test scene submits.

**What this is not:** a diagnosis. Nothing here says what the renderer should do differently. It is
an instrument and a baseline; reading conclusions out of it is a later phase's job.

Related: [renderer-2-architecture.md](renderer-2-architecture.md) for the Phase 0 audit,
[world-editor.md](world-editor.md) §1 for what every recipe field means,
[world-performance.md](world-performance.md) for what a world costs in general.

---

## 1. The four presets

`examples/recipes/glowmere-{low,medium,dense,extreme}.recipe.json`. All four name
`art.profile: "glowmere"` and `assets/glowmere.manifest.json`, carry the same seed
(19740211), and are identical in every art field — palette, emission, fog, spores, haze, moon,
bioluminescence, volumetrics, bounce, organic motion, chaos — and in `negative_space` (0.28) and
`focalStrength` (0.8). They are the same place.

| | `glowmere-low` | `glowmere-medium` | `glowmere-dense` | `glowmere-extreme` |
|---|---|---|---|---|
| `extent` | 420 m | 420 m | 840 m | 1100 m |
| ground | 0.18 km² | 0.18 km² | 0.71 km² | 1.21 km² |
| `composition.foreground` | 0.38 | 0.95 | 0.95 | 0.95 |
| `composition.midground` | 0.32 | 0.80 | 0.80 | 0.80 |
| `composition.background` | 0.22 | 0.55 | 0.55 | 0.55 |
| `ecology.flora` | 0.38 | 0.95 | 0.95 | 0.95 |
| `ecology.fungi` | 0.30 | 0.75 | 0.75 | 0.75 |
| `ecology.rock` | 0.16 | 0.40 | 0.40 | 0.40 |
| **what it dials** | density per m², ×0.16 | the reference | ground, ×4 | ground, ×6.9 |
| instances placed | 1,665 | 10,123 | 99,515 | 160,766 |
| instances visible | 94 | 595 | 1,352 | 3,111 |
| terrain chunks | 121 | 121 | 441 | 784 |
| frame, 1440×900 | 10.7 ms | 14.1 ms | 27.6 ms | 33.6 ms |

(The last four rows are measured; §6 has the conditions, the per-run spread and the caveats.)

Two knobs are in play and only two, one per rung-pair:

- **low → medium is density per square metre.** Every weight is the shipped `glowmere.recipe.json`
  value multiplied by 0.40. A layer's density is the product of *two* weights (its ecology weight
  and its band weight), so scaling both by 0.40 scales every layer by 0.16 — and scaling them all
  by the same factor leaves the proportions between the thirteen species exactly as they were. Low
  is medium thinned, not medium rearranged.
- **medium → dense → extreme is how much ground there is.** The weights do not move at all, so the
  per-square-metre density and the species mix are unchanged; the extent grows. The mid and
  background view distances are fixed in metres (220 m and 620 m), so a larger world puts more of
  itself inside them. The terrain grows with it: 121 chunks at medium, 441 at dense, 784 at
  extreme.

`glowmere-medium` is byte-for-byte the density of the shipped `glowmere.recipe.json`. That is
deliberate: the reference rung is the world av-gen actually ships, so a change that moves it has
moved something a user would see.

**Why the ladder is not one knob.** Every weight in a recipe is validated into 0..1, and Glowmere's
largest is already 0.95 — so the whole headroom above the shipped recipe is 1.11× on the foreground
and 1.82× on the background. Composed at 420 m with every weight pinned at 1.0, Glowmere reaches
746 visible instances; the authored scene the Phase 0 audit measured reaches 2,340. **The recipe
format cannot express a world heavier than about a third of the authored Glowmere at a fixed
extent.** Reaching a rung that hurts therefore requires `extent`, and `extent` comes with the
non-linearities in §8.

## 2. Why this world and not a grid of spheres

§68 is right, and the reason is visible in the counters. The Glowmere manifest has thirteen species
spanning 114 triangles (a pebble) to 9,134 (a twisted pine) and 0.00055/m² to 0.48/m². They land in
three depth bands whose tuning the composer fixes in `world_composer.cpp`:

| band | species | view distance | min screen radius | instance ceiling | casts shadow |
|---|---|---|---|---|---|
| foreground | Grass_Common_Short, Fern_1, Flower_3_Group, Mushroom_Common, Mushroom_Laetiporus, Pebble_Round_2 | 90 m | 1.4 px | 120,000 | no |
| midground | Bush_Common, Plant_1_Big, Mushroom_Common_Beacon, Rock_Medium_1 | 220 m | 2.4 px | 40,000 | yes |
| background | CommonTree_1, TwistedTree_2, DeadTree_1 | 620 m | 1.0 px | 6,000 | yes |

That table is why the presets can stress things separately. The foreground band is an
instance-count problem — tens of thousands of 155-triangle objects, none of which casts a shadow.
The background band is a triangle problem — a few hundred 6,000–9,000-triangle objects, all of
which do. A benchmark of one mesh instanced N times cannot tell those two apart, and the audit says
the scene pass is geometry-bound, which is precisely the regime where the difference decides
whether a change helps.

## 3. How a recipe becomes density

From `src/world/world_composer.cpp`, per species:

```
density = asset.preferredDensity          (the manifest's, per square metre)
        x ecologyWeight(asset.category)   (recipe.ecology.flora / .fungi / .rock)
        x bandWeight(bandForAsset(asset)) (recipe.composition.foreground / .midground / .background)
```

The band is the manifest's `foreground` / `midground` / `background` tag when there is one, and
otherwise follows the species' height (under 1.5 m foreground, over 6 m background). Every species
in the Glowmere manifest carries an explicit tag, so the bands above are fixed and no weight can
move a species between them.

`biomesFor()` then splits that density across biomes by category, and `Ecology::place()` samples a
grid whose cell is `sqrt(1 / peakDensity)` and accepts at most one instance per cell with
probability `density x cellArea x clustering x clearances`. The count is therefore **linear in the
recipe's weights** — doubling a weight halves the cell and doubles the cell count — but the
positions are *re-sampled*, not added to. Two rungs of the ladder are the same world at different
densities; they are not nested sets of the same individual plants.

Between the nominal density and the placed count sits about a factor of ten, and it is worth
knowing where it goes, because it is the reason a perfectly reasonable-looking recipe composes a
sparse world. For `Grass_Common_Short` at `glowmere-medium` (nominal 0.48 × 0.95 × 0.95 =
0.433/m², over 176,400 m² = 76,400 expected, 7,811 placed):

| stage | factor |
|---|---|
| biome coverage — the composer plants grass meadow-first, and the terrain is 28% meadow, 11% marsh, 39% forest | ≈0.54 |
| ecological zones — four zones anchored on heroes, whose flora multipliers are 1.55, 0.55, 0.30 and 1.30 | ≈0.33 |
| void regions and the viewpoint corridor | ≈0.8 |
| clustering (`clustering` 0.55 on the foreground band) | ≈0.8 |

The composer logs the first of these itself (`biome coverage: marsh 11%, forest 39%, meadow 28%,
scree 0%, rim 22%`). It logs none of the others, which is why a recipe's numbers and its world feel
unrelated until somebody multiplies them out.

## 4. The knobs, and what each one moves

§60 asks that each stressor be dialable independently. They are — each is a separate field in a
plain JSON file — but they are not equally honest, and the difference matters more than the list.

Measured by composing one preset with one field changed and reading the counters (`--frames 20`,
which is enough: the geometry counters do not depend on the frame rate and are identical on every
steady frame).

| knob | what it stresses | behaviour |
|---|---|---|
| `composition.foreground` × `ecology.*` | procedural instance count, cull dispatch, scene-pass draws. **Not** shadows: the foreground band's `castsShadow` is false. | linear and clean, but capped at 1.0 against a shipped 0.95 |
| `composition.background` × `ecology.flora` | submitted triangles and shadow draws: the three tree species are 6,169–9,134 triangles each and all cast | linear and clean; 1.82× of headroom above the shipped recipe |
| `composition.midground` × `ecology.*` | instances *and* shadow draws | linear and clean; 1.25× of headroom |
| `ecology.rock` | inert, non-emissive instances — the only way to add geometry without adding light | linear; 2.5× of headroom, the most of any weight |
| `extent` | candidate instances, terrain chunk count, and how much of the world falls inside the fixed view distances | **super-linear and then non-monotonic** — see §8 |
| `composition.negativeSpace` | how much is deliberately emptied | weak: 0.28 → 0.00 moved visible instances from 595 to 623 (+4.7%) |
| `composition.focalStrength` | hero count, and through it the number *and size* of the ecological zones that suppress flora across most of the map | **the strongest and the most treacherous knob in the format** — 4.7× on placed instances, non-monotonic on visible ones, and it moves the camera. See below. |
| `ecology.crystals` / `.creatures` / `.structure` | nothing. The Glowmere manifest has no species in those categories | dead knobs for this library |
| `atmosphere.fog`, `.spores`, `lighting.volumetric` | the volume pass's *step count*: not at all. Its cost: **unresolved** — see below | the one thing here that could not be measured cleanly |

### Measured, one field at a time

`glowmere-medium` with exactly one field changed, composed at 1440×900. Counters only — these do
not depend on how fast the machine was.

| change | placed | visible | `tris` | draws |
|---|---:|---:|---:|---:|
| *(none — `glowmere-medium`)* | *10,123* | *595* | *149,393* | *73* |
| `composition.foreground` 0.95 → 1.00 | 10,597 (+4.7%) | 637 (**+7.1%**) | 149,191 (−0.1%) | 73 |
| `composition.midground` 0.80 → 1.00 | 10,247 (+1.2%) | 597 (+0.3%) | 149,898 (+0.3%) | 72 |
| `composition.background` 0.55 → 1.00 | 10,213 (+0.9%) | 602 (+1.2%) | 167,195 (**+11.9%**) | 73 |
| `ecology.rock` 0.40 → 1.00 | 10,826 (**+6.9%**) | 602 (+1.2%) | 149,679 (+0.2%) | 75 |
| all six density weights → 1.00 | 12,339 (+21.9%) | 746 (+25.4%) | 201,372 (+34.8%) | 74 |
| `composition.negativeSpace` 0.28 → 0.00 | 11,752 (+16.1%) | 623 (+4.7%) | 172,346 (+15.4%) | 73 |
| `composition.negativeSpace` 0.28 → 0.60 | 9,722 (−4.0%) | 595 (0.0%) | 149,393 (0.0%) | 73 |
| `composition.focalStrength` 0.8 → 0.2 | 28,037 (**+177%**) | 1,648 (**+177%**) | 320,988 (+115%) | 70 |
| `composition.focalStrength` 0.8 → 1.0 | 9,227 (−8.9%) | 590 (−0.8%) | 148,853 (−0.4%) | 74 |

Three things fall out of it.

**The bands really are separable.** Raising `composition.background` alone moves submitted
triangles by 11.9% and visible instances by 1.2%; raising `composition.foreground` alone moves
visible instances by 7.1% and triangles by nothing. One dials triangle mass, the other dials
instance count, and a change to the renderer that trades one against the other will show the trade
here.

**`composition.focalStrength` is the strongest density control in the format, and it is a trap.**
Swept, at `glowmere-medium` and otherwise unchanged:

| `focalStrength` | 0.0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.6 | 0.8 | 1.0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| heroes | 1 | 2 | 2 | 3 | 3 | 4 | 5 | 6 |
| placed | 43,823 | 28,586 | 28,037 | 14,351 | 13,278 | 11,572 | 10,123 | 9,227 |
| visible | *10,742* | 1,648 | 1,648 | 483 | 482 | 596 | 595 | 590 |

Placed instances vary by 4.7× across a control nobody would reach for to change density, and
visible instances are *not monotonic* in it. The mechanism, read from `world_composer.cpp`: the
hero count is `1 + round(focalStrength × 5)`, each of the first four heroes anchors an ecological
zone whose flora multipliers are 1.55, 0.55, 0.30 and 1.30 in that fixed order, and each zone's
radius comes from its hero's activation radius — which grows with the hero's height, which grows
with `focalStrength`. So a lower value gives fewer zones *and* smaller ones, and the suppression
they compound to lifts off most of the map. The visible count then moves independently again
because the camera's stand-off is tied to the landmark's height, which `focalStrength` also sets.

The italicised 10,742 is not comparable to the rest: at `focalStrength` 0 the composer emits no
focal region at all, so there is no corridor and **no viewpoint is chosen** — the camera stays
wherever the default composition put it. A recipe with `focalStrength: 0` renders a world nobody
arranged a shot for.

`focalStrength` is held at 0.8 on all four rungs for exactly these reasons. Do not use it to dial
density.

**`composition.negativeSpace` moves placement more than it moves the frame**: 16% more placed for
4.7% more visible. What it takes away is mostly not in shot.

### The atmosphere weights: what could and could not be shown

`atmosphere.fog`, `atmosphere.spores` and `lighting.volumetric` set fog and volume *densities*. The
obvious question is whether a recipe can therefore be used to make the volumetrics expensive. Two
answers, one solid and one not:

**Solid: the step count does not move.** `volumeSteps=32` at `--tier realtime` with all three
weights at 0.0 and with all three at 1.0 — the engine's own `workload:` line, identical in both.
The march length is the quality tier's decision, not the recipe's.

**Not solid: whether the pass itself costs more.** Three interleaved repeats of `fog=spores=
volumetric=0.0` against `=1.0` against `glowmere-medium` gave volume-pass medians of 3.74/2.88/2.03,
3.54/2.69/2.23 and 4.78/6.82/2.23 ms respectively — three fully overlapping distributions, on a
machine whose `acct` never rose above 0.65. **No difference was demonstrated, and none was excluded
either.** This is the one measurement in this document that could not be taken cleanly; a windowed
`avgen` belonging to other work held the GPU for the whole attempt. It is owed a repeat on a quiet
machine.

So: treat these three as art knobs until somebody shows otherwise, and reach for `--tier` and
`--size` when the volumetrics are what you want to stress. Do not treat "a recipe cannot make the
volumetrics more expensive" as established — it is not.

## 5. Running it

```sh
tools/render_bench.py                                  # the four rungs, 3 interleaved repeats
tools/render_bench.py low medium --repeats 5
tools/render_bench.py --frames 300 --size 2880x1800
tools/render_bench.py --wait-idle 600                  # wait for other agents' builds to finish
tools/render_bench.py --json out.json                  # every run, machine-readable
tools/render_bench.py examples/world/glowmere-stylized.json    # a project, for comparison
```

Standard library only. It shells out to

```sh
./build/release/src/avgen --headless --generate <recipe> --frames 180 --fps 30 \
    --size 1440x900 --tier realtime
```

(`--project` instead of `--generate` when the target is a project file) and parses the engine's own
`frame wall clock`, `gpu frame median` and `draws=` lines. It adds no reporting to the renderer,
deliberately: a benchmark that needs an engine change before it can measure anything arrives after
the change it was meant to evaluate.

Three things it does that a shell loop does not, each because the shell loop gave a wrong answer
first:

- **It interleaves.** Runs go round-robin across the presets. A laptop under sustained load drifts,
  and whichever preset is measured last inherits all of the drift as if it were a property of that
  preset. Interleaving does not remove the drift; it stops the drift from having a preferred
  victim.
- **It prints every run.** A median of three has no error bar, and a reader cannot tell a 5%
  regression from a noisy machine without one. The summary carries the peak-to-peak spread.
- **It checks what silently invalidates a pass**: a non-zero GPU error count, competing `avgen` or
  compiler processes before *and* after each run, and the benchmarked binary changing mid-pass.
  All three happened while this document was being written.

The `acct` column is the pass medians' sum over the GPU frame median. The timeline's intervals
partition the frame, so on an idle machine it sits near 1. When it collapses — 26 ms of passes
inside a 105 ms frame — the frame spent its time outside every measured pass, which on a shared GPU
means somebody else's work. It is the column that says whether to believe the others.

## 6. The baseline

### How these numbers were taken

Apple M2 Max, release build, headless, `--tier realtime`, 1440×900, 180 frames at a fixed 30 Hz
simulation clock with the first 12 dropped by the engine as warm-up. Zero GPU errors on every run.

**The binary was pinned.** Other work was changing the renderer during this session — the in-tree
`build/release/src/avgen` changed hash once mid-measurement, as ADR-077's submitted-primitive
counting landed. Every number below comes from one copy taken before the pass, sha256
`ac58e269d6df…`, and the harness re-hashes the binary at the end of a pass and says so if it moved.

**The machine was not quiet.** Several agents were working in this repository at once: windowed
`avgen --play` sessions and a full parallel build of the test suite. The harness waits for an idle
machine before each run and marks any run that had company at either end; contended runs are
excluded from the medians and kept in the per-run table. Two full passes were taken.

```sh
# pass 2, the one quoted below
tools/render_bench.py low medium dense extreme examples/world/glowmere-stylized.json \
    --repeats 3 --frames 180 --size 1440x900 --wait-idle 900 \
    --binary /tmp/avgen-bench --json bench-2.json
```

### The ladder

Pass 2. `glowmere-stylized` is the authored scene, included as an anchor rather than as a rung —
it is not composed from a recipe and is not part of the ladder.

| preset | wall | p10 | p90 | spread | gpu | scene pass | draws | shadowDraws | visible | placed | chunks | `tris` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `glowmere-low` | **10.71** | 10.16 | 15.68 | 5% | 8.39 | 5.31 | 63 | 128 | 94 | 1,665 | 121 | 65,476 |
| `glowmere-medium` | **14.14** | 13.49 | 17.89 | 5% | 11.67 | 8.13 | 73 | 130 | 595 | 10,123 | 121 | 149,393 |
| `glowmere-dense` | **27.64** | 25.84 | 29.16 | 1% | 25.23 | 19.99 | 154 | 143 | 1,352 | 99,515 | 441 | 949,174 |
| `glowmere-extreme` | **33.63** | 31.77 | 35.38 | 1% | 31.20 | 24.44 | 178 | 157 | 3,111 | 160,766 | 784 | 1,665,438 |
| *glowmere-stylized (authored)* | *26.41* | *25.25* | *29.28* | *1%* | *23.62* | *20.51* | *143* | *121* | *2,340* | *113,560* | *256* | *636,399* |

Milliseconds. `spread` is the peak-to-peak of the per-run medians over their median. `visible` is
the engine's `visibleInstances`; `placed` is the sum of the composer's own `scatter '…' placed N
instances` lines. `tris` is the engine's `tris=` counter at this resolution and is not comparable
across resolutions (§9). The engine's `culledInstances` is not `placed − visible` — at
`glowmere-dense` it reports 101,822 against 99,515 placed — so it is quoted here only as the raw
counter it is.

### They still look like Glowmere

Captured at 960×600 and looked at, because a benchmark world that has quietly stopped being the
world it claims to benchmark is worse than no benchmark:

```sh
./build/release/src/avgen --headless --generate examples/recipes/glowmere-<rung>.recipe.json \
    --frames 30 --fps 30 --size 960x600 --tier realtime --capture shot-<rung>.ppm
```

All four are the same cool alien night: the same palette, the same species, the same beacon-mushroom
hero at the same size in frame, and a ladder that reads visually as sparse → planted → wooded →
closed understorey under a canopy. `glowmere-low` is bare but not broken — a meadow with things in
it rather than an empty plane. `glowmere-extreme` is the closest of the four to the shipped
authored scene's look. At `glowmere-medium` a foreground frond stands partly across the hero; that
is the composer's corridor doing its job imperfectly, it is identical on every run, and it does not
affect anything measured here.

**`glowmere-extreme` is heavier than the authored Glowmere**: 33.6 ms against 26.4, 3,111 visible
instances against 2,340, 178 draws against 143. The top rung is not a synthetic torture test that
nothing resembles; it is the shipped world's own art direction pushed past the shipped world.

### Every run

Pass 2, in the order taken. Interleaved, so the rows are not grouped by preset in time.

```
preset             run   wall    p10    p90    gpu  scene  acct  visible  draws  err
low                  1  10.23  10.00  15.20   8.32   5.31  1.01       94     63    0
low                  2  10.73  10.16  16.76   8.39   5.37  1.02       94     63    0
low                  3  10.71  10.16  15.68   8.39   5.31  1.01       94     63    0
medium               1  14.87  13.31  19.00  11.86   8.39  1.01      595     73    0
medium               2  14.14  13.50  15.90  11.67   8.13  1.01      595     73    0
medium               3  14.10  13.49  17.89  11.67   8.13  1.01      595     73    0
dense                1  27.64  25.82  29.16  25.23  19.99  1.00     1352    154    0
dense                2  27.64  26.41  29.39  25.36  20.12  1.00     1352    154    0
dense                3  27.49  25.84  28.71  25.23  19.99  1.00     1352    154    0
extreme              1  33.67  31.77  35.35  31.26  24.64  1.00     3111    178    0
extreme              2  33.63  31.74  35.38  31.20  24.44  1.00     3111    178    0
extreme              3  33.39  31.97  36.32  30.74  23.99  1.00     3111    178    0
glowmere-stylized    1  28.43  26.38  33.40  23.66  20.64  1.01     2340    143    0  busy
glowmere-stylized    2  26.53  25.23  29.40  23.79  20.64  1.00     2340    143    0
glowmere-stylized    3  26.28  25.27  29.16  23.46  20.38  1.00     2340    143    0
```

### The pass breakdown

Medians over the same window, in milliseconds, from the run quoted above.

| preset | scene | shadow | volume | depth | cull | ao | everything else |
|---|---:|---:|---:|---:|---:|---:|---:|
| `glowmere-low` | 5.31 | 0.26 | 2.03 | 0.07 | 0.26 | 0.26 | ≈0.3 |
| `glowmere-medium` | 8.13 | 0.46 | 2.03 | 0.13 | 0.33 | 0.33 | ≈0.3 |
| `glowmere-dense` | 19.99 | 1.51 | 2.03 | 0.79 | 0.39 | 0.26 | ≈0.3 |
| `glowmere-extreme` | 23.99 | 2.49 | 2.03 | 1.11 | 0.39 | 0.33 | ≈0.3 |

The volume pass is 2.03 ms on all four rungs — all four carry identical atmosphere and lighting
weights, so this says the volumetrics are indifferent to how much geometry is in front of them,
which is what a screen-space effect should be. Whether they respond to the *atmosphere* weights is
a separate question and is answered in §4.

### How reproducible is this?

Two independent passes were taken, hours apart, on a machine doing different things each time
(pass 1: five repeats, three of twenty runs contended and excluded; pass 2: three repeats, one of
fifteen excluded).

| preset | pass 1 wall | pass 2 wall | difference |
|---|---:|---:|---:|
| `glowmere-low` | 14.75 | 10.71 | **−27.4%** |
| `glowmere-medium` | 16.36 | 14.14 | **−13.5%** |
| `glowmere-dense` | 27.79 | 27.64 | −0.5% |
| `glowmere-extreme` | 33.56 | 33.63 | +0.2% |

**This is the most important row of numbers in the document, and it is not the ladder.** The two
heavy rungs reproduce across passes to within half a per cent. The two light rungs do not reproduce
at all: `glowmere-low` moved by 27% between passes on a machine that was merely busier, and its
within-pass spread in pass 1 was 38%.

The reason is visible in the ladder: `glowmere-medium` has 6.3× the visible instances of
`glowmere-low` and costs 32% more per frame. Whatever the bottom of the ladder is measuring, most
of it is not the geometry the preset was built to vary.

So, for using this as an instrument: **`glowmere-dense` and `glowmere-extreme` are the rungs a
regression should be judged on.** `low` and `medium` are useful for checking that a change did not
break a light scene, and for the geometry counters, which are exact at every rung. A frame-time
difference under about 5% at `low` or `medium` is not a result.

## 7. Determinism

A composition is a pure function of `(recipe, library)` — `hash01(seed, salt)` rather than a PRNG
stream, precisely so that adding a layer does not move everything after it. That is the design
claim; this is the check.

Each preset was composed and rendered twice, and both the composition log and the rendered frame
compared:

```sh
for t in low medium dense extreme; do
  for r in a b; do
    ./build/release/src/avgen --headless --generate examples/recipes/glowmere-$t.recipe.json \
      --frames 20 --fps 30 --size 640x400 --tier realtime --capture /tmp/det/$t-$r.ppm \
      2>&1 | grep -E "placed [0-9]+ instances|camera framed|biome coverage" > /tmp/det/$t-$r.txt
  done
  cmp /tmp/det/$t-a.txt /tmp/det/$t-b.txt && shasum -a 256 /tmp/det/$t-{a,b}.ppm
done
```

Every preset passed both checks. The composition logs compared byte for byte, and the captured
frames were identical:

| preset | frame sha256 (640×400, frame 20) |
|---|---|
| `glowmere-low` | `ca42383fe1d1e8c0f5c1bcd3afc62e9a229bd058ed2a6c719145f73d77ba3208` |
| `glowmere-medium` | `a15322fad842d5b7c975360e8bb69c0e16974d1ea44730804e423057bedb439c` |
| `glowmere-dense` | `fca27cabf7bc5386bab0d4c911c32756f1cb4d1810a048a52382f40f33d204b6` |
| `glowmere-extreme` | `bda76c46dd756a76a3e572c0e4415c112deea9577edffa1a42f156ed093a87f2` |

The frames being identical is a stronger result than the composition being identical: it says the
whole path — composition, placement, terrain, upload, culling, LOD, shading — is reproducible, not
only the seeded part.

**The camera is a function of the recipe too**, and it is worth writing down what it does across the
ladder, because a benchmark whose shot moves is not a benchmark. Read back with `--save-project`:

| preset | eye | target | stand-off |
|---|---|---|---|
| `glowmere-low` | 87.038, −1.162, 3.419 | 104.514, −1.852, 27.803 | 30.0 m |
| `glowmere-medium` | 87.038, −1.162, 3.419 | 104.514, −1.852, 27.803 | 30.0 m |
| `glowmere-dense` | 196.649, −3.466, 82.932 | 209.028, −4.894, 55.606 | 30.0 m |
| `glowmere-extreme` | 261.348, −5.231, 100.144 | 273.727, −6.777, 72.817 | 30.0 m |

`glowmere-low` and `glowmere-medium` are **the identical shot** — same eye, same target, to the
last bit — so the only thing that differs between them is how much grows in front of the camera.
`dense` and `extreme` stand somewhere else, because the viewpoint is chosen relative to a focal
subject whose position scales with the world; but their view *direction* is identical to each
other's to two decimal places, and all four frame their hero from exactly 30 m. The four rungs are
the same framing of the same valley at three sizes, not four different pictures.

One more invariant, and the cleanest evidence that the extent knob does not change the art: the
composer's own biome report is **identical at every extent** —
`marsh 11%, forest 39%, meadow 28%, scree 0%, rim 22%` at 420 m, 840 m, 1100 m and every extent
probed in §8. The terrain's noise frequencies are expressed as fractions of the world's span, so a
larger world is the same landform at a larger scale rather than a different one.

Two further observations from the measurement pass, which are determinism evidence of a different
kind:

- The geometry counters were **identical in all five runs of every preset** — same visible
  instances, same draws, same shadow draws, same LOD distribution — while the frame times varied by
  a factor of four. The world is deterministic; the machine is not.
- The counters are also identical on every steady frame *within* a run. The composed world installs
  a free camera at the composer's viewpoint and nothing animates it, so a benchmark run is one
  fixed shot measured many times rather than a flythrough. That is the right shape for a benchmark
  and it is worth not accidentally losing.

One thing determinism does *not* cover: the reported counters are a function of `--size` as well as
of the recipe. The placement is identical at every resolution — the same instances in the same
places — but `tris=`, the LOD distribution and the visible-instance count all move, because
screen-radius culling and LOD selection are screen-space decisions (§9). Compare runs at one size.

## 8. What had to be capped, and why

**`glowmere-extreme` is 1100 m, not larger.** Three reasons, in the order they matter.

**1. Past 1100 m the ladder stops being a ladder.** Visible instances against extent, at otherwise
identical weights, all measured on the pinned binary at 1440×900:

| extent | 420 | 840 | 900 | 950 | 1000 | **1100** | 1200 | 1300 | 1600 | 1800 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| visible instances | 595 | 1,352 | 1,557 | 1,921 | 2,245 | **3,111** | 1,564 | 1,827 | 2,172 | 2,282 |

1100 m is a local maximum and 1200 m falls off a cliff. This is not noise: the counters are stable
to ±2 across runs of the same preset. The composer picks its viewpoint by scoring twelve candidate
bearings around the focal subject for the fertility of the ground each stands on, and the winning
bearing changes as the terrain scales — so past 1100 m the camera lands somewhere with less in
front of it. The *shot* changed, not the world's density.

**2. A layer hits its ceiling.** `Grass_Common_Short` reaches the foreground band's
120,000-instance cap between 950 m (116,031 placed) and 1000 m (120,000, capped). `glowmere-extreme`
at 1100 m is over that line, so the ground cover is thinner underfoot than `glowmere-dense`'s:

| extent | 840 (`dense`) | 950 | **1100 (`extreme`)** | 1600 |
|---|---:|---:|---:|---:|
| grass placed | 80,109 | 116,031 | **120,000 (capped)** | 120,000 (capped) |
| realised density | 0.114/m² | 0.129/m² | **0.099/m²** | 0.047/m² |

13% thinner at `extreme` than at `dense` is small enough to live with and large enough to write
down. At 1600 m it would be less than half, which is where the rung stops being the same world.

**3. Practicality.** A 180-frame run takes 4.1 s at `low`, 4.8 s at `medium`, 7.5 s at `dense` and
8.8 s at `extreme`, including composition, asset placement and the first frame's scene build. A
default pass — four rungs, three interleaved repeats — is under four minutes on an idle machine.
That is cheap enough that people will actually run it, which is the only property of a benchmark
that matters more than its accuracy.

**Density per square metre could not be dialled above the shipped recipe at all**, for the reason
in §1: every weight is validated into 0..1 and Glowmere's are already at 0.95. The ladder therefore
runs from 0.16× to 1.0× on that axis and no further. Anyone wanting a genuinely denser square metre
of Glowmere has to change `preferredDensity` in `assets/glowmere.manifest.json`, which is a change
to the art rather than to the benchmark.

## 9. Found and not fixed

Everything below was measured while building this and is left alone, because the files it lives in
belong to other work.

- **The example browser cannot open a recipe, so the four presets are not in `examples/index.json`.**
  `Engine::loadFile` routes every `.json` that is not a composition to `loadProject`, which rejects
  a recipe with `'...glowmere-low.recipe.json' is not an avgen project`. Verified by adding an
  entry and opening it: `--example "…"` fails and headless start-up aborts. Adding four menu items
  that error when clicked is worse than adding none, so none were added. Making them reachable
  needs a branch in `src/app/engine.cpp` that recognises a world recipe and composes it — the same
  two calls `--generate` already makes. The presets are reachable today with
  `--generate examples/recipes/glowmere-<rung>.recipe.json`.
- **`tris=` is resolution-dependent, so it cannot be compared across `--size`, and the Phase 0
  audit's description of it no longer matches the build.** `glowmere-medium`, pinned binary, one
  preset, three resolutions:

  | `--size` | `tris=` | LOD distribution | draws |
  |---|---:|---|---:|
  | 640×400 | 118,503 | 26/106/365/96 | 82 |
  | 1440×900 | 149,393 | 115/320/158/2 | 73 |
  | 2880×1800 | 187,545 | 326/251/18/0 | 66 |

  It is tracking the LOD the cull pass chose, which is exactly what a submitted-primitive count
  should do. [ADR-077](decisions/ADR-077-frame-profiler.md) is the change that did it, and says so
  explicitly: "the headline `tris=` figure changes meaning. It was 15.1 M for Glowmere and is now
  the submitted count." `docs/renderer-2-architecture.md` §3 still describes the counter as it was
  before that, which was true when the audit was written — ADR-077 landed *during* this session,
  and is the reason the binary here is pinned by hash. Two consequences for this baseline: the
  `tris` column is only comparable between runs at the same `--size`, and it cannot be compared
  against the audit's 15.1 M figure for the authored scene.
- **`culledInstances` does not mean what its name suggests.** At `glowmere-dense` the engine
  reports `instances=1352v/101822c` against 99,515 instances actually placed by the composer, so
  `visible + culled` exceeds the population. Whatever the counter is summing — several views,
  several cascades, or something else — it is not "instances the camera cull rejected", and the
  table in §6 quotes it as the raw counter rather than deriving anything from it.
- **The composer's ecological zones are sized in absolute metres and do not scale with `extent`.**
  `zone.radius = max(extent × 0.13, hero.activationRadius × 0.8)`, and the activation radius comes
  from the hero's own height, which has nothing to do with the world's size. At 420 m the four
  zones (radius ≈281 m) blanket the entire world and their flora multipliers — 1.55, 0.55, 0.30,
  1.30 — compound to about 0.33 everywhere; at 1100 m they cover a fraction of it. That is most of
  why placed instances grow super-linearly with extent (4× the ground gives 10× the grass between
  medium and dense) and it is why `extent` is not the clean dial it looks like. Whether it is a
  defect or a deliberate absolute scale is a question for whoever owns the composer.
- **Whether `atmosphere.fog`, `atmosphere.spores` and `lighting.volumetric` change what the
  volumetrics cost is unresolved.** The step count is the quality tier's (`volumeSteps=32` at
  `realtime`, identical at 0.0 and at 1.0), but the attempt to measure the pass itself ran on a
  contended machine and the distributions overlapped completely. §4 has the numbers. Not fixed
  because it is not a defect — it is a measurement that is owed.
- **The composer creates no particle systems.** `particles=0sys/0cap/0emit` on every generated
  world, against `1sys/10240cap/11emit` on the authored `examples/world/glowmere-stylized.json`.
  `atmosphere.floating_elements` is accepted, validated and then used for nothing measurable. So
  these presets do not exercise the particle renderer at all, and a change to it will not show up
  here.

## 10. Not measured

Stated rather than quietly skipped.

- **A resolution or tier sweep of the timings.** Only the geometry counters were taken at 640×400
  and 2880×1800 (§9); every millisecond here is 1440×900 at `--tier realtime`. The harness takes
  `--size` and `--tier`, and the audit shows the scene pass becomes fragment-sensitive at
  2880×1800, so this is the obvious next pass.
- **Memory.** Instance buffer bytes, texture and buffer residency are unreported by the engine and
  nothing here estimates them.
- **The offline render path.** Every number here is `--headless` with the realtime tier.
- **Anything on a quiet machine.** Both passes were taken on a laptop with other agents working on
  it. The harness waits for idle and excludes contended runs, and the two heavy rungs reproduced to
  within 0.5% across passes — but nobody has run this on a machine doing nothing else, and the
  light rungs' 27% between-pass difference says that would be worth doing before anyone quotes
  `glowmere-low` as a baseline.
