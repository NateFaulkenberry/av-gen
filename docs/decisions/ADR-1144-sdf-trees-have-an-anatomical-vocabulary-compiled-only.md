# ADR-1144: SDF trees have an anatomical vocabulary, compiled only

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 1)
**Date:** 2026-10-06
**Resolves:** iteration 3's first retirement item (`docs/prototypes/astral-forge/07-iteration-3.md`: "ellipsoid and
tapered SDF primitives (or a compiled port of the face latent)"): production's latents were capsules and boxes,
so the T01 face read as a cartoon mask.
**Implemented by:** `SdfNodeKind::{Ellipsoid, TaperedCapsule, Octahedron, Facet, Blend, Fray, FarField}`,
`SdfNode::{from, to, radius2}`, `sdfNodeIsCompiledOnly` in `src/spatial/sdf.{hpp,cpp}` (the CPU distances,
validation, JSON, packing, and `sdfCompileWgsl`'s emission); the helpers `sdfEllipsoid`, `sdfTaperedCapsule`,
`sdfOctahedron`, `sdfFacet`, `sdfSstep`, `sdfBlendWeight` and `sdfFrayScale` in `shaders/sdf_program.wgsl`; the
kinds' live parameters in `src/scene/sdf_object.cpp` (`nodeFields`); `tools/astral_face.py` (the prototype's MASK
latent in this vocabulary); `examples/astral-forge/compare-t01-face.scene.json`, `latent-entity.scene.json` and
`compare-t02-metal.scene.json`.
**Tests:** `tests/unit/test_sdf_anatomy.cpp` (`[sdf][anatomy]`: round trip with every new key, refusals by name
for every kind and for the interpreter, each distance against an independent construction, the authored face
validates compiled and only compiled); `tests/rendering/test_sdf_anatomy_gpu.cpp` ("every compiled kind matches
the CPU tree", `[gpu][sdf][anatomy]`, including the 90-node face at two times).

## Context

The prototype's anatomy (`prototypes/astral-forge/shaders/latent.wgsl`) is written with a handful of functions
production did not have: Quilez's ellipsoid bound (the plate, the almond sockets, the eye balls, the mouth
slit), a tapered capsule `sdCone(p, a, b, ra, rb)` (brow and nose ridges, the teeth, the horns), an exact
octahedron (the geometric eye), a 24-plane facet shell (the faceted half), a spatial blend between the round and
the faceted plate, a noise-scaled radius (the torn rim), and two far-field guides. The guides matter for the
matter, not the image: far from a flat feature (the mouth slit, a blade) its bound is nearly a plane, and
particles attracted from afar collapse onto that plane and draw a straight line across the frame (measured in the
prototype's TEST 01 and 02). Iteration 3 approximated all of it with capsules and boxes in a 41-node tree.

There were two routes:

- **(a) new node kinds**, authored in scene JSON like every other SDF;
- **(b) a "compiled latent"**: a node that splices an authored WGSL function, so `facePlate`/`faceEyes`/`faceMouth`
  drop in verbatim.

## Decision

**Route (a), seven kinds, appended after `shell`** (the GPU kind numbers of every existing kind are unchanged):

| kind | keys | distance |
|---|---|---|
| `ellipsoid` | `size` = radii (> 0) | `k0 (k0 - 1) / k1`, `k0 = |p / r|`, `k1 = |p / r^2|` (a bound; 0 at the very centre) |
| `taperedCapsule` | `from`, `to`, `radius` (at `from`), `radius2` (at `to`) | `|p - a - (b - a) h| - mix(ra, rb, h)`, `h` the clamped segment parameter |
| `octahedron` | `radius` | Quilez's exact octahedron |
| `facet` | `size`, `count` (4..64 planes), `offset` (support level), `speed`, `amount` (jitter) | `(max_k dot(p / size, n_k) - offset) min(size)`, `n_k` a Fibonacci sphere turning at `speed t` |
| `blend` (2 children) | `amount`, `axis`, `offset`, `smooth` | `c0 + (c1 - c0) w`, `w = amount smoothstep(offset - smooth, offset + smooth, dot(p, axis))` (no axis: `w = amount`) |
| `fray` (unary) | `amount` (< 2), `frequency`, `speed`, `seed`, `radius`, `offset`, `size`, `translation` | `f child(p / f)`, `f = 1 + amount (valueNoise(p frequency + (0, 0, speed t)) - 0.5) m`, `m` the ramp from `radius` to `offset` of `|(p - translation) size|` |
| `farField` (unary) | `translation`, `size`, `radius` (> 0), `offset` | beyond `radius` of `|(p - translation) size|`: that minus `offset`, and **the child is not evaluated**; inside: the child |

`fray` is exact for an ellipsoid: `f sdEllipsoid(p / f, r) = sdEllipsoid(p, f r)` for constant `f`, so the
prototype's torn rim (`r (1 + tear m)`) is reproduced point for point (with production's value noise rather than
the prototype's hash, so the same character, not the same tears). Every value is a live parameter
(`node/<n>/size`, `/radius`, `/amount`, `/offset`, `/smooth`, `/frequency`, `/speed`); only structure compiles.

**Compiled trees only.** The packed interpreter (`sdfEvaluate`, `evaluatePacked`) has none of them:
`SdfTree::validate(Interpreter)` refuses a tree that uses one, by name and with the remedy
(`sdf node 'ellipsoid' runs only in a compiled tree (give the object "compile": true)`), so an uncompiled object
using one is refused at load. The WGSL helpers sit in `sdf_program.wgsl`, called only by `sdfCompileWgsl`'s code:
no interpreted object's shader changes (the interpreter loop and its dispatch functions are untouched, which is the
module ADR-1142 measured moving 5192 channels for one added branch). A compiled `farField` puts its child's code
in the `else` of a branch, so the far field genuinely skips the child; the CPU tree does the same.

**The face.** `tools/astral_face.py` writes the prototype's MASK at coherence 1 in this vocabulary (90 nodes, under
ADR-1001's 96): the faceted-half plate (`blend` of an ellipsoid shell and a facet shell) with its frayed rim, the
back cut, the almond sockets and the slit, brow and nose ridges; each eye an ellipsoid turned 0.22 rad (the right
one morphed 0.85 toward an octahedron) with three pupil rings, behind a radial guide beyond 1.3; the lip rim and
22 teeth with the prototype's hashed lengths, behind an anisotropic guide beyond 1.7. The prototype's final
`smin(face, plate, k)` thickens the plate by `k / 4` where it dominates; the tool folds that into the shell
(0.0625 thicker, the cuts 0.06 smaller) instead of evaluating the plate twice. Not ported: the small face inside
the mouth (hidden by the slit at every T01 distance), the rings' 0.02 wobble, the octahedron's 0.9 squash, and the
folds' per-feature terms (eye travel, mouth tunnel), which are parameters of the prototype's conductor rather than
of the anatomy. The T02 horns latent takes the same kinds: an ellipsoid core with an ellipsoid hollow and lip,
tapered horns, and blade fins as thin ellipsoids behind far-field guides.

## Consequences

- An existing tree is untouched: the kinds are new numbers, the interpreter and every existing compiled kind's
  emission are unchanged, `structuralHash` hashes the new keys only for `taperedCapsule`, and `toJson` writes them
  only when set. Byte-identity against the pre-change binary: see ADR-1145's table (the same run).
- A compiled-only kind cannot be bound by the particle interpreter: ADR-1145 gives the latent force a compiled
  variant for exactly this.
- The node limit (96 per tree) binds the face at 90: a second face in one tree would need ADR-1001's limit raised
  for compiled trees, which have no stacks.

## Rejected alternatives

- **(b) An authored-WGSL node.** It ports the prototype text verbatim, but nothing on the CPU can evaluate it:
  no `SdfTree::evaluate`, no meshing, no CPU reference for the latent force and no parity test that is not the
  shader compared with itself; a compile error in a scene file surfaces only on the GPU; and the code cannot be
  reused by another scene except by copying it. Route (a) keeps every evaluator (CPU tree, compiled GPU, the
  surface-nets mesher) and each kind is reusable on its own: the T02 horns used four of them without new code.
- **Adding the kinds to the interpreter as well.** The interpreter's loop is the module every interpreted object in
  the repository runs; ADR-1142 measured one added branch moving a compiled scene. And an anatomy is ~200 packed
  records, which the interpreter runs at about 1 ms per million particles per 7 records (ADR-1140): the face
  interpreted would cost the particle simulation tens of milliseconds. The interpreter is not the place for it.
- **A `teeth` node** (a repeat with per-cell hashed lengths). 22 tapered capsules with the hashed lengths written
  out cost 27 nodes and no new semantics.
