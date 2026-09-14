# ADR-179: A material program that asserts emission owns the whole emission contract

**Status:** Accepted
**Date:** 2026-09-14

## Problem

The Tree of Life's "life force" is a vein pattern on dark bark. It cannot be a material emissive
value: measured here, emissive 0.20 over a near-black base made the trunk, the limbs and the canopy
one flat teal, because a tint has no edges. Turn it down and it vanishes; turn it up and the whole
surface glows. A vein is a pattern, which means a per-fragment mask, which means a `MaterialProgram`.

Separately and concurrently, the Glowmere Valley 2 work hit the adjacent failure from the other side:
a material program **overrode** the material's emission and turned hero gills green, breaking an
art-direction rule that was enforced correctly everywhere anyone was looking.

## The mechanism

In this engine a program does not modulate emission. It replaces it — `shaders/pbr_shade.wgsl`:

```wgsl
// The program's emission is a finished radiance, so the intensity lane becomes 1.
matEmissive = vec4<f32>(program.emission, 1.0);
```

So `Material::emissiveIntensity` and `Material::emissiveColor` are **discarded** for any surface
whose program writes an emission register. Emission is an assertion by the program, never an input to
it, and there is no way to author it as an input short of changing the shader.

That is the whole of it, and it is not visible from either side: the material still has an emissive
field that still accepts a value, and the program still has an output that still looks like one
channel among several.

## Decision

**A part whose material names a program that writes emission owns its entire emission contract — the
ladder rung and the audio response both.** Concretely, for the tree:

- the branch tiers carry the vein program, and their audio-driven gain is written to
  `MaterialProgram::emissionIntensity`, which is the multiplier the program's output passes through;
- the foliage carries **no** program, so its `Material::emissiveIntensity` still means what it meant
  and the audio routes onto it are untouched;
- branch materials set `emissiveIntensity = 0` explicitly, because a value that looks authored and is
  never read is worse than no value at all.

`verifyEmissionOwnership(scene)` enforces it: it fails if any entity carries both a program that
asserts emission and a non-zero material emissive, and also if an entity names a program the scene
does not carry — which resolves to nothing and silently keeps the authored material.

## Rationale

The rule could have gone the other way: keep authoring emission on the material and forbid programs
from writing it. Rejected, because the vein pattern is emission — a program that may not write
emission cannot express it, and the whole point of reaching for a program was the mask.

Given that, the only defensible position is that ownership is **exclusive and checked**. A convention
would not have survived: this exact mistake was made independently on two projects in one week, by
people who had each read the other's notes.

The check is a test rather than a comment for the same reason. An art-direction rule that passes
review and then does nothing is the failure being prevented, and a comment is not an instrument.

## Consequences

- Audio routing for the branches targets the program, not the material. A route onto
  `Material::emissiveIntensity` for a programmed surface would resolve, apply, and change nothing —
  the hardest kind of dead wiring to notice, because everything about it looks correct.
- Switching the veins off (`TreeLook::veinsEnabled`) falls back to material emissive, and
  `applyTreeLook` checks `material.program.empty()` before writing that lane.
- The rule is general and is not about trees. It should be applied wherever a program and an authored
  material meet.

## Verified vs assumed

**Verified:** that the check rejects a part carrying both, accepts it once the material lane is
zeroed, and rejects a dangling program name; that the tree's branches are on the program and its
foliage is not; that a beat raises the program's `emissionIntensity` while every programmed
material's emissive stays at zero; that the veins render as a pattern on dark bark.

**Assumed:** that `emissionIntensity` is the right lever for audio rather than a register driven by
the program's own `Audio` input. The program *can* read audio directly, which would bypass the route
system's chains and smoothing entirely — deliberately not done, but not measured against.
