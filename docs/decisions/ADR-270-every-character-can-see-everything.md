# ADR-270: Every character can see everything, and the only sight this engine owns costs 1.7 ms a look

**Status:** Accepted
**Date:** 2026-09-17

Perception is the one layer of the character-intelligence brief that does not exist here in any
form. This ADR is what its shape is, and the shape is decided almost entirely by one measurement.

---

## 1. What exists today: omniscience

`EntityWorld::interestPoints()` is a single list, assembled from the scene's landmarks, the entities
that move and whatever the navigation grid noticed about the terrain while it was being built.
On `glowmere-valley-2` it holds **505 entries**. Every character scores all of them, every time it
picks a goal, with:

* no range limit,
* no facing — a body notices what is behind it exactly as well as what is in front,
* no occlusion — a hill, a hero and a forest are not between anything and anything,
* no memory of having noticed. `Explore::recent_` remembers where it has *been*, not what it has
  *seen*.

Two characters in the same world take different routes only because they weight the same omniscient
list differently. That is a taste model standing in for a sense model, and it is why a character
cannot be surprised, cannot lose track of something, and cannot fail to notice.

## 2. The measurement that decides the design

`tools/charai_probe.cpp`, minima of 3 runs, load average 3.75. `world::heroSightline` is the
nine-ray silhouette occlusion test built for the camera (ADR-080), and it marches at a fixed step
in metres, so its cost is linear in how far the looker is looking:

```
world::heroSightline, 9 rays @  20 m     1720.779 µs
world::heroSightline, 9 rays @  60 m     5391.917 µs
world::heroSightline, 9 rays @ 200 m    16907.755 µs

Navigator::sample     (analytic world)     10.325 µs
Navigator::clearanceAt (grid lookup)        0.024 µs
Navigator::obstructed  (grid lookup)        0.011 µs
```

**One sightline per character per frame, at 100 characters, is 172 ms a frame.** The camera can
afford `heroSightline` because there is one camera. A crowd cannot, and it is not close: the gap is
four orders of magnitude, not a tuning problem.

The second half of that table is the constructive finding. **The navigation grid is the cheap
spatial index nobody uses for anything but pathing.** A grid lookup is four hundred times cheaper
than an analytic world sample and roughly seventy thousand times cheaper than a twenty-metre
sightline, and the only code that reads it is A*.

## 3. Decision

Perception is a **budgeted, cadenced, grid-backed stage**. It is not a sense simulation.
`Percept`, `PerceptionSettings` and `IPerception` in `src/entity/character_ai.hpp` §2.

* **Cadenced.** `PerceptionSettings::hertz` defaults to 4 — fifteen frames of staleness at 60,
  which is less than the time a character takes to turn its head. This is a budget, not a quality
  knob to be raised for realism. ADR-267's D3 permits the cadence to fall with distance; it forbids
  the *integration step* from changing, which is the distinction the coarse LOD band currently gets
  wrong by 50.263 m.
* **Bounded.** `capacity` defaults to 8 and is a hard cap, not a hint: sort by salience, keep the
  top `capacity`. A bounded working set is what makes ADR-267's D4 — memory reconstructed by the
  replay rather than persisted — possible at all.
* **Grid-backed.** Candidate filtering reads `spatial::PointGrid` and the navigation grid, never the
  analytic world.
* **Occlusion off by default, and honest about it.** `occlusionTestsPerSecond` defaults to 0,
  meaning `visibility` is 1 and `tested` is **false**. A world that turns it on has chosen to spend
  its frame on it, and the budget is per-world, served round-robin.

### The one rule that is easy to get wrong

**When the occlusion budget is spent, leave `tested` false. Never drop the percept.** A percept
silently dropped because a budget ran out makes a character's behaviour depend on how many other
characters exist — which is the class of bug that only appears in the crowd scene, only at the
worst moment, and reproduces nowhere. Reporting `visibility = 1` with `tested = true` when nothing
was tested is the same lie in the other direction; that is why `tested` is a separate field rather
than a sentinel value in `visibility`.

### Which position a percept is built from

`state().position()` — the simulation's answer — never `visualPosition()` (ADR-260). A percept
feeds a decision and a decision feeds navigation, and navigation reasons in simulation space.
Glowmere's saucer carries a `drift` of radius 2.4 m; perceived from its visual position it would be
noticed somewhere it is not standing, and a character sent to meet it would walk to the wrong place
by up to 2.4 m for reasons nothing could explain.

## 4. What it costs

Extrapolated, and labelled as such. A candidate scan at 60 m over 505 interest points selects order
1% of them — eight to twenty candidates — each costing a distance test and a `clearanceAt` at
0.024 µs. Estimate **2–4 µs per character per sense tick**, which at 4 Hz is **0.13–0.27 µs per
character per frame**: under 1% of the 89 µs an `explore` character already costs.

The estimate is dominated by the `spatial::PointGrid` radius query, which was not isolated. That is
the one number to measure before the first line of the implementation is written, and it is one
probe arm.

## 5. What is out of reach, and why

**A character noticing a specific tree.** The canopy is statistical (ADR-080): the world can say
"nine-metre trees grow around here" and cannot say "there is a trunk at this spot". Per-instance
solids do exist — `spatial::ObstacleField` holds 1,238 of them on `glowmere-valley-2` — but they
carry no identity beyond `ObstacleType`, so "that tree" is not a thing a percept can name. Giving
`NavigationObstacle` a stable id is cheap and would make the obstacle field a scene object rather
than a derived one, which is a larger decision than it looks.

**Hearing, smell, and any sense with a propagation model.** Nothing in the engine carries a field
these could be sampled from. A signal-bus-driven "a loud thing happened at P at time T" broadcast
with a radius is the honest cheap version, and it is a `signals::SignalBus` event, not a sense.
