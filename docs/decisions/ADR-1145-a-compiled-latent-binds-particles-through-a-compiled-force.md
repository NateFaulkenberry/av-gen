# ADR-1145: A compiled latent binds particles through a compiled variant of the force

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 2)
**Date:** 2026-10-06
**Resolves:** ADR-1140's revisit trigger "a latent needs a compiled-only tree" (ADR-1144's kinds), and the cost of
binding matter to a 90-node anatomy.
**Implemented by:** `ParticleRenderer::compiledLatentPipeline` and the dispatch choice in
`src/rendering/particle_renderer.{hpp,cpp}`; the load-time check in `src/scene/composition.cpp`;
`latentProject` / `latentVelocityStep` over an `SdfTree` in `src/scene/particle_latent.{hpp,cpp}` (the CPU
reference).
**Tests:** `tests/rendering/test_sdf_anatomy_gpu.cpp` ("a compiled latent binds matter to a kind the interpreter
cannot run", "one compiled latent step matches the CPU reference, binding and release alike",
`[gpu][particles][latent][anatomy]`); `tests/unit/test_particle_latent.cpp` ("... refused at load unless its object
compiles"), `tests/unit/test_sdf_anatomy.cpp`; the cost in `[.perf][astral4]`.

## Context

ADR-1140's force evaluates the latent with the packed interpreter, four times per bound particle per step. The
interpreter cannot run ADR-1144's kinds at all, and its cost grows with the program: about 6 ms per million bound
particles for a 7-record mask. The authored face is 90 nodes.

## Decision

**When the latent's SDF object has `"compile": true`, the force is compiled too.** The renderer builds, once per
tree structure (`sdfCompileKey`), a compute pipeline from a copy of `particles.wgsl` in which `cs_latent`'s four
`sdfEvaluate(0u, count, pl + k...)` taps read `sdfField(...)`, with `sdfCompileWgsl`'s field appended. The pool's
latent buffer then holds the compiled table (`sdfCompileTable`, refreshed every frame, so every value stays live)
instead of the packed program. `particles.wgsl` itself is not edited: every other system's kernels, and an
interpreted latent's `cs_latent`, are byte-for-byte the module they were. A variant that fails to build is said
once and the force is off for that tree. The copy checks that it renamed exactly four taps, so an edit to
`cs_latent` that moves them fails loudly rather than silently interpreting a table.

**At load**, a latent naming a compiled object is validated for the compiled evaluator (no interpreter stacks, the
ADR-1144 kinds allowed); an interpreted one still for the interpreter. `scene::latentVelocityStep(step, tree, ...)`
is the reference: the same formulas over `SdfTree::evaluate`.

## Consequences

- Interpreted latents are unchanged (`cs_latent` is the same entry of the same module); ADR-1140's parity and
  determinism tests still pass.
- The first frame of a new tree structure builds a pipeline synchronously ([TBD] ms for the face): a live editor
  that loads such a scene hitches once, as ADR-1142's density variant does.
- [TBD measured cost table]

## Rejected alternatives

- **Compiling every latent.** It would change the module, and so possibly the arithmetic, of every existing
  latent scene, for no gain on a 7-record tree.
- **A `particles.wgsl` entry point calling a stub `sdfField` that the variant replaces.** The base module would then
  carry an unused entry and a stub in every build, and the stub's existence changes nothing the copy does not.
