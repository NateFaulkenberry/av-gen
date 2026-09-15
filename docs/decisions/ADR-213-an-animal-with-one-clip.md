# ADR-213 — An animal with one clip

**Status:** accepted · 2026-09-15
**Follows:** ADR-088 (behaviours), ADR-161 (rate matching), ADR-204 / ADR-226 (a stride speed is a claim about a clip)

## Context

Two reports about the Glowmere farm animals, on the same day:

> "all the new animals we added in glowmere need the same upscaling as we did to the 4 aliens, they
> are looking small compared to everything"
>
> "animal animations are also sliding"

## The sliding is a missing idle, not a wrong speed

Every farm animal declares:

```json
"clips": { "idle": "Walk", "run": "Walk", "turn": "Walk", "walk": "Walk" }
```

because the nine GLBs ship exactly one clip, called `Walk` (ADR-205). And `Gait::playbackRate` read:

```cpp
if (activity == Activity::Walk)      authored = settings.walkSpeed;
else if (activity == Activity::Run)  authored = settings.runSpeed;
if (authored <= 1e-4f) return 1.0f;   // <- every other activity
```

For `Activity::Idle`, `authored` stays zero and the function returns **1.0**. That is correct for an
asset with a real idle clip — the aliens have one, and it should play at its authored speed — and it
is exactly wrong for an asset whose idle *is* its walk cycle. A standing animal played a walk cycle
at full rate. The feet ran on the spot, which is the whole of what "the animals are sliding" meant.

It was invisible for the four aliens for the only reason that matters: they ship 26 clips and one of
them is `Idle`.

## Decision

`GaitSettings::idleRate`, default **1.0**, so nothing changes for any asset that has an idle. The
farm animals set it to **0**, which freezes the clip while the body is not travelling.

A statue caught mid-stride is not a good idle. It is enormously better than feet running on the spot,
and the better answer — an actual idle clip — is an asset question rather than an engine one. The
field is written to JSON only when it differs from the default, so every existing scene round-trips
unchanged.

`playbackRate` cannot see clip names, which is why this is data on the entity rather than a check in
the engine: whether a non-locomotion clip should be moving is a property of the asset.

## Scale

The aliens are ~3.6× their 1.67 m source meshes, which is how they reached the authored 6 m. The
animals shipped at 1.0 — their true metre sizes — so a cow stood 1.57 m beside a 6 m alien and read
as a toy. All eighteen are now at **3.6**, the world's own factor.

Scaling the node alone would have re-created the defect ADR-204 and ADR-226 each fixed once, so
`walkSpeed` and `runSpeed` scale with the body — a 3.6× animal covers 3.6× the ground per stride —
and the wander speeds scale with them so the rate matcher sits where it did.

Territories did **not** scale up. The direction was explicit — *"I will give them very little
movement areas and very little movement animation and little space to move"* — so `homeRadius` is
7 m, about two body lengths, and pauses run 12–45 s. Standing still is the normal state.

## Measured

Foot-slip warnings from the engine's own ADR-161 check, 400 frames of the shipped project:

| | warnings |
|---|---:|
| before | **8+** (bull, sheep, pig, goat, chick, cow…) |
| after `idleRate = 0` and the scale/speed pass | 3 |
| after `rateMin` 0.15 → 0.05 | **1** |

The three that survived the first pass were arrival moments — 0.15 m/s against a 2.52 m/s clip —
saturating at the rate floor. That floor exists to stop a *cruising* body's clip crawling, and an
arriving body is not cruising.

## What is not fixed

**One warning remains** and is not chased here. It is the `minDwell` window ADR-204 documented: the
gait holds `Walk` for 0.25 s after the body stops, and during it a nearly-stationary body still asks
for a walk. Bounded and known rather than silently tolerated.

**The animals' authored stride speeds were never measured against their clips.** They were set from
plausible real-animal speeds, and this ADR scaled those numbers by 3.6 rather than replacing them
with measurements — so the ratio is preserved and whatever error it already contained is preserved
with it. ADR-226 built `tests/support/stride_speed.hpp` for exactly this measurement and it has not
been run against the farm pack. That is the next honest step, and it is not this one.
