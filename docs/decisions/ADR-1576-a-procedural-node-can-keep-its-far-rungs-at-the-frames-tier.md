# ADR-1576: A procedural node can keep its far LOD rungs at the frame's material tier

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-155 (the live tiers shade procedural LOD rungs past the foreground flat). This change is additive. A
  file without the key is demoted exactly as before, and the default writes no key, so every existing save is
  byte-identical.
- Code:
  - `src/scene/procedural.{hpp,cpp}`: `ProceduralGeometry::flatFarRungs`, read and written in `fromJson` and
    `toJson`, and the predicate `scene::proceduralRungDemoted`;
  - `src/rendering/procedural_renderer.cpp`: each rung's tier now comes from that predicate.
- Tests: `tests/unit/test_procedural_flat_far_rungs.cpp` (`[adr1576]`) checks four things:
  - the JSON both ways and the default;
  - that a non-boolean is refused;
  - that the key reaches the composition's flattened scene through attach and two frames;
  - the predicate, both with ADR-155's own behaviour and with the opt-out.

## Problem

The owner saw shadows and ground colour "popping" in Rainbow World's live projection, but not in its renders.

Rainbow World's ground is 48 procedural bands with four distance LOD rungs each, and its trees have three to four
screen-size rungs. The live tiers (Preview and Realtime) set `flatTierFromRung = 1` (ADR-155), so every rung past
the foreground is drawn at the flat material tier. On this scene the flat tier looks very different from the full
one: the far hills turn brown and the shaded mounds lose their modelling and their shadows. As each band or tree
crosses a rung boundary in flight, its colour and shadow jump.

Measured on an 8 s flight (88-96 s of Feline Footwear, 1080p). The pop share is the share of pixels whose value
departs from the mean of the frames either side of it by more than 0.12 (a temporal second difference):

| Tier | Pop share, mean |
|---|---|
| Offline | 1.10% |
| Realtime | 1.76% |
| High (which demotes nothing) | 0.97% |

ADR-155 measured the flat rungs on Glowmere as invisible at full size (3.07% of pixels differing). On a world whose
ground is itself a procedural LOD object, it is the most visible change in the frame.

## Decision

A procedural block may carry `"flatFarRungs": false`. Every rung of that object then keeps the frame's own tier.

- The key is optional and defaults to true. It is written only when false.
- It is not a parameter and not structural, the same contract as ADR-1545's `castsShadow`.
- Offline and High are unaffected, because their tiers demote nothing.

## Consequences

- Rainbow World sets the key on every procedural node with two or more LOD rungs: the ground bands, the trees and
  the outcrops (65 nodes). Realtime then measures 0.98%, at the offline and High level, and its frame matches the
  offline one.
- The cost is ADR-155's saving given back on those objects. It was measured live on AC (docs/prototypes/
  rainbow-world/17).
- No other shipped scene sets the key, so no other frame changes.
