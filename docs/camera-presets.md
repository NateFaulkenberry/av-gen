# The ten camera presets, and what each one promises

**Status:** current
**Covers:** `seq::CameraPreset`, `seq::CameraBehavior`, `seq::ShotCamera`
**Reads with:** [camera-preset-architecture.md](investigations/camera-preset-architecture.md) (the
audit), [follow-chase-discrepancy.md](investigations/follow-chase-discrepancy.md) (why Follow holds
still), ADR-245 (the camera rig and the shot), ADR-062/071 (the shot vocabulary)

A preset answers one question: **how should this shot's camera work?** There are two ways it can
answer, and the difference decides everything else about it.

| | a **move** | a **behaviour** |
|---|---|---|
| composed around | a *point* — a subject, a radius, a distance in radii | a *performer*, resolved at every sample |
| presets | Isometric, Follow, Wide, Close, Top Down, Tracking, Reveal | Chase, Orbit, POV |
| `CameraKind` | `Move` | `Behavior` |
| needs a performer | no | **yes** — it is what the shot is about |

Both are **baked to `camera/position` and `camera/target` keys at install**, and that is why every
one of them scrubs, seeks, loops and renders identically. See *Determinism*, below.

---

## The seven moves

A move is an `app::Shot`: a kind from the fourteen-move vocabulary, a subject, a distance range in
*radii*, an aim mode and an easing. Distances in radii rather than metres is what makes one preset
read correctly against a mushroom and a mountain.

### Isometric
> A fixed three-quarter view from above, holding still.

`Establish`, 45° azimuth, fixed elevation, 55 mm, 2 samples. The eye does not move — an isometric
view that drifts stops being isometric.

### Follow
> The camera holds its authored position; the **aim** follows the performer.

`Track` at 5 radii and a fixed elevation, 42 mm, **drift 0**. Pair it with **look at** and the aim
does the following.

Follow's eye does not move *because the performer moved*, and since the drift default became zero it
does not move at all. It used to carry a 20° swing: measured at **3.48 m** of eye travel against
**40 m** of aim travel, an 11.5:1 ratio at constant distance and height that made it read as a
stationary camera to everyone who used it. The swing survives as `drift`, a per-shot control on the
azimuth pair, defaulting to zero.

**Follow is not Chase.** Follow's eye is authored; Chase's eye travels.

### Wide
> The environment, with the subject in it.

`Establish`, 14 → 12 radii, 24 mm.

### Close
> Tight on the subject.

`Approach`, 3.4 → 2.4 radii, 70 mm.

### Top Down
> A plan view from directly above.

`Establish`, straight down at 7 radii of height, 40 mm, 2 samples. Fixed on the world. To hold a
*moving* subject from above, set **look at** — or use a Chase with the offset straight up.

### Tracking
> Lateral travel with the aim held: **parallax, not a pan.**

`Drift` on a straight curve, 45 mm. The camera crosses the world while the aim stays put, so the
foreground and background separate. This is the one preset that was already exactly what it should
be, and the audit that produced this document confirmed it needed no work.

**Tracking is not Chase.** Tracking travels a trajectory of its own; Chase holds a relationship to
somebody.

### Reveal
> Start on something ambiguous, pull back until it reads.

`Reveal`, 3 → 16 radii, arcing as it withdraws.

---

## The three behaviours

A behaviour states a **relationship** and resolves it against wherever the performer is at each
camera sample. All three take a performer, an aim mode and a clearance; the rest is per behaviour.

### Chase
> The camera travels through the world holding a spatial relationship to a performer.

```
              performer
                  ↑ facing
                  |
   lateral  <-----+       "behind 4, above 2, lateral 0"
                  |
              camera  (4 m behind, 2 m above)
```

**Performer-local axes, and the convention is load-bearing**: `x` is lateral (+ is their right), `y`
is vertical, `z` is forward/back (+ is ahead of them, so a chase sits at *negative* z). That matches
the engine's own convention — `headingDegrees` builds to +Z forward, rotation about +Y — so "four
behind" stays behind when they turn. Switch `offset turns with the performer` off to hold a compass
bearing instead.

**`lag` is a time lag, not a spring.** The camera stands where the performer *was*, `positionAt(t −
lag)`. A spring integrates, so its position at *t* depends on the path taken to reach *t*, and a
scrub and a play-through would disagree — the engine has one camera like that already
(`cameraAngle_ += orbitSpeed·dt`) and it is treated as a wart, not a pattern. The cost of the lag is
that it cannot overshoot and settle; it trails and catches up.

### Orbit
> The camera circles a performer, who may be standing perfectly still.

`angle(t) = mix(start, end, t)` from **shot-local time**, never accumulated, so the same frame is the
same pose however you arrived at it. A full circle is a 360° span past the start; the direction is
the sign. The radius and the height are held all the way round — an orbit that drifts in or up is a
spiral, and the two read completely differently.

### POV
> The camera occupies the performer's viewpoint.

A performer-local eye offset (1.7 m is roughly eye height), aiming **down their line of travel**.
Aiming a POV camera at its own performer would put the target inside the eye, which is the one aim
mode this preset must not take — so it ships on `where they are going`.

---

## Aim, which is a separate decision from position

All three behaviours choose independently where they look:

| aim | means |
|---|---|
| `the performer` | at them, plus an offset — chest rather than feet |
| `where they are going` | along their direction of travel. What POV wants, and a chase often does |
| `a fixed point` | a world position, whatever the performer does |

`look ahead` samples the performer that far into the future for the aim, so the camera leads them
into a turn instead of following them round it.

---

## Clearance

`clearance` keeps the eye that many metres above the ground, applied **at bake** — the only moment
the whole camera path is known at once, so a chase that would have crossed a hill is lifted over it
before a single frame renders, rather than corrected while running.

**The ground only.** That is the honest limit and it is deliberate: the ground is what a chase camera
actually hits, it is exactly queryable from `TerrainQuery::surfaceAt`, and it cannot jitter. Trunks,
rocks and scattered instances are *not* covered — a camera squeezing between them pops, and a popping
camera is worse than one that clips a tree.

The eye is raised, never lowered: a camera legitimately above the hill it is crossing is left alone.
With no ground to measure against, the bake **warns** rather than leaving a setting that silently
does nothing (ADR-225).

---

## Determinism, and what decides it

Every preset bakes to a key track, and a key-interpolated track is a pure function of time. So:

* scrubbing to a frame and playing to it give the same camera, **exactly**;
* rendering a range and rendering the whole piece agree on every frame in the overlap;
* a shot boundary needs no reset, because there is no state to reset;
* looping accumulates nothing.

A behaviour can consume a "live" target without giving any of that up, because **`Actor::positionAt`
and `::headingAt` are themselves pure functions of time** over the performer's keys or spline path.
Asking them at each sample is asking a fact, not running a simulation.

**Which is exactly why a behaviour targets a performer and not any object.** The table of what can
and cannot be followed:

| the target moves because of | deterministic at bake? | a behaviour can follow it |
|---|---|---|
| a sequencer **Actor**'s keys | yes | **yes** |
| a sequencer Actor's spline **path** | yes | **yes** |
| a timeline track on the node | yes, in principle — not wired | not yet |
| **modulation** (audio, LFO, MIDI) | no — depends on the running signal | no |
| an **entity behaviour** (a walk cycle) | no — integrates | no |
| a **staging scenario** | no | no |

For the cases in the lower half, the tool is an authored camera rig — `scene::CameraRig::followNode`
with `followLocal`, `followLagSeconds` and `followClearance` — which is evaluated per frame and
resolves through `nodeWorldTransform`, so it sees modulation and behaviours alike. It buys generality
and gives up the free determinism; the sequencer's behaviours make the opposite trade.

---

## Trying them

`examples/camera/behaviors` is the fixture: six shots, six seconds each, one per behaviour, in the
order above. Render it and the distinctions are not arguable — shot 1 the camera does not move, shot
2 it travels with the performer, shot 3 the performer walks while the camera circles at a held
radius, shot 4 the camera *is* the performer.

`tests/unit/test_camera_behaviors.cpp` asserts each of those in the terms a viewer would use, so a
fixture nobody watched this week cannot quietly stop showing what it claims.
