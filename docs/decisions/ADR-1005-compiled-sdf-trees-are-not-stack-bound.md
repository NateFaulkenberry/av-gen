# ADR-1005: Compiled SDF trees are not bound by the interpreter's stacks

- Status: Accepted (2026-09-30), proto/procedural-space
- Amends ADR-1001 (limits) and ADR-1003 (compiled trees). Implemented in `src/spatial/sdf.*`,
  `src/scene/sdf_object.cpp` and `src/rendering/sdf_renderer.cpp`.

## Context

`SdfTree::validate` refused any tree needing more than the interpreter's 8-entry point or distance
stack, whether or not the object was compiled. A compiled tree (ADR-1003) is straight-line WGSL with no
stacks, so the check bound it for no reason. On the POC it was the limit that bound the art: the
showcase and Folding Space sit at exactly 8 nested unary operators, and the art agent's roll,
kaleidoscope, bend and a second fold together need 9 to 10. The depth limit (12) sat just behind it:
the showcase is at depth 12.

## Decision

- `SdfTree::validate(SdfEvaluator)`: the default, `Interpreter`, is unchanged. `Compiled` skips the two
  stack checks and keeps every other check (nodes, depth, arity, parameters, names, recurse nesting).
- `SdfObject::validate` validates for the evaluator the object asks for (`compile`), both when it
  loads and every frame in the renderer.
- `kMaxSdfDepth` rises from 12 to 16. The interpreter is still bound by its stacks, which it checks
  independently; the depth now bounds a compiled tree (up to 15 unary operators over a primitive).
- If a compiled object's variant is unavailable (the compile failed), the renderer falls back to the
  interpreter only when the tree fits the stacks. Otherwise the object is not drawn, with a warning,
  rather than drawn wrong by an overflowing interpreter.

## Consequences

- No existing tree changes: every tree that validated before validates the same way, and packs and
  compiles to the same program. The 12 validation renders of the POC (two projects, six audio
  conditions) are byte-identical before and after.
- The same deep tree with `compile: false` is refused when it loads, with a message that says to
  compile it.
- `tools/make_space_presets.py` checks compiled trees against the compiled limits.
- Tests: `[sdf][space]` "SDF compiled trees are not bound by the interpreter's stacks" and the GPU case
  "SDF compiled trees deeper than the interpreter's stacks render".
