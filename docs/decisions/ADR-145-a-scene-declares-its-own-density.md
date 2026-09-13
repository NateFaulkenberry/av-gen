# ADR-145: A scene declares its own density, and the declaration is what is checked

**Status:** Accepted
**Date:** 2026-09-13

## Problem

`docs/renderer-2-benchmark-world.md` §6 is a careful, well-conditioned table: four density rungs
with their placed instances, visible instances, draws, terrain chunks and submitted triangles,
taken on a pinned binary with contended runs excluded. It is the best record of what the benchmark
world contains.

It is also inert. Nothing fails when the renderer stops producing those numbers. The only way to
find out what `glowmere-dense` contains today is to render it and read the log — and then you have a
number with nothing to compare it against except a document written before two waves of renderer
changes landed. Phase G's brief asks for stress scenes "with the density stated in the scene rather
than discovered by reading it", and the gap it is naming is exactly this one: the density is stated,
but somewhere the renderer cannot see it.

There is a second, sharper problem underneath. A benchmark scene can stop being the scene it
benchmarks. §6 of that document captures all four rungs and looks at them for precisely this reason.
But looking is a person's job done once; a count is a machine's job done every run.

## Decision

A certification subject carries a `certification` block **inside its own scene file**, and
`tools/certify.py` renders it and compares.

```json
"certification": {
  "what": "Dense Forest, the first of §45's four stress profiles ...",
  "load": "generate", "atResolution": "1280x800", "tier": "realtime", "fps": 30,
  "declares": "...why these are checkable and the timings are not...",
  "counters": { "draws": 154, "visibleInstances": 1352, ... },
  "recordedAt": { "revision": "...", "dirty": false, "resolution": "1280x800" }
}
```

**Counters are checked exactly. Timings are not checked at all.** This is the whole design and it
rests on one property of this engine: a composition is a pure function of `(recipe, library, seed)`
— hash-addressed rather than stream-addressed, so adding a layer does not move everything after it
(benchmark-world §7) — and the culling decision is a pure function of that plus the camera and the
resolution. So `draws`, `visibleInstances`, `submittedTriangles` and the rest are **exact**. An
unequal count is a real change every time, with no noise floor to argue about and no session to
qualify it.

A millisecond has none of those properties. A declared frame time would be a cross-session
comparison with the other session hidden inside a constant, which §3.1 of the audit forbids and
which ADR-113 built a whole instrument to stop people doing by accident. The tool measures the
timings in the same run, reports their full distribution and the spread across repeats, and asserts
nothing about them.

**The declaration carries its own conditions.** `atResolution` and `tier` are part of it because
they change the answer — a screen-size culling threshold is resolution-dependent by construction —
and `recordedAt.revision` says which engine produced the numbers. `--record` rewrites the block and
stamps the revision, so adopting a deliberate change is a reviewable diff in JSON rather than an
edit to a table in prose.

**A counter that moves *between repeats of one scene* is reported separately** from one that has
changed value. They are different findings: the second is a change in the renderer, the first is a
renderer that does not render the same scene twice — which is the `SYM-TERRAIN-1` shape, and is why
Terrain is excluded from the frame-state baselines.

## Subjects

The four Glowmere rungs, which already existed and are now declared rather than documented, plus one
new scene:

**`examples/stress/open-vista.recipe.json`** — the second of the four stress profiles
`05-scope-contract.md` §45 schedules, and the one the density ladder does not cover. The ladder is a
closed understorey a few metres deep: a foreground problem, which is why it loads culling and
overdraw. Open Vista is the opposite shot — mountainous relief over 1.4 km, the sightline running to
the horizon, thin cover, and the composition weight in the background band rather than the
foreground. Same seed, same asset library, same art profile as the rungs, so a difference between
them is a difference in the *shot* and not in the content.

Measured on first run: 15.66 ms GPU against `glowmere-medium`'s 9.24 ms, 125 draws, 304 visible
instances — and a 26.3 ms wall clock against a 15.7 ms GPU, which says this profile is bound
somewhere the density ladder is not.

§45's other two named profiles, Character and the AV Gen Showcase, are **not** built here and are
not claimed. They need authored content rather than a recipe.

## Consequences

- `WorldRecipe::fromJson` ignores unknown top-level keys, so the block is inert to the engine. It is
  also **not preserved by `WorldRecipe::toJson`**: a recipe round-tripped through the world editor
  loses its declaration silently. Recorded as a known defect rather than fixed here — the fix
  belongs with whoever owns the recipe serialiser, and the failure mode is visible in `git status`.
- The tool discovers subjects by globbing `examples/**/*.json` for the block, so adding a subject is
  adding a block. Nothing has a list of scenes in it that can go stale.

## Alternatives considered

**A separate manifest listing the subjects and their counts.** Rejected: it is the document problem
again, one directory closer. The point of putting it in the scene is that the claim and the thing it
claims about cannot be moved apart.

**Assert a frame-time budget per scene.** Rejected, and it is the tempting one — it is what "the
renderer got slower" would most directly catch. It cannot be done honestly here: the number would
have to come from some other session, and §6 of the benchmark-world document measures `glowmere-low`
moving 27% between two passes of the same binary on the same machine. A budget calibrated on a quiet
day fails on a busy one, and a budget calibrated on a busy day catches nothing.

**Check the counters inside a GPU test instead of a tool.** Rejected for the heavy rungs: composing
`glowmere-extreme` takes minutes, and a test that slow either never runs or is excluded from the
suite, which is the same as not existing. The tool runs the real binary through the real headless
path with `--bench-json`, which is also the path every other measurement in this upgrade used.

## Verified vs assumed

**Verified:** the block is ignored by the recipe parser (all five subjects load and render); the
counters this run recorded, under `tools/gpu-lock.sh`; that `open-vista` composes, renders and looks
like a vista.

**Assumed:** that the recorded counters are stable across *machines*. Nothing here has run on a
second adapter, and a screen-size culling threshold is a property of the projection rather than of
the device, so there is reason to expect it — but reason to expect is not a measurement.
