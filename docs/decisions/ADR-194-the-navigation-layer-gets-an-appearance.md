# ADR-194: The navigation layer gets an appearance, and the overlay gets a camera pointed at it

**Status:** Accepted
**Date:** 2026-09-14

## The report

*"The navigation layer is completely invisible in the editor."*

It was, and worse than that: the data had been sitting there unread since ADR-093. `Explore`
publishes `route()`, `routeLeg()`, `destination()`, `hasDestination()`, `lastPathStatus()` and
`phaseName()`, under a comment that says exactly what they are for:

> What this character is doing, for a debug overlay (§46) and for a diagnostic. The editor owns the
> drawing; navigation owes it the data, and a route nobody outside this class can see is a route
> nobody can tell is wrong.

Nothing called any of them. A grep of `src/ui/` for `NavGrid`, `Navigator`, `PathStatus` or
`waypoint` returned zero hits. `NavGrid::shorePoints()` and `vistaPoints()` — the interest points
the grid extracts for free while it builds, and the places `explore` actually walks to — had never
been read by anything at all.

So two questions had no answer on screen. **"Why is it going that way"**, and the worse one,
**"why is it not going anywhere"**: a character standing still because its goal came back
`Unreachable` looks exactly like one that is idling, and the reason existed, typed, one virtual call
away.

## What is drawn

Four things, in the viewport overlay beside the selection box, the hero markers and the parent link
(ADR-188), because that is where a per-object annotation belongs and because it is the only surface
in this project that can draw *text*.

**The route.** The waypoint polyline from the walker forward, with the leg it is actually walking
picked out brighter and thicker, a tick at each waypoint so a string-pulled path reads as the
several legs it is, and a ring and a stalk at the destination.

**The path status and the phase, as a label over the character.** `wanderer walk -> water [ok]
leg 1/2`. This is the part that did not exist anywhere, in any form — not in the log, not in a
panel — and it is the half of the feature that answers the harder question. A route that failed
draws the label in the same refusal red a placement that will not take uses, because the two mean
the same thing to a person and giving them two colours would be two things to learn for one fact.

**The grid**, as cells: walkable, water, steep, blocked, and walkable-against-an-edge, or coloured
by connected region instead. The region mode is the one that answers "my character will not cross
the valley": two cells the same colour are reachable from each other and two cells different
colours are not, whatever the distance between them.

**The shore and vista points.** Free, computed, and never looked at until now.

### From the selection, and the grid from the camera

Everything per-entity is drawn from the selection, for the reason ADR-188 and the hero markers give:
a world of always-on markers is a cat's cradle over the scenery. A route is the worst of the three
to leave on, because it is a polyline that moves every few seconds.

The grid is not per-entity — it is the world — so it is off by default and bounded by a radius
around the point the camera is *aimed at* rather than where the eye is. A camera two hundred metres
up looking at a valley would otherwise paint the grid under itself and leave the valley bare.

### Which authority's route

An action or a director override preempts a behaviour and the behaviour keeps its plan while it
yields (ADR-091/096), so drawing the behaviour's route while an action is walking the body somewhere
else would draw a line nothing is following. `ActionQueue` grew `route()` and `routeLeg()` for that,
and the overlay prefers them. A `move` with no planner installed routes straight at its goal and is
reported as the one waypoint it holds — that straight line through a lake is exactly the thing worth
seeing.

### Where the toggles are, and why not Performance

In the **World panel's Debug tab**, under a `Navigation` separator, below `Entity bounds`,
`Transform trail` and `Skeletons`.

The questions this answers are asked while looking at a character in the world, and that is the tab
a person is already in when they are looking at entity diagnostics. Performance is where you go when
a frame is too slow; nothing here is about frame time, and filing it there would be filing it by
what it is made of rather than by what it is for.

The overlay is drawn by the viewport from `EditorVisuals`, not by the debug line layer, so the
switches live on `WorldEditor` and the panel is handed a pointer to it. `DebugViewOptions` would
have been the wrong home for a state the thing that draws it cannot see.

### Nothing inert

Every one of these switches can legitimately draw nothing: no entities, no navigation graph
(`navCellSize 0`), nothing selected, the camera pointed off the grid. A checkbox whose effect cannot
be seen is indistinguishable from a checkbox wired to nothing, which is this project's recurring
failure, so the editor publishes `navStatus()` and the panel prints it:

```
grid 154x154 at 4.0 m  21743 walkable / 218 water / 838 blocked  24 region(s), largest 21694  built in 171 ms
drawing 2376 cell(s) within 110 m of the view
172 shore point(s), 67 vista point(s)
1 selected entity/entities
```

and, when there is nothing:

```
no entities in this scene, so no navigation graph was built
no entity selected, so no route is drawn
```

`Regions` and `grid radius` are settings *of* the grid, so they are greyed while the grid is off
rather than merely labelled: a control that responds and does nothing is the same defect as one
wired to nothing, and harder to notice.

## The instrument this needed first

This repository states in two places that it cannot screenshot an ImGui frame, and the world editor
is built around it — every decision it makes lives in an ImGui-free translation unit so it can be
asserted instead of looked at. That is right for the decisions and useless for the drawing. "Is the
label on the character or off the side of the canvas" and "is a grid cell at alpha 46 visible at
all" are not assertions.

So `tools/overlay_shot.cpp` builds a Dear ImGui context with **no backend**, runs the real
`drawViewportOverlay` against a real scene, and rasterises the draw list in software. Nothing in it
reimplements any part of the overlay — it is the same call with the same `WorldEditor`, and the
moment it held a second copy of anything it would start lying. `--background` composites the result
over a frame `avgen --capture` rendered from the same camera, and `--panel` draws the World panel's
Debug tab, so the switches can be looked at too.

**It found two things that reasoning had not.** The status label ran off the right-hand edge of the
canvas and was cut in half by the clip rect whenever its character was near the edge — labels are
now held inside the canvas, sliding rather than vanishing, which fixes the hero and parent-link
labels along with this one. And the walkable green at alpha 46 washed out a lit frame badly enough
to make the world underneath hard to read; it is 30 now.

## Measured

Glowmere, 1280x720, the median of 200 collect-and-draw pairs, CPU only — no device is involved in
any of this, which is why there are no GPU-lock conditions to record.

| what is on | cells drawn | collect | draw |
|---|---:|---:|---:|
| route only (the default) | 0 | 0.002 ms | 0.006 ms |
| grid, 40 m | 312 | 0.004 ms | 0.052 ms |
| grid, 80 m (the default) | 1,252 | 0.011 ms | 0.194 ms |
| grid, 110 m | 2,376 | 0.020 ms | 0.370 ms |
| grid, 200 m | 7,856 | 0.053 ms | 1.187 ms |
| grid, 400 m | 12,000 (capped) | 0.123 ms | 1.849 ms |
| shore + vista points | — | 0.002 ms | 0.229 ms |

About **0.15 µs per cell**, nearly all of it in the draw: four world points projected and one filled
quad each. The radius is the whole cost knob and the status line reports what it bought, so nobody
has to guess. `kNavCellCap` (12,000) is the backstop: at 400 m it is already binding, and it exists
so a 0.5 m grid over a kilometre of world cannot turn a viewport into a slideshow without saying so
— the status line names the count it capped off.

**The route overlay is free.** That matters more than the grid numbers, because it is the half that
is on by default.

## What is deliberately not done

**A failed route is not covered by a test.** The colour and the label are wired and were read off
the code path, but forcing a `PathStatus::Unreachable` in a unit test needs a world with two
disconnected regions and a walker standing in the smaller one, and building that is building a
terrain generator's output rather than testing an overlay. The arm that *is* non-vacuous — an
entity whose behaviour navigates nothing reports "no navigation" while one that navigates does not —
is in the suite, and it is the arm that would catch the collection silently doing nothing.

**`avgen` has no camera override on the command line**, so composing the overlay over a rendered
frame means writing a copy of the scene with the camera moved. That is friction in the instrument,
not in the overlay, and it is recorded here rather than fixed in a change about navigation.

**The grid is drawn with no depth test**, like the whole ImGui overlay, so a cell behind a hill is
drawn over it. On Glowmere's terrain this is right more often than not — the point of the grid is
that it is a plan view of what is walkable — but on a world with overhangs it would read as wrong.
The debug line layer has a depth-tested mode and this does not; moving the grid there is the fix if
anyone wants it, at the cost of the labels, which that layer cannot draw.
