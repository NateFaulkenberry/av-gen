# ADR-340: The showcase is a rebuild, the river was a wall, and the hills were banded off on purpose

**Status:** Accepted
**Date:** 2026-09-18
**Follows:** ADR-264 (a scene file is not the state that runs), ADR-271 (a UI edit lands in the
project), ADR-333 (a character kind is a list of considerers), ADR-336 (the route considerer),
ADR-337 (root motion), ADR-334 / ADR-335 (the scale ladder), ADR-174 (the riparian ladder),
ADR-182 (a probe that cannot fail), ADR-240 (a push may correct a walk, not replace one)

Glowmere Valley 3 is the autonomous-character showcase. Its brief has three halves: decide whether
valley 2 can be cleaned or must be rebuilt and record why; make four demonstrations work and be
emergent; and, added by the owner on the day,

> *"have the agent make the hills more densely populated with variety of trees available"*

---

## 1. Rebuild, and the reason is not that cleaning looked hard

`examples/world/glowmere-valley-2-multicam.json` carries **5,455 parameters**, and the cheap
computation ADR-264 wrote finds **248** of them naming a node transform, of which **41 contradict
the scene outright**. Counted here rather than quoted: 36 of the 41 are the four parts of nine hero
fungi turned together, which ADR-264 §4 adjudicated as *authoring* — somebody turned the mushrooms
in the editor. The other five are a stem nudged 0.60 m off its own cap, a spore emitter switched
invisible, an alien 68 m from where its scene puts it, a spore volume, and a second alien's yaw —
and ADR-264 called three of those residue. It reached that verdict with `git log -S` over a
7,114-line whole-file re-save. ADR-271 then found that two agents had got the same class of
judgement wrong on two separate nights, on sound reasoning, because **from inside the project file
a number is indistinguishable from residue.**

That is the argument. It is not that valley 2 is dirty; it is that valley 2 is a file whose two
halves cannot be told apart by anybody who was not there, and the owner's stated preference is
*clean authoritative project state > preserving every historical serialization detail*.

Four more facts, each independently sufficient:

* **The multicam project is a film.** 41 `cameraShotSpans`, 36 `cameraAimFollow`, 26 `routes`, a
  `sequence` with a `songPlan` and a `sectionTimeline`, an `autoDirector` block and a `timeline`.
  A character showcase needs none of it, and another unit of work owns those tables today.
* **The scene is shared by reference (ADR-271).** Cleaning `glowmere-valley-2.scene.json` in place
  changes what `glowmere-valley-2-multicam`, `-song` and `glowmere-atmospherics` render. Three of
  those are shipped films.
* **The behaviour layer had to be replaced whatever happened.** The only scenes in this repository
  that declared a `decide` behaviour were two lab fixtures — `guard-post.scene.json` and ADR-336's
  `river-crossing.scene.json` — and **no world**. Glowmere's five aliens are all `explore`, the
  700-line hardcoded decider ADR-269 was written about, and every one of the four demonstrations is
  a considerer. Valley 3 is the first world in the repository with a decider in it, which is also
  why §4's two engine-level gaps had gone unnoticed: neither shows up on a flat 240 m fixture with
  one errand in it.
* **`tools/make_glowmere_valley_2.py` re-reads its own project** (`REBASE = os.path.exists(PDST)`)
  because by the time it was written the project had become edited state: 3,063 parameters where
  the generator had written 585. That was the right call there and it is the condition that makes
  the file unrebuildable.

### What is carried, what is rebuilt, and what is dropped

`tools/make_glowmere_valley_3.py` reads `glowmere-valley-2.scene.json` — the base, not the multicam
— and **reads neither of its own outputs.** There is no rebase branch, so there is no half of the
project that a regeneration cannot tell from its own. That is the hygiene deliverable.

Carried, byte for byte: the terrain node's `world` and `terrain` settings and `material`; the ten
hero fungi (40 procedural nodes) and their ten spore emitters; the river dressing; `environment`,
`post`, `wind`, `lightRig`, `materialPrograms`, `worldEffects`; the ten hero points.

Authored in the generator: two new water features, the scatter layers, the cast, the cameras,
`composition.focalPoints`, and the whole project.

Dropped, each named in the script rather than filtered by a pattern: the saucer `visitor`, its
`visitor-beam` and the `staging` block that runs the abduction; the sixteen farm animals; the five
v2 aliens (their bodies return under new names with new minds, and keeping the names would make
"the same character with a decider bolted on" a claim nobody could check).

### The project is four parameters

```json
"parameters": {
  "camera/exposure/mode": 0, "camera/exposure/compensation": 0.35,
  "post/grade/contrast": 1.04, "post/grade/saturation": 1.06
}
```

None names a node, so `nodes/*/position` cannot drift from the scene because it is not there to
drift. `tests/unit/test_glowmere_valley_3.cpp` asserts the class is empty and uses
valley-2-multicam's 248 as its control. **The whole project is 781 bytes.**

It also carries no `worldEffects`, and that is the engine's own rule rather than a preference:
`Engine::loadProject` clears the list and refills it from the composition before it reads the
project's copy, and `Engine::saveProject` writes that copy **only when the live list differs from
what the project already holds**. The first draft of this file duplicated the scene's four hundred
numbers there, which is exactly the class of thing being removed.

And the generator **does not read `glowmere-valley-2-multicam.json`**. It did, for the hand-turned
rotations below, and that made valley 3's output depend on a file another unit of work is editing
today — a save over there would have silently changed this world at the next regeneration. The ten
values are written down in the script at the float32 spellings that file holds them in.

**What this does not fix.** ADR-271's boundary is unchanged and correct: a UI edit still lands in
the project. So this file will grow the first time anybody saves from the application. The
generator is idempotent, and re-running it is how the project returns to its authored state — which
is a different guarantee from "it cannot happen", and is the honest one.

### Three serialized values the runtime does not represent

Found by loading the scene and reading what `Composition` said out loud, not by inspection:

| what | what the engine says |
|---|---|
| `procedural.lod.lodCount`, on three dressing nodes | `lod: unknown setting 'lodCount' ignored` |
| the terrain's five `groundGlow*` keys and `groundMottle` | `groundGlow 0.08 is carried by the generated ground material, and this terrain draws with the authored program 'paintedGround2' instead -- so the glow, its scale, its coverage, its colour and groundMottle all do nothing` |
| `animation.cullDistance` 360 against an entity `cullDistance` of 620 | `the rig stops being posed at 360 m but the entity keeps simulating to 620 m; between them the character travels in a frozen pose` |

All three are carried by valley 2 and were dropped rather than carried forward. A fourth is noted
and not fixed: `"footprint"` on a `ground` behaviour is read by nothing — `Ground` has never had
the field — and valley 2 and two lab fixtures carry it.

Two more residues in valley-2-multicam are recorded here and **not** touched, because that file
belongs to another unit of work: its render output path is the absolute
`/Users/natefaulkenberry/Desktop/bad.mov`, and its audio asset is
`../../../../../Desktop/Rebuild.mp3`.

---

## 2. The river was a wall, and the first fix made it deeper

ADR-336 §9 says the showcase's second demonstration "is two `route` considerers with two
`wadePenalty` values, which is what `river-crossing.scene.json` already demonstrates". The lab's
river is 14 m wide, **1.40 m deep against a `navWadeDepth` of 1.8**, and **ends at x = −50**. So a
ford exists and a way round exists, both by construction.

Glowmere has neither, measured before anything was authored:

```
navWadeDepth 0.8536        the channel is 3.5 - 3.65 m deep at all six sampled stations
19 disconnected nav regions; 10,179 of 20,819 walkable cells (49%) stranded
```

A 3.48 m body that wades 0.85 m cannot cross 3.6 m of water, and
`tools/make_glowmere_valley_2.py` authors the centreline to leave the map at both ends on purpose
— *"the channel crosses both boundaries instead of stopping at them, so there is no edge at which
it can end"*. **The Glowmere run is not a crossing decision. It is a wall, and the region count
says so.**

Raising `navWadeDepth` to 3.8 is the obvious answer and it is wrong twice: ADR-334 derived 0.8536
from the cast, and it is a property of the *world's* navigator rather than of a character, so it
cannot be the thing that tells two characters apart.

### What the engine will not let a ford be

The first attempt laid a radial `flat` feature with `water: true, waterDepth: 0.46` over the
channel, on the model of `elder-pool`. **It made the river deeper — 3.60 m to 4.07 m** — and
`world_map.cpp` says exactly why:

```cpp
cutTarget    = min(cutTarget, hit.level - amplitude)   // over every water feature that reaches
cutWeight    = max(cutWeight, w)
waterSurface = max(hit.level + waterDepth)             // over every water feature that reaches
```

The bed is a **min** and the surface is a **max**. A second water feature laid over a river can
only ever deepen it; the flatten cannot help either, because the cut is applied after it.

The consequence is structural and it decided the geography: **a shallow reach of a river is a
shallow segment of the river feature itself**, and it reads as shallow only where no deep segment
reaches at all — `featureWeight` is zero at and beyond `width`, so the neighbouring deep segments
must end 26 m away on each side.

### What valley 3 authors

| feature | what it is |
|---|---|
| `glowmere-run-3-upper` / `the-ford` / `glowmere-run-3-lower` | the river in three segments; the middle one has `amplitude` 0.50 instead of 3.60 over z ∈ [−50, 50]. It measures **0.50 m** and it is the only place the main channel can be crossed on foot. |
| `the-backwater` | a 0.70 m flood channel on the east plain, 30 m wide, **with both ends inside the map**. This is the crossing *decision*. |

The ford is what stops the valley being two worlds:

| | valley 2 | valley 3 |
|---|---:|---:|
| nav regions | 19 | **3** |
| walkable cells stranded from the largest | 10,179 (49%) | **6 (0%)** |

And `RouteConsiderer::price` itself, from the two bodies' own start to their own destination:

```
ford    104.15 m, 17.10 weighted wet metres
detour  231.31 m,  0.00 weighted wet metres

wadePenalty  1.6 -> ford   131.51 (score 0.4063) beats detour 231.31 (0.2801)
wadePenalty 16.0 -> detour 231.31 (score 0.2801) beats ford   377.80 (0.1924)
```

The crossover is at 7.44 and the two authored tastes bracket it by 4.6x and 2.2x. The detour is
**bone dry**, which ADR-336's lab detour was not; that is the geography being kinder here, not a
stronger claim.

**The control that nothing about the geometry can pass:** exchange the two `wadePenalty` values in
the file and the wetness swaps with them. That is an arm of
`tests/unit/test_glowmere_valley_3.cpp`, and the same two points in valley 2 publish **one** way,
96.74 m, 0.00 wet — one option, no choice.

### There is no jetpack, and the brief says there is

The brief's second demonstration is *"a jetpack alien crosses the river; a walking-only one routes
around it"*. Every alien GLB ships a `Flying_jet` clip and `Floating` and `Walking_low_grav`, so
the fiction has animation. **The engine has no flight mode for a walking character.** `Airborne`
(ADR-194) is a ballistic hop with a `maxDistance` default of 4 m against a 26 m river, and the
navigator's `wadeDepth` is a property of the world. What ADR-336 delivered — and the only thing the
engine can express — is a **price on a wet metre**, so the two bodies are named `wader` and
`drylander` for what they actually differ by. Recorded rather than papered over.

### The first siting drowned a hero

The backwater's first course ran up the *west* floodplain, and the staging probe reported the
`lantern` hero fungus standing in **0.66 m of water**. The east plain between z = −130 and z = +50
is the largest piece of flat, empty, dry ground in the valley — 8 to 10.5 m, slope under 0.01 at
every station — and the nearest staged thing to the course it took is the `spire` fungus, 33 m off.

---

## 3. The hills, and two plausible explanations that are both false

The reconnaissance handed to this unit said the hills are bare because `canopy` declares no `scree`
or `rim` density and because `maxSlope` 0.26–0.32 gates the tree layers off steeper ground. Both
were checked before anything was changed, and **both are wrong on this terrain.**

`tests/unit/test_glowmere_valley_3.cpp`, 240×240 dry samples of the shipped valley-2 world:

```
the hills (above mid-height) are 33% of dry ground, and they are
  forest 49.9%   rim 29.9%   scree 20.2%

of hill ground, each tree layer is refused for:
  canopy     slope  0.4%   height-above-water 95.1%   biome 0.0%
  deadwood   slope  0.0%   height-above-water 89.9%   biome 0.0%
  pines      slope  0.1%   height-above-water  3.2%   biome 3.0%

slope percentiles: p50 0.054  p75 0.101  p90 0.149  p95 0.182  p99 0.283  max 0.520
```

* **Half the hill ground is `forest` biome**, and `canopy`'s forest density is its largest. The
  biome refuses **0.0%** of it.
* **98.1% of all ground and 99.6% of hill ground is already under 0.26**, the tightest tree
  layer's `maxSlope`. Raising it to 0.38 buys about one per cent of the map. It is raised anyway,
  for the steep scree pockets it does reach, and that is worth saying because it is nearly nothing.

What empties the hills is **ADR-174's riparian ladder** — `HAR_BANDS` in
`tools/make_glowmere_valley_2.py`, which bands `canopy` at 2.5–42 m above the water table and
`deadwood` at 4–46 m. The hills sit at **p05 47.4 m, p50 66.8 m, p95 100.5 m** above the water.
The band ends where the hills begin, and it was authored to: the ladder's own comment says it
*"removes instances from the places the camera looks across rather than at"*, and Phase 7 of that
script had already widened `bushes` from 24 m to 38 m for the same complaint — *"the east wall
carried pines and nothing else, and wide shots read bare on that side"*.

**So this is a deliberate design being deliberately revisited**, and the revision is aimed at the
gate that is shut rather than at the two that are open.

### Eight layers across eight models

Three layers across three models becomes eight across eight: `CommonTree_1`, `CommonTree_4`,
`TwistedTree_2`, `TwistedTree_4`, `Pine_3`, `Pine_1`, `DeadTree_1`, `DeadTree_4`. The valley-floor
pair keep the 2.5–44 m band; the upland six run to 100–124 m above the water and carry `scree` and
`rim` densities, which no layer in valley 2 had for `rim` at all — 30% of the hills.

**The layer named `pines` grew no pines, and both halves of that are fixed.** It loaded
`TwistedTree_2` while `Pine_1..5` sat unused. Renaming it alone would have left a world with no
pines in it and adding pines alone would have left a layer lying about its species, so the layer is
**renamed `twisted`** for what it grows and **two real pine layers are added**, which is what makes
the upland read as a treeline rather than as scrub.

### What it comes to, measured

Like for like -- the same world, the same clearings, the same cast -- against
`glowmere-valley-3-legacytrees`, which is valley 2's three layers dropped into valley 3:

| | the three layers | valley 3's eight |
|---|---:|---:|
| tree-sized instances placed | 545 | **2,593** (4.8x) |
| of those, on the hills | 150 | **1,344** (9.0x) |
| distinct tree models | 3 | **8** |
| layers sitting on their `maxInstances` cap | 0 | **0** |
| tree line (`height x maxScale`) | 11.20 m | **11.20 m** |
| every scatter instance, including the grass | 56,716 | 58,764 |

(`glowmere-valley-2-multicam` as shipped places 638 trees, 152 of them on the hills. It is not the
A/B, because its world has no ford, no backwater and two fewer glades; it is the number the owner
was looking at when they asked.)

**No cap binds, before or after.** The reconnaissance expected `maxInstances` and `meshBudget` to
bind; they do not. Valley 2's densest tree layer placed 306 of an allowed 2,000. The caps were
raised anyway (to 1,400–3,200) so that a future density change is not silently truncated, and the
test asserts no layer is at its cap, because a density in a file that a cap is eating is a fiction.

### The scale ladder did not move

ADR-334 measured the tree line at 11.2 m and ADR-335 re-derived the cast's 1.94x from it, so the
ceiling is 16 / 1.2 = 13.33 m. **Every one of the eight layers is authored so that
`height × maxScale` ≤ 11.2**, and the tallest three sit exactly on it. Variety is in species, tint,
clustering and scale *spread*, never in the ceiling — which is what turns a plateau into a canopy
with a top:

```
   16.000  elder-2 (hero fungus)                          4.60 bodies
   11.200  canopy / twisted / pine-upper (scatter, max)   3.22 bodies
   11.011  elder-2 gill line                              3.16 bodies
    9.940  canopy-broad      2.86        9.520  pine-rim       2.74
    9.240  twisted-low       2.65        8.450  deadwood       2.43
    7.020  deadwood-rim      2.02        5.120  fan-plants     1.47
    3.480  drylander (tallest body)                       1.00 bodies
```

`test_glowmere_scale.cpp` now runs over five scenes, and two of its derivations had to stop being
lists of names. `kTreeLayers {"pines","canopy","deadwood"}` would have computed valley 3's tree
line off the one of those three names it still has and thrown on the other two; **a tree is now any
scatter layer at least 5 m tall**, with `treeLayers >= 3` as the liveness control. And the
per-scene `speedsChecked >= 8` was a liveness check wearing an agreement check's clothes: valley
3's project copies no behaviour speed at all, and a project that carries no copy *cannot* disagree
with its scene. The count is a total across the five scenes now.

---

## 4. The four demonstrations, and the two the engine could not do

All four are `decide` behaviours over ADR-333's stock considerers. There is no `actions` list on
any of the five bodies, no timeline, no staging scenario and no trigger. What is authored is where
each one starts, what it can perceive, what it likes, and how much it minds getting wet.

| | what it is | measured over 180 s of the shipped scene |
|---|---|---|
| 1 environmental awareness | `scout`: `interest` over its percepts with an `activity`, an `approach` and a `dwell`, plus a low-weight `holdPost` for a territory | **243.9 m walked, 21 decisions, 11.4 m net** -- it circulates rather than departs, and every committed option is the name of a perceived glow patch |
| 2 environmental navigation | `wader` / `drylander`: one `route` considerer, `wadePenalty` 1.6 against 16.0, identical otherwise | **97.4 m through 0.77 m of water, 19.4 s wet** against **248.0 m through 0.01 m, 0.0 s wet**. Both arrive within 9 m of the same point |
| 3 character awareness | `watcher`: the same considerer as the scout with `character` at 4.8 instead of 0.6 | **215.7 m, 23 decisions**, committed most often to `elder` -- the name of another **body** -- closes to under 14 m, dwells, leaves and comes back |
| 4 animation intelligence | 8 distinct clips of the 26 each GLB ships, under nine role names, plus ADR-300's `aim` and `additive` pose layers, `matchRate` and `slopeAlign` | every body reaches at least two activities for half a second or more |

A fifth body earns its place by not being a demonstration: the `elder` holds its ground by the
elder fungus (76.5 m, 14 decisions, 9.0 m net) so that "the watcher went and looked at it" is
distinguishable from "they both happened to walk the same way".

**One observation this does not claim to have chased.** The `watcher` records a deepest water of
1.58 m in the first arm and 0.00 in the second, and 1.58 m is past `navWadeDepth`. It is not
route-driven and it is not a defect in the crossing: the nav grid is 4 m and its `wade` is a cell
sample, while the probe reads `Navigator::sample`'s analytic depth at the body's exact position, so
a body crossing near the edge of a walkable cell can stand in water the cell did not know about.
ADR-295 is the record of that gap being real and deliberate; the grid is honest enough to price a
route with and not to stand a body on.

**`investigate` is used by nobody, and that is a finding.** It was the obvious considerer for
demonstrations 1 and 3 and both locked. Salience rises as a body approaches, and `investigate` has
no visited memory — ADR-333 §5 keeps the two memories on the behaviour and only `goalWeight` reads
them. On a transient subject that is right; on a **stationary** one the option it is executing keeps
getting better and the body never leaves. Measured: a `watcher` with
`investigate kinds:["character"]` at weight 1.7 walked 36.8 m to the elder, stopped 8.2 m off, and
spent **6,183 of 7,200 frames idle** there. `minRange` does not fix it, it converts it into an
oscillation the size of the selector's dwell. So the approaching is `interest`, whose novelty
memory is the "resume" half.

**No clip is opted into root motion, and ADR-337 is why.** P9 landed the same day and the obvious
thing to do with a new showcase is to use it. Its own negative result says not to: `Landing`'s
displacement is 96% vertical, `applyGrounding` *assigns* `travel.y` so a grounded body keeps none
of it, and what is left is 0.022 m of horizontal shuffle. The clip root motion earns its keep on is
`Dying_forward`, which travels 0.985 m across the ground — and nobody dies in this showcase, so it
is not one of the eight clips bound. `animation.rootMotion` is absent from valley 3 for the same
reason it is absent from valley 2, and the reason is now written down twice.

**There is no IK in this engine.** The brief's fourth demonstration names it. `scene::PoseLayerKind`
is `Aim` and `Additive`; what valley 3 has is head/eye look-at, an additive reaction masked to the
spine, gait rate-matching and `ground`'s `slopeAlign`. Recorded as absent rather than claimed.

### A decider that stalled could not get out of it

`Selector::select` hands the queue new actions only when the committed option *changes*, and
`Decide::remember` is likewise called only on a change — both for good reasons written where they
are. Together they close a loop:

```
the committed goal becomes unreachable, so the body stops moving
  -> its position stops changing, so every option's score stops changing
  -> the same option keeps winning, so select() returns false
  -> nothing is pushed and nothing is remembered, and the body never moves again.
```

Measured: the `scout` committed to a glow patch 31 m away at t = 78 s with a score of **1.338**,
and reported **1.338 at every ten-second sample from t = 80 to t = 180** while standing still. Four
of five bodies had stopped by t = 90.

`decide` takes `stallSeconds` (0 = off) and `stallDistance`. A body that has not moved
**remembers where its plan was taking it** — the opposite of what `remember` does on a change, and
deliberately: recording a destination *on departure* devalues the errand you just set out on, while
recording one you have failed to reach is exactly the fact `goalWeight` wants. Then
`Selector::forget` drops the commitment and **keeps the counts**, because a stall breaker that used
`reset()` would erase the evidence that it had fired.

A partial data-level mitigation was tried first and is kept for what it is: a low-weight `holdPost`
whose score rises with distance is the one stock score that grows while a body stays away from
somewhere, so it breaks a stall that happens far from home. It broke one and not the next, because
the second stall was 25 m from the anchor. It stays as the cast's *territory*, which is what it is
good for.

### A `decide` character was not a body

`EntityState::radius` is what `EntityWorld` collects into the crowd field, and 0 means "not a body:
takes part in nothing that separates crowds". The only behaviour that ever wrote it was `Explore` —
so ADR-333 moved the decider out of `Explore` and left the body behind. Measured: the `watcher` and
the `elder` closed to **0.238 m** with a combined width of 1.8 m.

`ground` takes `bodyRadius` (0 = off) and the separation arithmetic moves out of `Explore` into a
shared `separateFromCrowd`, called by both and guarded so an entity with `explore` *and* `ground` is
not pushed twice at twice the speed ADR-240 argued for.

The first cut declared the radius and then never separated, and the tell was that **the
measurement came back bit-identical**: `EntityState::radius` persists across frames, so "did
anything declare a body *this frame*" is not the question `state.radius <= 0` answers.

**Neither change alters any existing scene.** Both default to off, and the 190 entity, behaviour,
decision, route, glowmere2 and scale cases — 249,288 assertions — pass unchanged.

### It varies, and the way it varies changed

The showcase's claim is that nothing is scripted, so it has to vary. The control is **determinism**:
two runs of identical inputs differ by exactly 0.0, which is what makes the two numbers below
statements rather than tolerances.

| arm | scout | watcher |
|---|---:|---:|
| a second run of A | 0.0 | 0.0 |
| B: every entity seed + 900,001 | 1.9 | **55.9** |
| C: every ecology seed + 101 (a different world to perceive) | 94.4 | 66.4 |

(The measure is endpoint distance plus the difference in metres walked. Endpoint alone is the wrong
instrument and the measurement said so: the watcher attends to the `elder`, which does not move, so
in a re-rolled world it ends 12.0 m from where it ended before — after walking 190 m instead of 35.
A different errand at nearly the same address.)

**The entity seed used to be worth 0.4 m** and is now worth up to 55.9, and the reason is worth
writing down: ADR-333's considerers draw from no stream (D1, D2), so the seed only moves the
decision tick's *phase* — and a stall is broken **on a decision tick**. A body that gives up a
quarter of a second earlier commits to a different percept and walks somewhere else for the next
minute. A body that never stalls barely moves under a seed; a body that does, moves a long way.

The *demonstration* survives what the itinerary does not: in every arm the wader wades and the
drylander does not, because that is a property of the geography and the taste rather than of the
run.

---

## 5. The picture

Rendered from the project the way the owner renders it. Every still names its camera, because one
fixed cut cannot be in two places at once and the cast does not wait its turn —
`tools/glowmere3_shot.py` writes a temporary scene with the camera pinned, renders one frame, and
deletes it.

| frame (under `examples/world/renders/`) | what it shows |
|---|---|
| `v3-crossing-36s` | the `wader` standing mid-channel in the backwater, legs submerged, the wooded hill behind |
| `v3-detour-62s` | the `drylander` on dry ground, the same water behind it, walking round its northern end |
| `v3-watcher-30s` | two aliens under the 16 m elder, one walking to the other, clear of each other |
| `v3-scout-118s` | the scout working the west floodplain between two hero fungi |
| `v3-westwall-new` / `v3-westwall-legacy` | the A/B below |
| `v3-scout-118s-seedC` | the same camera and second as `v3-scout-118s` with every scatter seed shifted by 101 |

The last pair differs on **89.2%** of pixels, and what that proves is narrow and worth stating: it
proves arm C really is a different world rather than a no-op, which is the control the itinerary
numbers rest on. It is not itself evidence about behaviour — every plant moved — and the evidence
about behaviour is the table in §4.

### The trees, A against B

`glowmere-valley-3-legacytrees.{scene.json,json}` is generated by the same script under
`AVGEN_V3_TREES=legacy`: the same world, the same cast, the same cameras, the same second, with
valley 2's three tree layers put back. It exists because rendering valley 2 against valley 3 would
be a frame in which the water, the cast and the cameras all differ too — and this project has
shipped a "before" frame that was the empty sky's own hash.

The two frames differ on **27.7% of pixels, max channel delta 179**, which is the first thing
checked. In the legacy frame the hillside carries countable trees in its lower third and the ridge
line is bare; in valley 3 the wood is continuous to the crest and three silhouettes are
distinguishable in it — rounded broadleaf, conical pine, pale snag.

### What it costs

The structural numbers are load-independent and are the honest ones:

| same camera, same second | legacy 3 layers | valley 3's 8 |
|---|---:|---:|
| draws | 178 | **205** (+15%) |
| indirect draws | 223 | 358 |
| triangles, steady state | 281,389 | **399,467** (+42%) |
| visible instances | 1,183 | 1,476 |
| shadow draws | 36 | 36 |
| total scatter instances placed | 61,241 | 63,048 |

**The frame time is not reported, and the reason is the measurement.** Interleaved, twelve rounds,
on a machine other agents were using (load average 6.6 / 11.8 / 10.5), the same configuration
produced 30.8 ms to 171.7 ms and the **denser arm's minimum came out lower than the sparser arm's**
— 32.37 against 40.15 — which is physically impossible for a strictly heavier scene and is
therefore a measurement of the machine. ADR-170's rule is minima over repeats; a minimum taken
under that spread is a lower bound on nothing. A second pass at load 5.0 is reported in §7.

---

## 6. Consequences

* `examples/world/glowmere-valley-3.{scene.json,json}` and
  `glowmere-valley-3-legacytrees.{scene.json,json}`, generated by
  `tools/make_glowmere_valley_3.py`, which reads neither.
* `tools/glowmere3_shot.py`: one still, from a named camera, at a chosen second.
* `src/entity/behaviors.cpp`: `decide`'s `stallSeconds` / `stallDistance`; `ground`'s `bodyRadius`;
  `separateFromCrowd` extracted from `Explore`. `src/entity/decision.{hpp,cpp}`:
  `Selector::forget`. `src/entity/behavior.hpp`: `DecisionDebug::stalls`. All three default to off.
* `tests/unit/test_glowmere_valley_3.cpp`: five probes and six arms, each with a control.
* `tests/unit/test_glowmere_scale.cpp`: `kScenes` is five; the tree line is derived from height
  rather than from three names; the behaviour-speed liveness check is a total.
* Nothing in valley 2, the song film, the atmospherics demonstration or any lab fixture changes.

## Rejected alternatives

* **Cleaning valley-2-multicam in place.** §1. The scene is shared with three shipped films, the
  project is a song film's, and 41 of its 248 node parameters are a judgement call that two people
  have already got wrong.
* **Raising `navWadeDepth` so the main channel is fordable.** 3.8 m is over the head of a 3.48 m
  body, and it is a property of the world's navigator rather than of a character, so it could not
  have told the two bodies apart anyway.
* **A radial water `flat` as a ford.** Measured: it made the river 0.47 m deeper. The bed is a min
  over water features and the surface is a max.
* **Two shallow reaches of the main river as the two crossings.** They have to be ~52 m of deep
  channel apart for the river to still read as a river between them, which puts them 150 m apart
  and makes the detour a 3.2x walk nobody can film.
* **`investigate` for demonstrations 1 and 3.** Measured: it locks on a stationary subject.
* **Fabricating more animation clips.** The demonstration is that a small library is enough. Eight
  of the twenty-six each GLB ships are bound, under nine role names.
* **Reporting a frame-time delta from the twelve-round bench.** §5.

## Revisit triggers

* **A stall term in the selector rather than in `decide`.** `stallSeconds` is a per-behaviour
  setting and the loop it breaks is the selector's. The right shape is probably a selector that
  knows its committed option is not being executed, which needs the queue's opinion and is a
  bigger unit than this.
* **`investigate` with a memory.** It is the considerer the brief's first and third demonstrations
  describe, and it cannot do them on a stationary subject. Giving it the behaviour's `visited_` is
  a one-line change and a design decision this unit did not have the measurements to make.
* **A flight capability for a walking character.** The brief asked for a jetpack; the engine has a
  ballistic hop with a 4 m default and a world-wide wade depth.
* **Anybody saving valley 3 from the application.** The project will grow and the generator is the
  way back. The test that asserts `nodes/*` is empty is the guard.
* **A second Glowmere with a different cast.** `test_glowmere_scale.cpp`'s `kScenes` is a list and
  the next world will have to be added to it, which is the cost of the arm being worth anything.
* **The nav grid still refuses the terrain half of `pathClear`** in valley 3, as it does in valley
  2: *"it disagreed with the world after 323 sampled walk(s)"*. A* still runs on the grid, so the
  route considerer works; the fast path does not. That is inherited and unexamined here.
