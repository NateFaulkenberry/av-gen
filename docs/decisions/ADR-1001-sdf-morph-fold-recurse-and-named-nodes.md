# ADR-1001: SDF morph, fold, recurse, and named nodes

- Status: Accepted (2026-09-29), proto/procedural-space
- Extends ADR-027. Implemented in `src/spatial/sdf.*`, `shaders/sdf.wgsl` and `src/scene/sdf_object.*`.
- Tests: `tests/unit/test_sdf_space.cpp` and the parity list in `tests/rendering/test_sdf_gpu.cpp`.

## Context

The Procedural Space POC (ADR-1000) needs three things the ADR-027 vocabulary lacks:

- structural states that one parameter can switch or morph between;
- folds beyond axis mirrors;
- recursion, that is, nested architecture.

It also needs node parameters that a person can read. `sdf/space/node/17/size` means nothing to the
art agent writing routes.

## Decision

- **`morph`** is a combination. With `a = clamp(amount, 0, k-1)` and `i = floor(a)`, it gives
  `d = c_i + (c_{i+1} - c_i)(a - i)`. Only `c_i` and `c_{i+1}` are evaluated and packed, so a settled
  state costs one structure. The rejected alternative, a sequential fold over every child, costs every
  state on every step and loses precision against the far value (`1e9 + (c - 1e9)` is not `c`).
- **`fold`** reflects the half-space behind the plane `dot(p, n) = offset`. It is exact while the
  content does not cross the plane.
- **`recurse`** evaluates its child at levels `0..count` (at most 8), with
  `p_{l+1} = conj(R) · fold(p_l) · scale − translation`. The fold takes `|p|` on the axes where
  `size > 0`. It returns `min_l child(p_l) / scale^l`. The packed interpreter runs this as a loop: the
  END record stores its BEGIN index in `fieldSlot` and jumps back, and up to `kMaxSdfLoops = 2` loop
  frames may nest. The folds, the rotation and the uniform scale all preserve distance, so the result
  stays a bound that full-stride sphere tracing may use.
- **Named nodes:** `name` (letters, digits, `_` and `-`, not all digits, unique within the tree) makes
  a node's parameters `node/<name>/<field>`. Unnamed nodes keep the index.
- **`count` (int) and `axis` (vec3)** become parameters on the kinds that use them: repeat,
  polarRepeat, recurse and fold.

## Consequences

The kind numbering moved: `morph` sits at 14, and `fold` and `recurse` sit at 23 and 24, which puts
the displacements at 25-28. Trees serialise by name, so no file changes. The CPU enum and the WGSL
constants moved together, and the parity tests check the pair. The graph editor's SDF nodes do not
offer the new kinds yet.
