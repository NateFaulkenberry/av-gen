# ADR-1670: A Light Beam can follow its owner

- Status: Accepted (scene/talking-heads; the owner's decision D3, 2026-10-24)
- Amends the Light Beam effect (Effect Library Wave 3, `catalog-light.md`). Additive: a beam without the row draws
  byte-identically.
- Code: `src/world/effects/kinds/light_beam_effect.cpp`, which adds the rows `local` and `pitch` and a branch in
  `resolveBeam`.
- Tests: `tests/unit/test_shell2.cpp` "Light Beam follows its owner in local space (ADR-1670)" (`[adr1670]`). It
  checks four things:
  - off is byte-identical to a beam without the row, including with `pitch` set;
  - the offset and the aim are carried by the node's rotation, scale and position;
  - the beam can point straight up;
  - turning the node turns the beam with it;
  - a zero-scaled owner draws nothing.

## Problem

Talking Heads needs coloured rays coming from a mannequin's mouth and eyes while the head turns, bobs and shakes. An
entity-owned Light Beam has two limits:
- it leaves the bottom of the owner's drawn bounds plus a **world** offset;
- it is aimed by a **world** yaw and a tilt from straight down, clamped to ±89 degrees.

So it neither turns with its owner nor points upward. The prep probe worked round this with one authored spot light
per beam riding the head node, and the beam owned by the light. That works, but 16-24 lights exist only to carry
transforms, and the effect is not self-contained. The owner chose the engine change.

## Decision

A `local` row ("Follow the owner", off by default) on `lightBeam`. With it on and an Entity owner:

- `offsetX/Y/Z` is a point in the owner node's **local** space, in its mesh units from its origin, carried by the
  node's drawn world matrix. That matrix includes XFORM offsets (Shake, Float...) and scale.
- The axis is `fxHeading(yaw, pitch)` in the node's frame: heading about the node's +Y (0 = its +Z), and a new
  `pitch` row (-90..90 degrees, up from the node's horizontal plane), rotated by the node's normalised basis. It covers
  the full sphere.
- `length`, `aperture` and `angle` stay in metres and degrees, so a scaled node does not fatten its beams.
- A zero-scaled node has no frame, and the beam is not drawn.

Off, or on a Light or World owner, nothing changes. The rows are ordinary effect parameters (`fx/<id>/local`,
`fx/<id>/pitch`), so routes and presets reach them.

## Consequences

- An emitter is now one effect: a fan is N beams on one entity, with no carrier lights.
- The pool light a beam asks for (`castLight`) follows the beam's resolved apex and axis as before.
- A node whose forward is +Z (glTF's facing) aims with heading 0. The rule does not consult `FxAnchor.forward`.
