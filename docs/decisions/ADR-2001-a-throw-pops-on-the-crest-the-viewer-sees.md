# ADR-2001: A throw pops on the crest the viewer sees, not the first top after the fastest rise; none without one

- Status: Accepted (Musical Geography, CPD-R9, the owner on draft7, 2026-11-11), scene/musical-geography. Amends
  ADR-2000 (which top the pop is on) and ADR-1998 (which swings throw).
- Code: `Hop::visibleCrest`, `throwCrestWindow`, `throwCrestMargin`, `throwMinCrest`, the withheld swing
  (`withheldUntil_`) (`src/entity/behaviors.cpp`).
- Tests: `[adr2001]` in `tests/integration/test_tile_motion.cpp`: three bodies a tile apart, a short train on the swell
  and authored rings at art's ring spec; every pop is within 0.4 m of the highest landscape top within 0.7 s either
  side (the margin, plus room for ADR-2002 moving a pop onto a drawn top), on a crest that rose at least 0.25 m, and no more than one in ten has a drawn top (springs included)
  0.1 m higher in that window. Canary: `throwCrestWindow` 0 (ADR-2000's first top) fails the first check (pops 1.7-2.3 m
  under the crest's top). The `[cpdr9]` measure (hidden) writes every launch of a real
  project against its tile, drawn and landscape, with each body's springs split out.

## Context

The owner on draft7: "the timings still look off on some of the later aliens - or jumping randomly rather than when a
wave passes through". Art's r6 audit (CPD-R9) put the crest throws' launches within 0.05 s of a top of the planning
surface (`GroundSource::landscape`), and found the drawn crest under the body peaking 0.1-0.7 s away in 29% of them,
and asked whether the landing springs were the cause.

Measured on art's HEAD (dd3849d0, `build.py --full --characters --clouds --tiltshift`, 62 aliens, 1,637 throws; the
`[cpdr9]` measure at 60 fps, the tile each body left from, the springs split by whose landing they are):

| The surface the launch is compared with | off its top (> 0.15 s) | a top 0.1 m higher within 0.7 s |
|---|---|---|
| the landscape alone (what the throw reads) | 360 | 287 |
| landscape + the body's own landings | 378 | 298 |
| landscape + the other bodies' landings | 436 | 343 |
| drawn (all springs) | 448 | 347 |

So the springs are a minority. 343 of the 448 offsets are on the landscape itself. ADR-2000 pops at the first top
after a swing's fastest rise. On art's ground that is often not the crest:
- **Rings** (45 m/s, a 24 m ripple inside a 20 m front) pass as two or three ripples 0.5 s apart, the middle one the
  highest. The first top is the smaller one before it.
- **Two trains** and the rolling relief make a shoulder: a short top, then the crest climbs on. In some throws the
  ground rose another 0.3-2.9 m within the second after the pop.
- **Aftershocks:** a swing armed after the cooldown ended mid-passage pops on a smaller ripple after the crest has
  passed.

## Decision

`visibleCrest`, a pure function of the landscape at the body's tile (seek-safe like the rest of the throw):
1. **Later tops.** From ADR-2000's first top, a later top within `throwCrestWindow` (default 0.7 s) that is higher by
   more than `throwCrestMargin` (default 0.1 m) becomes the pop. The search runs again from that top. A climb still
   going over the chosen top at the window's end is followed to where it tops. The throw's speed is the fastest rise
   on the way (still `tossGain` x it).
2. **Earlier tops.** If a top within the window before the chosen one is higher by the margin, the crest has passed.
   That swing throws nobody, and the body does not look again on that tile until the chosen top's instant.
3. **Readable.** If the crest rose less than `throwMinCrest` (default 0.25 m, art's audit's "no visible crest") in the
   two seconds before its top, the swing is withheld.

## Consequences

On art's build, ADR-2001 alone: no throw has a landscape top 0.1 m higher within 0.7 s (287 before). The throws go
from 1,637 to 1,508. Crests under 0.5 m go from 113 to 56. The body's median apex rises from 1.41 m to 2.06 m,
because the pop now comes from the crest's highest top with the fastest rise to it. The residual offsets on the drawn
tile come from the springs (ADR-2002).
