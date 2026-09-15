# ADR-217: The cut cannot know what the world is doing, so let it be told one thing

**Status:** Accepted
**Date:** 2026-09-15

Two requests about the camera and the saucer, and they turned out to be one question and one
answer:

> Can we add a control to the UFO abduction event where if it's currently engaged in an abduction
> and the auto director has its attention on the UFO it remains focused on the UFO while the
> abduction is ongoing before it moves away?

> Verify that its actually pointed at the UFO too and not rotating away.

The second is a verification request, so it is answered first and with numbers.

## The verification: it is pointed at it, and the follow is what does it

ADR-202's cut is **baked** — `directFromStructure` folds the whole track into `Shot`s before a frame
is drawn and `Sequence::toTimelineTracks` writes eight `camera/position` and `camera/target` keys per
shot. A baked aim points at where the subject *was* when the keys were written. ADR-158 added the one
correction: `Composition::applyDirectedAim` translates the aim by how far the shot's hero has moved
since the cut, which for a walking mushroom-gatherer is a few metres and for a saucer crossing the
valley at 30 m/s is two hundred.

Measured on the shipped project (`tests/unit/test_camera_hold.cpp`, "The directed camera really is
pointed at the saucer while it flies"), over the frames of the first shot cut for `visitor`:

| | |
|---|---|
| how far the saucer flew from where the shot was cut for it | **122.6 m** |
| frames sampled (a quarter-second inside each end of the shot) | 328 |
| frames the follow did not run on | **0** |
| aim miss from the saucer, median | **6.39 m** |
| the same frames with the follow's contribution subtracted, median | **125.7 m** |
| angle between the camera's aim and the saucer, median | **1.3°** |
| the same, worst (inside the shot's authored aim swing — see below) | 32.4° |
| aim miss once the shot has finished swinging onto its own subject | **6.68 m** |
| the same, as an angle | **1.4°** |

So: **the camera really is pointed at the UFO, and this was not a defect.** The residual is the
deliberate framing offset — `CompositionProfile::framing` puts the subject off centre on purpose —
rather than drift, and 6.68 m at that range is 1.4°.

The unfollowed row is the counterfactual, and it is why the followed rows mean anything: a probe
reporting only "1.3°" would read exactly the same if the follow did nothing and the saucer had
happened to stand still (ADR-182). It did not stand still — it flew 122.6 m during the shot — and
with the follow's contribution removed the aim lands 125.7 m away, which is what "rotating away"
actually looks like. The follow ran on every one of the 328 sampled frames.

The worst-case angle is not a defect either, and naming what it is matters more than the number:
ADR-185 has a shot *carry its aim across the cut*, swinging from where the previous shot was looking
into its own framing over `swingWindow` -- half the shot, by default. During that swing the aim is
supposed to be somewhere other than the subject. So the head of the window is reported separately
rather than allowed to set the maximum for the whole shot, and past it the aim is on the saucer.

Four conditions have to hold for that chain, and all four do in this scene, which is worth writing
down because three of them are silent when they fail:

1. the shot's `lookMode()` is `Subject` — a `Transition` shot defaults to `Handoff` and gets no
   follow entry at all;
2. `camera/mode` is 1 (free), which the bake always writes;
3. the playhead is inside the shot's window;
4. `visitor` is simultaneously a hero, a composition node, an entity and the staging actor's body —
   the four names that have to agree for `syncHeroesToNodes` to see the saucer move. The aim is
   measured against the **hero**, so this one is load-bearing: "the aim is on the hero" only means
   "the aim is on the UFO" if the hero is on the UFO. It is — 1.86 m of authored offset, and under
   1% of the flight in drift. See the measurement note below.

**No defect. Report 4 closes as "it already worked", and the numbers are what says so.**

## The request: what the bake structurally cannot know

What *is* happening is the other half. The cut is decided from the music; an abduction is a live
state machine (ADR-210) whose beats are decided from where the animals are. Shot seven can land in
the middle of a lift and shot eight can walk out of it, and no amount of re-cutting fixes that,
because at bake time nothing knows when the lifts will be. `dwellShots` (ADR-203) holds a subject for
N consecutive shots, but it is applied uniformly across the whole track: raising it to keep one
abduction keeps every other subject for four shots too.

So this is the one fact the bake cannot carry, and it is passed as a fact rather than as a re-cut.

### Decision

`AutoDirectorSettings` gains three fields, and the whole feature is **off** unless the first is set:

```
holdScenario        ""        a stage::Staging scenario name; "" = off
holdRole            "target"  the role whose binding means "engaged"
holdReleaseSeconds  1.0       how long the camera takes to rejoin the cut afterwards
```

`scene::AimHold` carries them to the composition next to the `AimFollow` table they extend, and
`Composition::applyAimHold` runs immediately after `applyDirectedAim` — three states, which are
exactly the three the request describes:

* **on it** — the playhead is inside a shot cut for the scenario's actor. Nothing is overridden; the
  camera pose and the actor's position are remembered, so a hold can continue the framing the shot
  arrived at rather than inventing one.
* **holding** — that shot has ended and the scenario has not. The remembered eye *and* aim are
  translated by how far the actor has moved since, so the camera rides along and a saucer that flies
  two hundred metres stays the same size in frame instead of shrinking to a dot.
* **releasing** — the scenario let go. A smoothstep over `holdReleaseSeconds` back to whatever the
  cut is now doing, because a hold that ends by snapping is a cut nobody asked for.

Measured, on the shipped project, over the frames after the first `visitor` shot ends while the
abduction is still running:

| | frames after the shot | with an abduction running | held | worst aim miss | worst angle off the saucer |
|---|---|---|---|---|---|
| `holdScenario = ""` (the default) | 479 | 479 | **0** | 182.4 m | **179.8°** |
| `holdScenario = "abduction"` | 479 | 479 | **479** | **5.7 m** | **1.5°** |

179.8° is the camera pointing almost exactly *away* from the saucer: the cut had moved on to a hero
on the other side of the valley, which is the correct thing for it to do and the wrong thing for this
piece. With the hold on, the same 479 frames keep the saucer 1.5° off the aim -- the framing offset
the shot was frozen with, and nothing else, because the eye and the aim take the same delta.

### The hold ends on its own

Nothing bounds it explicitly and nothing needs to: the shipped scenario's `depart` beat carries
`release: true`, so the claim drops at the end of every cycle and `engaged` goes false for the frame
before `acquire` binds the next target. So a hold lasts at most one abduction and then lets go,
which is what "while the abduction is ongoing before it moves away" asks for. Re-arming needs the
cut to come back to the actor of its own accord.

### "Engaged" is a claim, not a beat name

The hold asks `Staging::binding(scenario, role)` — *does the scenario currently hold a claim on the
thing it works on* — and not "is the beat called `abduct`". ADR-210's entire architectural claim is
that the UFO sequence contains no UFO-specific C++, and a camera that knew the word `abduct` would
be the first line of it. A scenario that claims a subject while it is busy is the general shape;
this file knows that and nothing else.

### Where it is *not*

* **Not in `directFromStructure`.** Casting is a fold over the whole track with no world in scope,
  and a live event cannot reach it without making the bake depend on the frame the user pressed the
  button on — which would make a rendered file differ from the window that asked for it.
* **Not a second camera system.** It writes two vectors, after the shot the director already chose,
  using the same hero delta ADR-158 already applies to the aim.
* **Not on by default, and not on in the shipped project.** A cut that leaves mid-event is a mistake
  in a piece whose event is the point and exactly right in a piece where the saucer is scenery. The
  shipped Glowmere project is unchanged, so every existing render is unchanged.

### Determinism

Everything the hold reads is a pure function of the frame sequence: the scenario's own state (which
ADR-210 already guarantees is), the heroes the entity pass has just moved, and the timeline second.
No wall clock and no frame index. `Engine::seek` clears the hold's running state alongside the
director's, so the seeked second is a function of the second rather than of how the playhead got
there.

## Consequences

- `src/app/camera_director.{hpp,cpp}`: three settings, validated (a named scenario with no role to
  watch would hold for ever and is refused), serialised into the project's `autoDirector` block, and
  `installSequence` gains a `settings` parameter defaulted to `{}` so every existing call means
  "no hold".
- `src/scene/composition.{hpp,cpp}`: `AimHold`, `setAimHold`, `applyAimHold`, `aimHeld()`.
  `setAimHold` resolves scenario → actor → body entity → node itself, so the camera director never
  learns that chain.
- `src/app/application.cpp`: `--director holdScenario=,holdRole=,holdRelease=`.
- `src/ui/control_panel.cpp`: a combo listing the scenarios the loaded scene actually stages, so a
  scenario name that does not exist cannot be asked for. **And the panel's dirty check changed from
  `std::memcmp` over the settings struct to its own `operator==`** — the struct has padding, and now
  it has `std::string`s in it, so comparing its bytes was already undefined and is now wrong.

## What this cost, in measurements that were wrong

Three, and the interesting part is that each of them read as a *plausible* answer rather than as an
error. The final numbers are above; these are what it took to be allowed to believe them.

### A maximum made entirely of two frames

The first version reported a worst aim miss of **98.35 m** and concluded the follow was only removing
a quarter of the saucer's travel. It was not: `applyDirectedAim` picks its window on the engine's own
render time, and the measuring loop was counting its own frames, and the two disagree by a frame or
two at a window boundary. Those frames have *no follow on them at all*, and on a hero that has flown
a hundred and twenty metres one such frame is the maximum of everything else put together.

Two changes, and both were needed. Sampling a quarter-second inside each end removes the
disagreement; and counting the frames where the followed and unfollowed aims are the same point
(they are the same point exactly when the follow did not run) turns "I think that was an edge
artefact" into **0 frames**, which is a fact. A maximum is a fragile statistic when one bad frame can
own it, and the fix was not to take a median and hope.

### A maximum that was the feature working

Even with the edges excluded the worst angle stayed at **32.4°**, and this one is not an artefact at
all: ADR-185 has a shot *carry its aim across the cut*, swinging from where the previous shot was
looking into its own framing over `swingWindow` — half the shot. During that swing the aim is
supposed to be somewhere other than the subject. So the head of the window is now reported as its own
number rather than being allowed to set the maximum, and past it the aim is **6.68 m / 1.4°** off the
saucer. "Improved is not correct" (ADR-199) has a sibling: *anomalous is not wrong*, and the way to
tell is to name the mechanism before touching anything.

### And the probe that was measuring something else, twice

The whole verification rests on one link — the aim is measured against the **hero**, so "the aim is
on the hero" only means "the aim is on the UFO" if the hero is on the UFO. Two spellings of that
check read a disagreement of ~28.7 m and both were measuring the wrong pair of points:

* against `Composition::nodeBounds("visitor")` — but `visitor` is a `procedural` node, and
  `nodeBounds`'s procedural branch returns `boundsMin`/`boundsMax` memoised at **rebuild** time,
  which do not move when the node's transform does;
* against the node's own `nodes/visitor/position` parameter — the body's transform, which is not
  where the hero was authored to sit.

Measured properly — against `Entity::visualPosition()`, which is unambiguously where the saucer is
drawn — the hero **sits 1.86 m from the craft and drifts 1.076 m** while the saucer flies 122.6 m.
The 1.86 m is an authored anchor offset and not a fault: a hero is allowed to sit somewhere other
than the origin of the thing it names, and this one is authored 1.45 m under its node. The 1.076 m is
**under 1% of the flight**, and an order of magnitude below the 6.68 m framing offset the aim already
carries, so it cannot be what the aim error is made of. `syncHeroesToNodes` accumulates per-frame
deltas, so what it loses it loses in proportion to how far the thing went — which is why the
assertion is a fraction of the travel rather than an absolute metre. Choosing 1.0 m first, and
watching it fail at 1.076, is how that got noticed; the fix was to pick a bound that means something
rather than one that clears the reading.

The lesson is the one this project keeps paying for: a small number is the same shape of reading a
broken probe produces. So the check now carries a **control arm** — shove the hero a known 25 m and
assert the measurement notices — because without it a probe that had silently stopped reading either
position would report a rock-steady zero and look like the strongest evidence in the file.

## One thing found on the way, and not fixed here

A moving `procedural` node reports **stale world bounds** to anything that asks: `nodeBounds` returns
the instance extent memoised at the last rebuild, so on a saucer that had flown 122 m it was 28.95 m
behind. Anything that asks a moving procedural node where it is — picking and gizmos, among others —
gets the answer from the last rebuild. Out of scope here, recorded rather than fixed.

## Revisit triggers

- A scene with two scenarios that both want the camera. The setting names one; a list would be a
  different decision and is not needed by anything yet.
- A hold that should also slow the cut rather than override it. `Sequence::retime()` exists and the
  director has never called it.
