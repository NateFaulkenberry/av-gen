# ADR-1154: The engraving is cut in the tree's domain

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 7)
**Date:** 2026-10-06
**Resolves:** ADR-1152's "the lines follow the object's transform but not its domain warps"
(`docs/prototypes/astral-forge/07-iteration-3.md` §1).
**Implemented by:** `spatial::sdfDomainChainLength` (`src/spatial/sdf.{hpp,cpp}`); the engraving header's chain
count (`src/rendering/sdf_renderer.cpp`); `sdfDomainAt`, `sdfDetailDomainOf`, the Jacobian in `fs_sdf` and
`sdfGrooveLayer`'s local gradients (`shaders/sdf_raymarch.wgsl`).
**Tests:** `tests/unit/test_astral_port.cpp` ("the domain chain is the leading unary nodes ...", `[engraving]`);
`tests/rendering/test_astral_port_gpu.cpp` ("engraving domain: a turned tree engraves like a turned axis, and would
not without the chain", `[adr1154]`).

## Context

The prototype cuts its line fields in the WARPED latent domain (`warp(p)`: the twist, the bend, the spiral
implosion), so a fold bends the anatomy and its engraving together. ADR-1152 cut them in the object's local space:
a tree under a twist kept straight lines on a twisted surface.

## Decision

**The engraving's domain is the tree's domain chain**: the enabled unary nodes that lead from the root to its first
primitive or combination (Recurse excluded), at most 16. Both the packed program and the compiled table are
pre-order, so the chain is their first records, and the shader carries the local hit point through them with each
record's own `sdfWarp`: `q = warp_N(... warp_1(p))`. The header's `p1.z` holds the count. The groove gradients are
taken along the local axes through the chain's Jacobian (`J = (q(p + e x) - q) / e`, three more chain walks), so
they stay consistent with the local normal; a uniform `scale` in the chain scales the pixel footprint. Rosette and
contour centres, the region points (ADR-1149) and the frequencies are authored in the domain's units: a face under a
`scale` 0.6 is engraved in the face's own coordinates.

## Consequences

- A tree with no chain (every tree before this ADR that is engraved: a primitive or a combination at the root) takes
  the identity: `q = p` and `J = I`, so `q + J[0] e` is `q + (e, 0, 0)` exactly and the module computes what it
  computed. Measured: ADR-1145's byte-identity table (`tempered-metal`, `compare-t02-metal`, `latent-entity`).
- Displacements and ADR-1144's `fray` and `farField` pass through the chain unchanged (they move no point the
  shader's `sdfWarp` knows); a `fray` is a local radius scale, which the prototype's engraving does not follow
  either.
