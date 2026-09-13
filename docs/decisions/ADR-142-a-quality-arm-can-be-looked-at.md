# ADR-142: A quality arm can be looked at, not only timed

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** the §50 gate, for any quality reduction
**Related:** ADR-113 (`--ab`), ADR-117 (a quality arm)

## Context

§50 requires that a quality reduction be inspected in a captured frame and not only read off a
timer: an optimisation that saves 2 ms and visibly damages the image is rejected. Quality arms
(ADR-117) are the project's mechanism for expressing a reduction — and they existed **only inside
`--ab`**, which renders interleaved measurement blocks and captures nothing.

So the gate and the instrument did not meet. The only way to see an arm's frame was to edit a tier
table, rebuild, capture, and revert — which is slow enough that in practice the frame does not get
looked at, and which changes the tier for everything else in the same run.

Volumetrics is where this bites hardest, because ADR-141's numbers are about a *pass*, and
volumetric damage is banding and silhouette fringing in an atmosphere: precisely the class of defect
that costs nothing a timer can see.

## Decision

`--quality-arm <names>` applies one or more quality arms, comma separated, to an ordinary run.
Applied **after** `--tier`, so `--tier high --quality-arm volumepreview` reads as "High, except for
this one reduction" — the isolating comparison a visual gate wants. An unknown name is an error
naming the valid ones, not a silent no-op, because an arm that quietly did not apply produces a
frame that looks fine for the wrong reason.

## Consequences

The §50 evidence for ADR-139 is a set of single-axis captures that could not have been taken before:
Glowmere at `volumesteps` alone shows concentric banding across the smooth sky and almost nothing at
edges; at `volumequarter` alone it shows that banding *plus* bright fringing along the mushroom's
gills. Two different artefacts from two different parameters, separated by moving one at a time —
which is also what established that ADR-139's two axes deserved to be two.

It composes with the workload line, which now reports `volumeTarget=WxH` beside `volumeSteps`, so a
capture carries proof of the reduction it was taken under. A capture whose log says
`volumeSteps=32 volumeTarget=1280x800` is a run where `volumefull` demonstrably applied, and §49's
"explicit and measurable" is satisfied by the record rather than by the author's word.

The facility is general. Nothing about it is volumetric; `--quality-arm contact` or
`--quality-arm maskfull` works identically, and the shadow work of Phase B would have wanted it.

## Alternatives considered

**Make `--ab` capture a frame per arm.** Rejected: an A/B run's frames are measurement blocks with
warm-up and interleaving, and the frame you would capture is whichever one the schedule happened to
end on. A visual gate wants one deterministic frame at a known index under a known configuration,
which is what an ordinary `--frames N --capture` run already is.

**A `--volume-scale <f>` debug override.** Rejected as exactly the thing §49 forbids: an arbitrary
per-run quality value that no tier offers is a reduction that is not policy. An arm is a tier's own
setting, and that is what makes a frame captured under it meaningful.
