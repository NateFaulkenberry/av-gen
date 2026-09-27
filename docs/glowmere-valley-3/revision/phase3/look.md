# Phase 3, gv3-look: audio reactivity, the drop, water, wind

The stream's iteration log (brief §17). Each iteration names what changed, what was measured and
what was decided. Branch `gv3/look` in `~/Documents/GitHub/av-gen-gv3-look`, from `gv3/production`
`334c4cf6`; shared engine `040d6644`. Renders are 960×540 clips through the GPU lock; the Critic
session is `gv3-look`, one track per clip range. "Before" is always the same range rendered from
`334c4cf6`'s generator output (the first pass plus the foundation's energy arc) on the same engine.

Evidence (stills, sheets, side-by-side clips) is in
`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/look/`.

## How the reactivity is made now

`tools/gv3/reactivity.py` makes the film's reactivity a generator step:

1. **Prepare** what the Director's planner reads: two wave fields from the elder (a ring on every
   downbeat for the fungi and shelf fungi; one valley-wide ring on the drop for the beacons and the
   heroes), and the heroes ranked by how much of the cut they fill.
2. **Propose:** `avgen --project <scratch copy> --propose-reactivity` (ADR-927).
3. **Edit** item by item (`EDITS`, `DROPS`, `DROP_TARGETS`, each with its reason).
4. **Install** as ADR-927 says: routes, sources, parameters, and the plan in `directingPlans`.
5. **Audit:** `avgen --audit-routes` on the installed project; the generator prints any route or
   track that is not live.

`look.py` keeps authored only what the score writes and a detector would place wrong: the crash's
flash, the four kick gaps, the kicks the elder's heartbeat reads, and the river's bass.

## Iterations
