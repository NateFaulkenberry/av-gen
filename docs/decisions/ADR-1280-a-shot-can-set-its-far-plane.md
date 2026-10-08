# ADR-1280: A shot can set its far plane

- Status: Accepted (2026-10-08), proto/bio-valley (the Bioluminescent Valley, CP1)
- Code: `src/scene/composition.{hpp,cpp}` (`camera/far`, scene `camera.far`).
- Tests: `tests/integration/test_film_validate.cpp` (`[adr1280]`).

## Context

The far plane was always automatic: 50 x the world's bounding radius, at least 2 km (`Composition::update`). The
Bioluminescent Valley holds a crescent moon 9 km from the camera and a band of far ranges out to 11.5 km, so its
radius is about 28 km and the automatic far plane about 1,400 km.

The valley also has a macro shot and a riverbank shot a few centimetres to half a metre above the ground, which
need a 2-5 cm near plane (ADR-1058's `camera/near`). With a 5 cm near plane and a 1,400 km far plane the depth range
is about 3 x 10^7, beyond what the depth buffer resolves: the sky and every distant surface rendered pure black,
with a hard edge where the precision ran out (measured: pixels exactly (0, 0, 0); the same frame with a 0.2 m near
plane is correct).

## Decision

`camera/far` (metres, keyable; scene JSON `camera.far`): 0 = automatic (the default, saved only when set). A
positive value is the far plane, kept at least twice the near plane.

## Alternatives

- **Move the moon closer and shrink it.** It then parallaxes between shots a few hundred metres apart, and the far
  ranges still set a ~12 km radius.
- **Derive the far plane from the camera's view distance.** There is no such single quantity: terrain, procedural
  culls and the sky each have their own.
- **Reversed-Z depth.** The right long-term fix for depth precision, but a renderer-wide change to every pass that
  reads depth. Not for a scene branch.

## Consequences

- Every existing scene is unchanged (0 = automatic).
- A shot that sets a far plane must keep it beyond everything it should see; geometry past it is clipped.
