# ADR-354: A starred character was a wall around itself, and a cow became part of the hill

**Status:** Accepted
**Date:** 2026-09-19
**Follows:** ADR-093 (the navigation seam), ADR-182 (a probe that cannot fail), ADR-264 (a scene
file is not the state that runs), ADR-273 (the seek budget), ADR-330 (a removal is a negative
fact), ADR-333 (a character kind is a list of considerers), ADR-336 (the route considerer),
ADR-340 (the stall breaker and the body radius), ADR-344 (the multicam's cast and cut)

Three defects the owner reported on `glowmere-valley-2-multicam`:

1. two aliens *"just standing there, not moving"*
2. farm animals *"walking off axis"* — and, later, *"this has occured since before the foot IK work
   - it may be stale data the project file or something"*
3. farm animals *"still clipping into hills etc"*

They are **two defects, not three**, and the split is not where it was expected.

---

## 0. The instrument was wrong first, and everything else follows from fixing it

`tests/unit/test_glowmere_multicam.cpp` loads the **scene**. A project is applied over its scene
(ADR-264, ADR-271) and this project carries 5,502 parameters. Every behaviour number in ADR-344 was
therefore taken on a world that does not ship, and the owner is looking at one that does.

Loaded from the project, over the film's own 226 seconds rather than 90, at four seeds:

```
                before                      after
  rook     0.0 m   idle 9040/9040      270-568 m
  tide     0.0 m   idle 9040/9040      117-455 m
  vane     0.0 m   idle 9040/9040      211-429 m
  sage   446-495 m                     423-514 m
  ember  286-516 m                     420-497 m
```

Not "slow": **0.00 m, from frame one, for the whole film, at every seed tried.** Three bodies, not
two. `test_glowmere_multicam_defects.cpp` is new and loads the project for this reason; its header
says so.

## 1. Defect 1 — a hero that walks was a wall around itself

`obstaclesFromHeroes` states its own assumption: *"the elder, the monument, the arch"*. Starring a
character in the Objects list breaks it, and the film has to star them — the Auto-director cuts to
`heroes[]` and an unstarred character gets no shots.

So each of the five aliens had a Blocking cylinder **and** a `ClearanceField` capsule centred on its
own standing position. `TerrainQuery::at` asks `heroPenetration` at `ground + heroMargin` and
answered `InsideHero` **at the body's own feet**; the router will not plan a route out of a cell it
will not stand in. Measured before: all five report `navigable: NO / inside a hero`, and `rook` and
`vane` are in nav region **0**, the unwalkable set.

The fix excludes heroes that an entity drives from the two structures a **walker** reads, and from
nothing else:

* `obstaclesFromHeroes` takes the driven-node names and skips them;
* `Composition::buildNavigator` builds the walker's `ClearanceField` from the same filtered list.

The **camera's** clearance field is untouched and must be — a camera flying through a character is a
real artefact — and the ten hero fungi, which are the actual monuments, keep their cylinders in
both. A body's collision was never this mechanism's job: `Ground`'s `bodyRadius` crowd field is
dynamic and is what bodies already use, while a cylinder pinned to where a walker stood when the
world was built is wrong a second later.

**The control names the bodies.** It is not "run it again": it is `heroPenetration` over the
*unfiltered* list, at the query `TerrainQuery::at` actually makes, and it returns exactly
`rook`, `tide`, `vane` — exactly the three that travelled 0.00 m and exactly the ones the owner saw.
`sage` clears its own capsule on the vertical test; `ember` clears it only because
`nodes/ember/position` puts its body 68 m from its own hero anchor, which is ADR-340's *"an alien
68 m from where its scene puts it"*, still in the file. The first cut of that control returned 0 for
five bodies the engine had just refused to stand, because it asked at the body's y instead of at
`ground + heroMargin`; a control that agrees with the fix for the wrong reason is worse than none.

### What was hypothesised and is false

That `interest` scores places the body cannot reach and `holdPost` then wins by default. It is a
real design observation and it is **not** what happened here. Measured after the hero fix: for every
one of the five, **every percept it holds is in its own nav region** — same 14, 12, 10, 14, 16;
other 0; on an unwalkable cell 0. Perception reaches 70–120 m and a region boundary is never that
close. A `GoalTaste::requireReachable` filter was written, wired and measured: it changed the
outcome by **zero bytes** at four seeds, so it was removed rather than shipped. A knob that does
nothing on the only world that motivated it is not earned.

## 2. Defects 2 and 3 are one defect, and it is `slopeAlign`

**The off-axis walk is not a yaw error.** Measured over the film, comparing each body's facing with
its direction of travel, every frame with more than 90° of error — 42, 52, 34, 41, 7, 17, 1 across
seven animals — is a frame in which the body is **16.5–16.9 m in the air**. They are the animals in
the saucer's tractor beam, spinning at the abduction scenario's own `animalSpin` of 230°/s. Animals
that never leave the ground have a yaw error of exactly 0.000, because `Wander` travels along its
own heading at `max(0, cos(facing error))` and cannot go backwards.

**And the owner's "stale project data" instinct was right about the family and wrong about the
parameter.** Not one of the sixteen farm animals carries `nodes/<name>/rotation` in the project —
the 41 contradicting parameters ADR-340 counted are hero fungi and aliens. What is stale is in the
*scene*: twelve of the sixteen carry `"slopeAlign": 1.0`, and `git log -S` plus
`tools/make_tractor_beam_lab.py` say where it came from. The lab fixture sets it on its static
animals and explains the choice — *"a body on the surface, with no behaviour of its own adding an
offset. That is what makes them the control"*. On a flat pad 1.0 means "exactly on the surface".
Copied into a valley it means what `GroundSettings::slopeAlign` says it means: *"0 stays vertical, 1
lies along the surface normal. Something short of 1 is almost always right: a walker leans into a
slope, it does not become part of it."*

Measured as the angle between each body's own up axis and world up, over 226 s:

```
  bull-1     mean 12.0  max 36.7 deg   ->   mean  6.6  max 20.3
  goat-5     mean 18.4  max 36.4       ->   mean 10.1  max 20.1
  bull-10    mean  9.5  max 40.7       ->   mean  5.2  max 22.4
  cow-12     mean  6.4  max 28.9       ->   mean  3.5  max 15.8
  pig-15     mean 10.0  max 36.7       ->   mean  5.5  max 20.1
  rooster-7  mean  6.7  max 22.6       ->   mean  6.7  max 22.6
  chicken-8  mean  6.5  max 13.0       ->   mean  6.5  max 13.0
```

`maxTilt` is 34° and the livestock were *reaching* it. **The last two rows are the control and it
was free**: the four birds already carried 0.55, they were not touched, and they did not move.

Two absences land with it. `bodyRadius` was **0 on all sixteen**, so no farm animal was a body to
the crowd field; and `footprint` was unset, so all sixteen read the ground over the engine's 0.55 m
while being 0.7–5.3 m across — a bull reads a disc a fifth of its own length. Both are now set from
the measured world half-extent (`Composition::nodeBounds`, so the 1.94× scale is already in it).

## 3. The picture, and the shot that had to be built to get it

No shot in this film frames a farm animal: every one of the 42 `cameraShotSpans` names a fungus, an
alien or the saucer. The first before/after pair was taken at `t = 48.5` and the two frames are
indistinguishable, because **the shot contains none of what changed**. A purpose-made view was
needed, and one camera 16 m from `bull-1` at 30 mm is it.

`examples/world/renders/v2modern/defect3-bull/{before,after}.png` (gitignored):

* **before** — the bull is pitched nose-down into the slope, its back tilted along the hillside,
  and its forelegs disappear into the ground: the near hoof is under the surface and the far one is
  gone entirely.
* **after** — the bull stands level, its back horizontal, and all four hooves rest **on** the
  surface.

One body, one cause, and the thing the owner called "off axis" and the thing they called "clipping
into hills" are the same twenty degrees.

## 4. A render does show what the cast did — and a targeted build does not produce one

The doubt was worth resolving because it decides whether frames can validate a cast fix at all.
`render_job.cpp` seeks before it draws; `Engine::seekSeconds` calls `EntityWorld::seek`, which
resets every body to its anchor and replays forward at 1/60 under `SeekBudget{maxSeconds = 90}`.
Measured, seeking to 17 s: the five aliens are 15.2–35.9 m from their anchors. So a frame at t is a
frame of t seconds of deciding — **up to ninety**. Past that the replay is capped and a frame at
t = 200 shows the world at t = 90 of behaviour, which is ADR-273's budget working and a real limit
on what a late frame can be asked.

The reason it was ever in doubt is the more useful half. Three render pairs came back byte-identical
across the engine change, and that was read as evidence about the renderer. It was evidence about
the operator: `--target avgen_tests` had been rebuilt all session and `src/avgen` had not, so every
render ran a binary from before the fix. The test binary said the bodies were navigable and the
render binary drew them frozen, and both were telling the truth about different executables.

**A render is evidence about the binary that produced it, and a targeted build does not produce
it.** Build `avgen` before rendering, or render nothing.

## 5. Consequences

* `src/entity/obstacles.{hpp,cpp}` and `src/scene/composition.{hpp,cpp}` change, and the change is
  engine-wide. Every world in the repository that stars a moving character gets it; a world that
  stars only scenery is unaffected, because the filter keys on whether an entity drives the node.
* `examples/world/glowmere-valley-2-multicam.scene.json` changes: sixteen `ground` blocks.
  `glowmere-valley-2` and `-song` still carry `slopeAlign: 1.0` on their livestock and will diverge,
  as they already have since ADR-344.
* `tests/unit/test_glowmere_multicam_defects.cpp` is new: five probes and two guards, loading the
  project, with the unfiltered-hero control and the four untouched birds as the two arms that must
  come out differently.

## Rejected alternatives

* **Unstar the five aliens.** It removes the defect and the film with it: `heroes[]` is what the
  Auto-director cuts to, and ADR-344's whole cut is composed around them.
* **Give the alien heroes `radius` or `height` 0** so `obstaclesFromHeroes` skips them on its
  existing guard. Those two numbers are what the director frames with (`briefFromHeroes` takes
  `max(radius, height/2)`), so it fixes the walking by breaking the shots.
* **`GoalTaste::requireReachable`.** Written, wired, measured, removed: zero cross-region percepts
  at four seeds, so it changed nothing on the world that motivated it.
* **Chase the foot-IK grounding changes for the off-axis walk.** The owner said it predates them and
  the measurement agrees: the `footprint` 0.0-vs-0.55 default fix had already reached this film —
  none of the sixteen overrides it — and what remained was an authored 1.0 in the scene.

## Revisit triggers

* **A hero that is also a body.** The exclusion is by driven-node name. A world that puts a hero at
  a node an entity drives *indirectly* -- a parent, a part -- is not covered, and the symptom is a
  body that cannot route out of its own position.
* **`slopeAlign: 1.0` in `glowmere-valley-2` and `glowmere-valley-2-song`.** Same twelve animals,
  same inheritance from the beam lab, not touched here.
* **The ninety-second seek budget.** Any frame after t = 90 shows the cast at t = 90. Nothing in the
  film's 226 seconds warns about it, and a shot composed on a late body is composed on a body that
  stopped deciding two minutes earlier.
* **`nodes/ember/position`**, still 68 m from the scene, still winning over it, and now the only
  reason one of the five was not trapped.
