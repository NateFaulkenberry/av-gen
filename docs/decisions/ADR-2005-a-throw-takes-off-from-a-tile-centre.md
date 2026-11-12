# ADR-2005: A throw takes off from a tile's centre, and lands there

- Status: Accepted (Musical Geography, CPD-R13, the owner on draft9, 2026-11-11), scene/musical-geography. Amends
  ADR-2000 (when the crouch begins) and ADR-1998 (where a walker reads the crest).
- Code: the hop's `armPos_`, the crouch's centre rule and settle (`kCrouchReach`, `kMinCrouch`, `kSettleSpeed`,
  `kCrouchSettle`) (`src/entity/behaviors.cpp`).
- Tests: `[adr2005]` in `tests/integration/test_tile_motion.cpp`: three bodies running about with 24 m homes are
  thrown by the swell. Every take-off and landing is within 5 cm of a tile's centre. Canary: with the rule off
  (crouch anywhere), one leaves 1.6 m from a centre.

## Context

The owner on draft9: "saw one alien that was slightly clipping off the edge of the tile when it jumped - let's try to
keep em more on center of tile for jumps".

Measured on draft9 (art's c5535ba6 build, 53 aliens, the CPD-R6 trace at 60 fps, 1,296 throws):
- **Take-off distance from the tile's centre:** median 0.54 m, p90 1.65 m.
- **Further than 1 m:** 397 throws.
- **Past the box's edge (1.83 m), feet over the gap:** 79 throws.
- **Landings:** identical to take-offs. A throw from a crouch goes straight up, so it lands where it left.

**Cause.** A crest arms the throw wherever the body is. ADR-2000's crouch then began on the spot, so a walker
mid-step crouched and jumped between two centres, often at a tile's edge.

## Decision

1. **A walker mid-step reads the crest of the tile it is stepping onto.** It reads it at that tile's centre, and once
   armed there its walk ends on that centre.
2. **The crouch begins only near a centre.** The body must be within 1 m, with time to settle onto the centre at no
   more than 2.5 m/s before the pop, and at least 0.1 s ahead of it. Over the crouch's first 0.4 s (or by the pop,
   if sooner) the body eases onto the centre.
3. **A missed crest lets go.** An arm whose crest went by without a crouch is dropped, and the walker looks again.

## Consequences

Draft9, before and after:

| | before | after |
|---|---|---|
| Take-off from the centre (p50 / max) | 0.54 / 2.00 m | 0.00 / 0.02 m |
| Feet past the box's edge | 79 | 0 |
| Throws | 1,300 | 1,096 (-16%) |
| Idle | 0.279 | 0.303 |

The crest timing does not regress; it improves, on the `[cpdr9]` audit:
- art's rule (off the top by more than 0.15 s): 101 of 1,300 (7.8%) before, 71 of 1,096 (6.5%) after;
- by height (a top 0.1 m higher within 0.7 s): 1 before, 3 after.

The throws lost are mostly runners whose next centre is reached too late for the crest.
