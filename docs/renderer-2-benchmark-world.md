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
non-linearities in §7.

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
| `extent` | candidate instances, terrain chunk count, and how much of the world falls inside the fixed view distances | **super-linear and then non-monotonic** — see §7 |
| `composition.negativeSpace` | how much is deliberately emptied | weak: 0.28 → 0.00 moved visible instances from 595 to 623 (+4.7%) |
| `composition.focalStrength` | hero count, and through it the *number of ecological zones*, whose category multipliers are a ~3× swing on flora | **powerful and non-monotonic**, and it also changes hero scale and the camera stand-off, so it cannot be moved without changing the shot |
| `ecology.crystals` / `.creatures` / `.structure` | nothing. The Glowmere manifest has no species in those categories | dead knobs for this library |
| `atmosphere.fog`, `.spores`, `lighting.volumetric` | **nothing measurable.** They set fog and volume *densities*; the step count is set by the quality tier (`volumeSteps=32` at `realtime`) and does not move | art knobs, not stress knobs |

The last row is the one most likely to be assumed rather than checked. A recipe cannot make the
volumetrics more expensive; `--tier` and `--size` can.

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

<!--BASELINE-->

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

<!--DETERMINISM-->

Two further observations from the measurement pass, which are determinism evidence of a different
kind:

- The geometry counters were **identical in all five runs of every preset** — same visible
  instances, same draws, same shadow draws, same LOD distribution — while the frame times varied by
  a factor of four. The world is deterministic; the machine is not.
- The counters are also identical on every steady frame *within* a run. The composed world installs
  a free camera at the composer's viewpoint and nothing animates it, so a benchmark run is one
  fixed shot measured many times rather than a flythrough. That is the right shape for a benchmark
  and it is worth not accidentally losing.

The composition is **not** independent of `--size`: the reported `tris=` counter and the LOD
distribution both move with resolution (see §9). The *placement* does not.

## 8. What had to be capped, and why

**`glowmere-extreme` is 1100 m, not larger.** Three reasons, in order of how much they matter:

1. **The ladder stops being a ladder.** Visible instances against extent, at otherwise identical
   weights: 595 at 420 m, 1,352 at 840 m, 2,245 at 1000 m, **3,111 at 1100 m**, 1,564 at 1200 m,
   1,827 at 1300 m. It is not noise — the counts are stable to ±2 across runs. The composer picks
   its viewpoint by scoring twelve candidate bearings around the focal subject for the fertility of
   the ground they stand on, and the winning bearing changes as the terrain scales, so the *shot*
   changes. Past 1100 m the camera lands somewhere with less in front of it.
2. **A layer hits its ceiling.** `Grass_Common_Short` reaches the foreground band's
   120,000-instance cap somewhere between 840 m (80,109 placed) and 1000 m (capped). Above that,
   the world keeps growing and the ground cover thins: at 840 m the realised grass density is
   0.114/m², at 1600 m it is 0.047/m². A rung above the cap is not the same world with more in it.
3. **Practicality.** Composition and asset placement at 1100 m take about 1.5 s, and the first
   frame carries ~0.8 s of scene build. At 1800 m that becomes ~2 s and ~1.3 s, and the pre-cull
   geometry reaches 106.7 M triangles. A benchmark pass has to be cheap enough that people run it.

1100 m was chosen over the tidier 1680 m (4× medium) for reasons 1 and 2. It is the heaviest rung
that is still an extension of the ladder rather than a different world.

**Density per square metre could not be dialled above the shipped recipe at all**, for the reason
in §1: every weight is validated into 0..1 and Glowmere's are already at 0.95. The ladder therefore
runs from 0.16× to 1.0× on that axis and no further. Anyone wanting a genuinely denser square metre
of Glowmere has to change `preferredDensity` in the manifest, which is a change to the art and not
to the benchmark.

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
- **`tris=` is resolution-dependent, so the Phase 0 audit's §3 description of it is stale.**
  `glowmere-medium` reports 1,591,634 triangles at 1440×900 and 118,503 at 640×400, with the LOD
  distribution moving from 115/320/158/2 to 26/106/365/96. It is tracking the LOD the cull pass
  chose, which is what ADR-077's comment in `scene_renderer.hpp` says the counter now does — the
  pre-cull figure has moved to `geometry.logicalTriangles`, which nothing logs. The audit document
  describes the counter as it was before that change. Two consequences for anyone using this
  baseline: `tris` is only comparable between runs at the same `--size`, and the column cannot be
  compared against the audit's 15.1 M figure for the authored scene.
- **The composer's ecological zones are sized in absolute metres and do not scale with `extent`.**
  `zone.radius = max(extent × 0.13, hero.activationRadius × 0.8)`, and the activation radius comes
  from the hero's own height, which has nothing to do with the world's size. At 420 m the four
  zones (radius ≈281 m) blanket the entire world and their flora multipliers — 1.55, 0.55, 0.30,
  1.30 — compound to about 0.33 everywhere; at 1100 m they cover a fraction of it. That is most of
  why placed instances grow super-linearly with extent (4× the ground gives 10× the grass between
  medium and dense) and it is why `extent` is not the clean dial it looks like. Whether it is a
  defect or a deliberate absolute scale is a question for whoever owns the composer.
- **`atmosphere.fog`, `atmosphere.spores` and `lighting.volumetric` do not change what the
  volumetrics cost.** They set densities; the step count comes from the quality tier. A recipe
  cannot be used to stress the volume pass.
- **The composer creates no particle systems.** `particles=0sys/0cap/0emit` on every generated
  world, against `1sys/10240cap/11emit` on the authored `examples/world/glowmere-stylized.json`.
  `atmosphere.floating_elements` is accepted, validated and then used for nothing measurable. So
  these presets do not exercise the particle renderer at all, and a change to it will not show up
  here.

## 10. Not measured

Stated rather than quietly skipped.

- Anything above 1440×900 or below `--tier realtime`. The harness takes `--size` and `--tier`, and
  the audit already shows the scene pass is geometry-bound at editor resolution and becomes
  fragment-sensitive at 2880×1800; a resolution sweep across the four rungs is a natural next pass
  and was not run.
- Memory. Instance buffer bytes, texture and buffer residency are all unreported by the engine and
  nothing here estimates them.
- The offline render path. Every number here is `--headless` with the realtime tier.
- Whether the four rungs look right. They compose, install, render with zero GPU errors and produce
  byte-identical frames run to run; nobody has looked at the frames and said they are still
  Glowmere. The captures are one `--capture out.ppm` away.
