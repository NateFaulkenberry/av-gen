# The Follow/Chase discrepancy, settled by measurement

**Status:** resolved
**Date:** 2026-09-17
**Question:** the Phase 1 camera audit concluded *"Follow is the conflict — today's Follow moves the
camera"*. The owner tested Follow in the running application and observed the camera holding still
while its aim tracked the actor. Both cannot be right.
**Instrument:** `tests/unit/test_camera_presets.cpp`, which is now a permanent fixture

> **The short answer.** The source audit was **factually correct** and the observation was **also
> correct about what is on screen**. The Follow preset moves the camera **3.48 m** across its shot,
> and the engine reproduces that exactly. In the same shot the *aim* travels **40 m**. The eye is
> outrun by the aim **11.5 to 1**, at constant distance and constant height, which is precisely the
> configuration in which a moving camera looks like a stationary one.
>
> Nothing is stale. Nothing is broken. There is one design decision left and it is one line.

## What was measured

Two levels, because they answer different questions. If they disagreed, the code being read would be
stale relative to what runs; they do not.

### A. The pure function — no engine at all

`cameraFromPreset(preset, subject)` then `Shot::cameraAt(t)`, against a subject at the origin of
radius 2 m. This is what the preset *computes*.

| preset | camera travel | aim travel | shot kind | look mode |
|---|---:|---:|---|---|
| isometric | **0.000** | 0.000 | Establish | Subject |
| **follow** | **3.482** | 0.355 | **Track** | **Subject** |
| wide | 4.472 | 0.729 | Establish | Subject |
| close | 2.025 | 0.115 | Approach | Subject |
| topDown | **0.000** | 0.000 | Establish | Subject |
| tracking | 11.506 | 11.506 | Drift | Subject |
| reveal | 37.286 | 5.138 | Reveal | Subject |

Two presets hold the camera still — Isometric and Top Down, both compositions rather than moves.
**Follow is not one of them.**

### B. The running engine — real project, real bake, real frames

`examples/city/night-shift.json` loaded, its sequence replaced with one 4-second Follow shot and one
actor walking **40 m** across it, `lookAtActor` set to that actor exactly as the panel sets it.
Then 240 frames played at 60 fps through `Engine::update`, reading `scene().camera` every 60.

```
eye travelled  3.48216 m
aim travelled 40.00000 m
ratio           11.4871x
```

**3.48216 against the pure function's 3.482.** The engine and the source agree to five significant
figures. `Sequence::install` bakes `cameraAt(t)` straight into `camera/position` keys, so there is
no step between the preset and the screen at which the arc could be discarded — and there was none.

## The seven questions, answered

**1. What does the source say Follow does?** `cameraFromPreset` sets `ShotKind::Track`,
`startDistance == endDistance == 5` radii, `startElevation == endElevation == 0.42`,
`startAzimuth 0.9 → endAzimuth 0.55`, 42 mm, `LookMode::Subject`. The azimuth delta is a **0.35
radian (~20°) arc around the subject**, at constant distance and constant height.

**2. What does the running application actually do?** The same thing, measured: 3.48 m of eye travel
on a 2 m subject, while the aim is dragged 40 m by the walking actor.

**3. Why did Phase 1 conclude Follow moves?** Because it does. The audit read the azimuth pair
correctly. What the audit got **wrong** was the significance: it reported this as a *conflict with
the intended contract* without measuring the arc against the aim swing that accompanies it, and so
presented a 20° flourish as though it were a tracking shot. That overstated the size of the problem
and the size of the fix.

**4. Are there multiple Follow implementations?** **Four things are called "follow", and only the
first is the preset:**

| name | what it is |
|---|---|
| `seq::CameraPreset::Follow` | the shot preset under discussion |
| `app::ShotKind::Track`, alias `"follow"` | `shotKindFromName` accepts "follow" as a spelling of Track — a one-way alias, so JSON round-trips as "track" |
| `scene::CameraRig::followNode` | an authored camera standing at a node's live position plus a world-axes offset. **This is a positional follow, and it is the closest thing in the engine to Chase** |
| `stage::StepKind::Follow` | a staging scenario verb, unrelated to cameras |

**5. Is lower-level shared infrastructure moving the camera?** No, and the check matters because two
mechanisms could have. `applyDirectedAim` (ADR-158) writes only the **target**, never the position,
and is gated to the main camera in free mode. `CameraRig::followNode` **does** move an eye, but only
for authored rigs, and this shot uses none. The 3.48 m is entirely the preset's own azimuth arc.

**6. Does it differ between playback, scrubbing and rendering?** **It cannot.** The shot is baked to
`camera/position` timeline keys at install, and a key-interpolated track is a pure function of time.
Scrubbing to a frame and playing to it read the same keys. This is the property the sequencer's whole
bake-rather-than-evaluate design exists to buy, and it is why the camera architecture note lists
"scrub/seek/render: exact, free" for this path.

**7. What code path should each use?**
* **Follow** — the current one: `cameraFromPreset` → `Shot::cameraAt/targetAt` → baked keys, with
  `lookAtActor` supplying the live aim. Correct as an architecture.
* **Chase** — *not* this one. A baked path can only see a subject whose position is a pure function
  of time at bake (`Actor::positionAt`). Chase must follow an actor moving **for any reason**, which
  needs per-frame evaluation — `CameraRig::followNode`'s path, extended. See the note below.

## One mistake of my own, recorded because it nearly produced a second wrong answer

The first version of the engine test called `engine.seekSeconds(t)` and read `scene().camera`. It
reported **eye 0 m, aim 0 m** — apparently confirming the owner's observation and contradicting the
pure function, which would have been a dramatic "the running code is stale" finding.

It was wrong. `seekSeconds` moves the clock; it does not re-derive the scene. Parameters are applied
and `scene().camera` is rebuilt inside `Engine::update`, so the test was reading the frame it was
already on, forever. The fixed version plays 240 real frames.

An instrument that reports zero because it never advanced is ADR-182's failure exactly, and it is
worth noting that **the zero looked like the answer I was being asked to find.** That is the most
dangerous shape a vacuous arm can take. The guard that caught it was the pure function sitting beside
it: two instruments that should agree, disagreeing.

## What is actually left to decide

**Follow already satisfies almost all of the authoritative contract.** It holds its distance, holds
its height, and its aim tracks the actor. The only deviation is the 20° arc.

And whether that even *is* a deviation turns on a distinction in the contract's own wording:

> *Follow does NOT translate the camera **because the actor moves**.*

The arc is **not** a response to the actor. It is a fixed authored flourish, identical whether the
actor walks forty metres or stands still. So on the letter of the contract, today's Follow already
complies: nothing about the actor's movement translates the camera.

Two defensible readings, and the choice belongs to a person:

| reading | change | consequence |
|---|---|---|
| **the contract means "the eye is authored and static"** | `m.endAzimuth = m.startAzimuth` — **one line** | Follow becomes exactly the contract. Existing Follow shots keep their authored values in their own serialised data and are unaffected; only *newly stamped* ones change, because the preset is a stamp and not retained state |
| **the contract means "the eye does not react to the actor"** | none | today's Follow is already correct, and the 20° arc stays as the thing that stops a held shot reading as dead |

**Neither requires renaming Follow, migrating it to Chase, or touching Tracking.** The Phase 1 claim
that Follow is "the conflict" is withdrawn: it is a one-line preference, not an architectural
problem.

## What this does not change about Chase

Chase remains the real work, and for the reason Phase 1 gave rather than the one it emphasised: a
**baked** camera can only follow a subject whose position is a pure function of time at bake, and the
requirement is that Chase follows an actor moving for *any* reason. That is a live-evaluation path,
and it already exists in `CameraRig::followNode` — which resolves through `nodeWorldTransform` and so
sees modulation, behaviours and staging alike.

Groundwork already committed on that path: `followLocal` (actor-local offset, so "behind" survives a
turn), `followLagSeconds` (a **time lag** rather than an integrating spring — `target(t − lag)`), and
`followClearance` (terrain clearance). What is missing is that these live on a camera rig rather than
on a shot, which is the separation the mandate describes and the next piece of work.
