# ADR-367: A value that reached the GPU and was ignored

- Status: Accepted (2026-09-19)
- Extends ADR-015 (GPU particles), ADR-040 (particles and motion), ADR-035 (the linear-depth target).
- Related: ADR-350 (a system the application runs and does not keep), ADR-360 (the same family, in
  the wind).

*Numbered 367, at the third attempt. It was written as 361, renumbered to 364 when 361-363 turned
out to be taken on branches in flight, and renumbered again when 364 landed on main as the offline
branch's viewport suspension. The number is not the point; the habit is: check the high-water mark
across every worktree, not just your own, and renumber by counting references rather than by a
blanket substitution, because the moment a second ADR of that number exists anywhere in the tree a
`sed` will silently rewrite the wrong ones. `composition.cpp` briefly held two different meanings of
one number today for exactly that reason.*

## Problem

`scene::ParticleSystem::softness` is documented as "depth fade distance (world units)". It is
authored, validated, serialised to the scene file, copied into `ParticleUniforms`, uploaded to the
GPU as `turb.z` — and read by nothing. In `shaders/particles.wgsl` the token `turb.z` occurs exactly
once, in the comment naming the fields of the struct it sits in.

This is the fourth member of a family this repository keeps finding: ADR-350's day/night settings
that no writer emitted, ADR-358's ISF inputs that `loadProject` ran too early to apply, ADR-360's
wind gate with no parameter, and now a value that travels all the way to GPU memory before being
dropped. It is the one that went furthest before being ignored, and therefore the one that looked
most alive from every vantage point short of reading the fragment shader.

**It was not dormant.** Fifteen committed scene files carry deliberately tuned values — 0.3, 0.35,
0.4, 0.5, 0.6, 0.8, 0.95, 1.2, 1.6, 3.0 — spread across `constellation`, `hyperspace`, `infinite`,
`machine`, `reassembly`, `tide` and others. Somebody sat and tuned a number, more than once, and it
has never changed a pixel.

## Decision

### 1. The fade is implemented where the value already arrives

`softParticleFade` in `shaders/particles.wgsl`, applied to the fragment's alpha in `fs_particle`
(which serves the billboard and the trail-ribbon pipelines both):

```wgsl
fade = clamp((sceneZ - particleZ) / softness, 0.0, 1.0)
```

A billboard is a flat card in a world with depth, so where it crosses a surface it draws its own
silhouette as a hard line — the single clearest tell that a volume is a sprite. Fading the card out
as it nears the surface behind it is the whole trick.

### 2. It measures view-space Z, not ray distance, because that is what the texture holds

`shaders/linear_depth.wgsl` writes `dot(p - cameraPos, cameraForward)` — view-space Z. The particle
has to be measured the same way or the fade would tighten toward the corners of the frame, where
euclidean distance and view Z diverge. `Params` carries no camera forward, but `cameraRight` and
`cameraUp` are the orthonormal billboard basis, so their cross product is the view axis up to sign,
and every drawn particle is in front of the eye, so `abs` settles the sign. No new uniform, no C++
struct change, no ripple into anyone else's work-in-progress.

Noted in passing and **not** changed here: the neighbouring `fogCorrection` compares that same
view-Z texture against `length(world - cameraPos)`, a euclidean distance. It is a real
inconsistency, it predates this change, and correcting it would move every fog-coupled particle
frame in the repository. It belongs to whoever next owns ADR-040's atmosphere coupling.

### 3. The default becomes 0, and that is the interesting part

The struct default was `0.2f`. **0.2 was never a behaviour** — it was a number that had no effect
and that the writer, which emitted `softness` unconditionally, baked into every scene file the
editor ever saved. Because no shader read it, *every particle system in the repository has been
rendering as though softness were 0*. So 0 is the only value that reproduces what those scenes
already look like, and 0 is now the default. "Off" is provably off: the shader returns `1.0` before
it touches the depth texture.

The writer is now conditional too, so a system that wants nothing writes nothing.

### 4. It becomes a parameter

`particles/<name>/softness`, hard range 0..50, soft range 0..4. A value that reaches the GPU and is
ignored is one defect; a value that reaches the GPU, is obeyed, and cannot be turned from the
application is the next one along, and this ADR would be creating it.

## The measurement

`tests/rendering/test_soft_particles_gpu.cpp`, three cases, 50 assertions, under `tools/gpu-lock.sh`.

| arm | wall behind the cloud | softness | total brightness |
|---|---|---|---|
| hard | yes | 0.0 | 3 899 627 |
| soft | yes | 4.0 | 3 024 667 (**-22.4%**) |
| **control** | **no** | 0.0 vs 4.0 | **byte-identical** |

The control is the probe that could have failed. Same cloud, same seeds, same frame count, wall
removed: `linear_depth.wgsl` writes `1e7` where nothing was drawn, so the fade evaluates to exactly
1 and the two frames must be the same bytes. If the fade had been wired to distance, to fog, or
applied unconditionally, the control is what catches it.

A third case pins the identity claim directly and asserts `ParticleSystem{}.softness == 0.0f`, so
re-introducing a non-zero default has to be a deliberate act that breaks a test.

The existing suite is unmoved: `[particles]` is 12 cases and 2 585 assertions green, including
ADR-015's 200-frame per-frame-hash determinism runs.

### What the test harness taught, which is worth more than the fade

The first draft shared one `SceneRenderer` across both arms, and the byte-identity case **failed
against itself**. `ParticleRenderer` resets a pool when the `scene::Scene*` it is handed changes
identity — and two `Scene` temporaries in one function can land on the same stack address, so the
second arm silently inherited the first arm's live particles. Only sometimes. A determinism arm
whose result depends on where the optimiser put a local is not a determinism arm, so every arm now
builds its own renderer from a named scene.

## Consequences

- **Fifteen scene files will change appearance the first time they are rendered after this**, and
  that is the feature arriving rather than a regression: those are authored values finally being
  obeyed. They are **not** migrated here. `examples/world/` belongs to another agent this session,
  and silently rewriting someone's art direction to preserve a bug is the wrong trade. The list is
  in the handover.
- `examples/treeisland/tree-of-life-floating-island` has **no particle systems at all** — its motes
  and stars are procedural point geometry — so the scene this branch owns is untouched, and that is
  verified rather than assumed.
- Unblocks the falling leaves: leaves drifting past the island's rim would otherwise have shown a
  hard card edge against the rock.

## Revisit when

- `fogCorrection`'s distance metric is corrected; the two should then share one helper.
- A particle system wants a *soft* fade curve rather than a linear ramp — the clamp is the obvious
  place, and nobody has asked.
