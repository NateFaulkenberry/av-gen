# ADR-344: The showcase came home, and the hero table was a photograph of the end

**Status:** Accepted
**Date:** 2026-09-18
**Supersedes:** ADR-340 (retired — the scene it decided no longer exists)
**Follows:** ADR-174 (the riparian ladder), ADR-182 (a probe that cannot fail), ADR-245 (a camera is
not the camera), ADR-264 (a scene file is not the state that runs), ADR-271 (a UI edit lands in the
project), ADR-300 (the pose layer stack), ADR-330 (a removal is a negative fact), ADR-333 (a
character kind is a list of considerers), ADR-334 / ADR-335 (the scale ladder), ADR-336 (the route
considerer), ADR-337 (root motion)

> *"why dont you update glowmere-valley-2-multicam.json to use the new animation system - the
> glowmere valley 3 terrain did not come out well and I dont feel like starting all over - let's
> just cut glowmere-valley-3 altogether"*
>
> and, on the framing: *"if you can make wider angles less wide that's all it needs - it's just too
> small a world for those real wide shots."*

ADR-340 built a second Glowmere to answer two questions. The owner kept the answers and cut the
scene. This is where the answers went.

---

## 1. What was deleted, and the one thing that was not

Gone: `glowmere-valley-3.{json,scene.json}`, `glowmere-valley-3-legacytrees.{json,scene.json}`,
`tools/make_glowmere_valley_3.py`, `tools/glowmere3_shot.py`,
`tests/unit/test_glowmere_valley_3.cpp`, the `examples/index.json` entry, and the fifth element of
`test_glowmere_scale.cpp`'s `kScenes`.

**`test_glowmere_scale.cpp` needed one line changed, and that is the whole vindication of how
ADR-340 wrote it.** Its tree line is derived from *"a layer that places something at least five
metres tall"* rather than from `{"pines", "canopy", "deadwood"}`. A name list would have thrown the
day valley 3 arrived with eight layers; instead the arm followed the eight layers into their new
home without being touched. Two `CHECK(scenesChecked == 5)` became
`CHECK(scenesChecked == kScenes.size())`, which is the same lesson one level up.

**The engine changes stay.** `decide`'s `stallSeconds`/`stallDistance` and `ground`'s `bodyRadius`
are opt-in, default to 0, and fix defects that were never valley 3's: a stalled decider could not
recover, and a `decide` character was not a body in the crowd field. ADR-340 is marked Retired
rather than deleted, with a new §0 that pins the findings that outlive the scene — the river, the
riparian gate, and the `investigate` lock-on.

## 2. The trees, and the gate re-measured on the film's own terrain

Eight layers over eight Quaternius models, recovered from the deleted generator. Measured
like-for-like against `glowmere-valley-2`, which shares this terrain byte for byte and keeps the
three-layer table — so **the sibling film is the control arm**, and ADR-340's purpose-built
`-legacytrees` scene was not needed twice:

| | `glowmere-valley-2` | `-multicam` |
|---|---|---|
| tree layers / models | 3 / 3 | **8 / 8** |
| tree instances | 638 | **2,872** |
| on the hills | 152 | **1,351** |
| tree line | 11.20 m | **11.20 m** |
| any layer at its cap | no | no |

The tree line does not move, and that is a constraint rather than a coincidence: ADR-334 put the
ladder at 8/8/6.5 m so the 16 m elder clears the tallest instance by a fifth, and ADR-335
re-derived the cast's 1.94× from it. Every ported row is authored so `height × maxScale ≤ 11.2`.
What the new rows add is rungs *between* the line and the floor — 9.94, 9.52, 9.24, 7.02 m — so the
canopy has a top instead of a plateau.

**And the gate really was the riparian ladder, on this terrain and not only on valley 3's:**

```
glowmere-valley-2   canopy    hills refused: slope 0.4%  HAR 95.1%
                    deadwood  hills refused: slope 0.0%  HAR 89.9%
multicam            the six upland layers           0.0%       0.0%
```

95.1% is ADR-340's figure to the digit. Slope refuses between 0.0% and 0.4% of hill samples on
*every* layer in *both* files, so the ported table's slope raises (0.26 → 0.28, 0.30 → 0.32) buy
nothing; they are carried because they came with the table, and they are not the lever. Anybody who
reaches for `maxSlope` to put trees on a Glowmere hillside is reaching for the wrong knob.

Cost, structural counts at t = 0 and no milliseconds (ADR-170; three other agents were live and a
contended timing is worse than none): draws 236 → 253, indirect draw slots 224 → 359, triangles
303,829 → 462,217.

## 3. The cast, and the demonstration that did not port

The five aliens move from `explore` to `decide`. The mapping was not a judgement call: valley 3's
five bodies carry the same five GLBs **and the same five seeds**, so `rook` is the scout, `tide` the
wader, `sage` the elder, `ember` the drylander and `vane` the watcher by construction.

Provenance is split on purpose. **Structural** numbers are ADR-340's measured ones — `minRange` 12,
which is what stops a body choosing the thing it is standing next to and spinning on the spot
(7,070 of 7,200 frames in `Turn` before it was set); the novelty radii; `holdPost` at 0.42 and not
1.0. **Taste tables** are the film's own: each character's five `explore` affinities, carried
verbatim, because the brief was to move this cast to a new system and not to import another.

Also landed: ADR-300's pose layers on all five nodes (an `Aim` over `head.x`, `Eye_L`, `Eye_R`,
`Mouth`, `Antenna` pivoting on `head.x` — these rigs are flat Auto-Rig Pro exports whose eyes are
*siblings* of the head — and an `Additive` startle over the spine), and both of ADR-340's engine
fixes opted into.

**`RouteConsiderer` does not port, and the probe says why.** Valley 3 had two ways across its river
because valley 3 *authored* a ford and a backwater into its terrain. This film never got those
edits. Measured here: the channel is 2.60–3.65 m deep at all eight points along its course against
a wade depth of 0.8536 m, and the nav grid is in **4 disconnected pieces with 9,788 of 20,180
walkable cells (49%) unreachable from the largest**. `rook`, `tide` and `sage` are on the west bank;
`ember` and `vane` are on the east; neither group can reach the other's hero fungi. A `route`
considerer here would price one way and call it a choice.

That geography is also why the stall breaker is load-bearing and not belt-and-braces: perception
reaches 85–120 m, the river is 50 m from `rook`, and a percept across the water is a goal no path
reaches.

**The character-awareness demonstration does port, and it ports better for being unauthored.**
`vane`'s committed option over ninety seconds is `'ember'`, at both seeds. Nothing in the file names
`ember`: a Character percept is whatever body this one saw, and on this terrain `ember` is the only
other body in `vane`'s nav region. That is a fact about the river.

### Ninety seconds of the cast, measured

```
                     travelled     net   nearest body   mostly
  rook                 200.8 m   29.9 m        4.74 m   lantern-cap
  tide                 196.1 m   34.7 m        4.74 m   range
  sage                 222.6 m   18.8 m       20.74 m   grove
  ember                238.5 m   52.7 m        5.48 m   range
  vane                 189.1 m   48.9 m        5.48 m   ember
```

`sage` is the entry that had to be earned. On the first cut it travelled **0.0 m and spent all
3,600 frames in `Idle`** — ADR-340's `holdPost` failure exactly, and the reason it happened is
worth writing down because the number that fixed it is not the one ADR-340 names. `holdPost`'s
score is flat inside `tolerance`; `interest` only beats it if something in range scores above it.
Valley 3's elder stood 17 m from a 16 m mushroom; `sage` stands at (−112, 16) with the nearest hero
fungus 85 m away, so at `maxRange` 48 there was **nothing in range to want**. The weight was not too
low, the world was too far: `graze` went 0.62 → 0.95 and `maxRange` 48 → 70, `homeRadius` 34 → 50,
and `sage` now walks 222 m with a net displacement of 19 — it strays and comes back, which is what
an elder by a grove should look like. `grove` is still its most-committed option.

**Two losses, stated rather than buried.** `ember` and `vane` carried
`jumpRange`/`jumpApex`/`jumpSignal: audio.beat` on `explore`; jumping is an `explore` feature and
`decide` has no equivalent, so the beat-jump is gone from the film. And three reactions targeted
`explore/speed`, `explore/observeChance` and `explore/runChance`, which are parameters of a
behaviour that no longer exists — a reaction that resolves to no parameter is reported once and then
does nothing. They are retargeted onto parameters `decide` publishes (the considerer weights and
`decide/hertz`) at re-scaled depths, because a weight is not metres per second. 24 reactions bound,
0 unresolved.

## 4. The hero table was a photograph of the *end*, and that is why the alien shots were wide

The owner asked for the wide angles to be less wide. Two things were true and only one of them was
the lens.

**The camera is baked at playback and *evaluated* at bake time**, and the second half of that has
been got wrong on this film before. `heroes[].preferredCameraDistance` decides nothing on playback —
`composition.cpp:1785`, "the camera's path is the bake" — but `orbitPoint()` clamps
`r = subjectRadius × shotKindMultiplier` into `[pd × 0.35, pd × 1.5]` and the Auto-director runs
through exactly that path. It is live and binding while a cut is being composed. It turned out not
to be the lever here, but it is not inert, and saying so is half the point of this section.

**The lever was `heroes[].position`.** `orbitPoint` orbits about it. For the ten hero fungi it is
the cap and it is correct. For the six *moving* heroes it is a photograph taken by
`--save-project`, and the photograph is of the **last frame of the director's forward simulation**,
not of the scene. So every re-bake anchors the next one on where the body finished the previous
film. Measured across three successive bakes, `ember`'s anchor walked

```
  x = -78.6  ->  -146.6  ->  -214.6
```

— exactly −68.0 m each time, while `ember`'s node sits at x = +12. By the third bake the anchor was
**272 m** from the body it named, and the two `establish` shots on `ember` were composed 45 m out
from a subject 1.76 m across. That is where "the wide shots" came from, and it is why the three
alien-subject shots had been "known fragile": they were framed on a ghost.

Subject coverage — the fraction of frame height the subject spans, which is the honest measure
because a big mushroom at 75 m and a small one at 21 m frame identically:

| | widest | tightest | biggest step in the ladder |
|---|---|---|---|
| the film as it was (with the hand dolly) | 0.1490 | 0.4751 | ×1.45 |
| re-baked on the stale anchors | 0.1130 | 0.4751 | ×1.90 |
| **re-baked on corrected anchors** | **0.2511** | **0.4751** | **×1.13** |

Every one of the 42 shots is now in one family, with no outlier to correct. **The earlier
50%/20% dolly pass is not re-applied and is not needed**: the owner released it, and fixing the
anchor did more than scaling 43 shots' keys about their own subjects ever could, because it removed
the cause instead of the symptom.

The procedure, and it is a procedure rather than a fix because the defect is in the save: set the
six moving heroes' anchors to their scene nodes (body centre for a walker, the node for the
saucer), bake, then **set them again**, because the save will have re-photographed the end. The
first pass is what the cut is composed from; the second is what the next person inherits.

`Valley Wide` is the film's one genuine wide *angle* and it is a separate camera the re-bake does
not touch — `directEngine` writes `camera/*`, not `cameras/valleywide/*`, so its earlier 50% dolly
survived for free. Its lens goes **52° / 24 mm → 44° / 30 mm**, which is the owner's sentence taken
literally.

**And it had to be changed in two places.** The scene's `cameraDirection.cameras[].fov` is only the
*default*: `composition.cpp:5081` registers it as the parameter `cameras/valleywide/fov` and
line 5149 reads the parameter, so the project's saved 52.0 won. Changing the scene alone produced
two renders whose hashes were **byte-identical to the unchanged arm** — which is the check catching
a silent no-op rather than a test passing, and the reason ADR-182's rule is phrased about arms that
must differ.

## 5. The picture

Six before/after pairs under `examples/world/renders/v2modern/` (gitignored), all twelve hashes
distinct, at the times the brief named.

* **t = 42.0, `cairn-cap`** — the clean tree A/B: identical camera, identical stand-off (37.3 m),
  identical coverage (0.293), so the only variable is the layer table. Before: a bare slope with
  four trees at the right edge. After: a wood of several species across the middle ground with a
  tree line following the glowing bank. This is the frame that shows what the owner asked for.
* **t = 3.0 and t = 28.0, `Valley Wide`** — the hills carry a canopy that breaks the skyline where
  they were bald, on both walls; at 44° the mushroom, the saucer and the wooded ridge are all
  bigger and the empty sky band is smaller. The world fills the frame.
* **t = 17.0, `vane`** — the one that proves §4. Before: a bed of ferns with no subject anywhere.
  Re-baked on the stale anchor: undergrowth, still no subject. Re-baked on the corrected anchor: a
  full-body shot of the alien, centre-left, in a glowing meadow with the saucer behind it.
* **t = 34.0, `ember`** and **t = 48.5, `tide`** — subject in frame in both, against a wooded
  hillside. `t = 48.5` before was an empty sky over a bare hill and is the sharpest illustration of
  "too small a world": it was not too small, it was too empty.

## 6. Consequences

* `examples/world/glowmere-valley-2-multicam.{json,scene.json}` change. The song film,
  `glowmere-valley-2` and `glowmere-atmospherics` are untouched and now diverge from the multicam.
  That is intended: each film owns its own scene file and the owner named one.
* `tests/unit/test_glowmere_multicam.cpp` is new: four probes (placement counts, the hill gate, the
  river and reachability, ninety seconds of the cast) and three guards, every one of them with the
  sibling film or a seed shift as a control that must come out differently.
* The hero-anchor defect in `--save-project` is **not fixed here**. It is an engine change on a path
  three other units of work are standing on tonight, and this ADR's job was a film. Recorded as a
  revisit trigger below with the measurement that finds it again in one command.

## Rejected alternatives

* **Lower `heroes[].preferredCameraDistance` to cap the wide end.** It is the right-shaped lever and
  it is live at bake time, but it was aimed at a symptom: the two wide shots were wide because their
  anchor was 272 m out, and capping `1.5 × pd` would have pulled *every* `establish` and `approach`
  shot in to hide two broken ones. Measured after the anchor fix, the ladder's biggest step is
  ×1.13 and there is nothing left to cap.
* **Re-apply the 50%/20% dolly after re-baking.** Released by the owner, and superseded: the
  re-bake on corrected anchors produces a tighter ladder than the dolly did.
* **Port valley 3's ford and backwater terrain edits too, so `RouteConsiderer` would work.** That is
  re-cutting the river through a film whose 42 shots, 26 routes and abduction scenario are composed
  against the terrain as it is. The owner asked for the trees and the cast, not the geography.
* **Give `tide` and `ember` a `route` considerer with same-bank destinations.** It would parse, run
  and prove nothing: both probes return the same path, so the "two ways" the considerer exists to
  price do not exist. A demonstration that cannot fail is ADR-182's vacuous arm.
* **Drop `sage`'s `interest` behaviour** (the `music.impact` attention at four named subjects) when
  moving it to `decide`. Kept: it is a different mechanism from the `interest` *considerer*, it is
  the film's musicality, and the ninety-second probe shows `sage` moving with it in place.

## Revisit triggers

* **The hero anchor.** `heroes[].position` for a moving hero is written from the end of the
  director's simulation. Re-bake twice and diff the table; a body whose anchor moves by a constant
  step per save is this bug. Until it is fixed, every re-bake of this film must be followed by
  restoring the six anchors.
* **`decide` has no jump.** `explore` reads `jumpSignal`/`jumpRange`/`jumpApex`; nothing in the
  considerer system does. Two characters in this film lost an audio-beat jump to the move.
* **`holdPost` in an empty neighbourhood.** ADR-340 blamed the weight; this file found a body at
  the right weight that still froze because there was nothing inside `maxRange` to want. The
  failure is the *ratio* of the post's flat score to the best candidate in range, and neither
  number is visible in the file.
* **Glowmere's river is still a wall.** 49% of the walkable ground is unreachable from the largest
  region in every surviving Glowmere scene. Any future work that wants characters to cross it has
  to author a crossing; valley 3's is gone.
