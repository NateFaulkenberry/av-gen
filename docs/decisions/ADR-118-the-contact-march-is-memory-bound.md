# ADR-118: the contact march is the frame's largest fragment item, and it is memory-bound

Status: accepted

## Context

§4.1 of the audit attributed the scene pass by removing one subsystem at a time and found that
nothing removable accounted for it -- except the shadow mask, whose *removal* cost 5.8 ms. That made
the mask a cost-saver rather than a cost, and Phase B's lead task (§27) was to find what the
screen-space shadow work is actually spending now that LOD0 has moved the denominator.

Everything below is Glowmere at 1280x800, realtime tier, `cmake --preset release`, Apple M2 Max,
Dawn/Metal, `tools/gpu-lock.sh` held, three interleaved pairs of 120 frames per arm in one process
(ADR-113/117). Baseline block spread is quoted with each result.

## What the shadow work costs now

| arm | GPU delta | scene-pass delta | verdict |
| --- | --- | --- | --- |
| `contact` -- no screen-space contact march | **+2.36 ms (+17.6%)** | 10.75 -> 8.39 | a result (spread 0.98%) |
| `shadowmask` -- no mask pass, term per pixel | -0.79 ms (-5.9%) | 10.68 -> 11.80 | a result (spread 1.49%) |
| `maskfull` -- mask at full resolution | -0.66 ms (-4.9%) | 10.62 -> 11.67 | a result (spread 1.48%) |
| `pcss` -- PCF instead of PCSS | +0.20 ms (+1.5%) | 10.68 -> 10.68 | **not a result** (spread 0.98%) |

Three of those move the record.

**The contact march is 2.36 ms of a 13.43 ms frame -- 22% of the scene pass.** It is the largest
single fragment item anywhere in this renderer, and Phase A never separated it, because it is not a
pass and not a subsystem: it is twelve lines inside `evaluateLight`, run per directional light per
fragment invocation, and no toggle reached it until ADR-117.

**The mask's win has collapsed from 5.8 ms to 1.12 ms of scene pass.** Same arm, same scene, same
instrument. ADR-112 is why: with the range shortened, most distant fragments now fail
`shadowLookup` and never reach the blocker search, so there is far less for the mask to save. The
5.8 ms in §4.1 is stale by a factor of five and must not be quoted. The mask is still a win and is
still not a deletion candidate -- it is just a 1.1 ms one.

**PCSS is no longer a cost.** ADR-111 decomposed the mask's residual and found PCSS the single
largest contributor to it. Its *time* is now inside the noise floor: turning it off changes nothing
measurable. Same cause.

## Two optimisations that did not work, and why that is the finding

The contact march is twelve iterations of: a 4x4 matrix-vector product, a perspective divide, a
`textureLoad` from the linear-depth target, and a compare. Two ways to make it cheaper were
implemented and measured. Both failed, in opposite directions, and together they say what the march
is bound by.

### Making the march incremental: no change

`viewProj` is linear, so the clip position along a straight march is affine in the step index:
`clipOrigin + clipStep * s`, with `clipStep = viewProj * vec4(toLight * stepSize, 0)`. The same
holds for the view depth. That replaces twelve matrix-vector products and twelve dot products with
two of each plus one fused multiply-add per step -- roughly 300 fewer multiply-adds per fragment per
light, an identity rather than an approximation.

Measured by alternating two pinned shader trees process by process, four pairs, same session:
old 13.17 / 13.96 / 13.63 / 13.24, new 13.37 / 13.37 / 13.30 / 14.75. **Medians 13.44 and 13.37 --
nothing.** Removing essentially all of the loop's arithmetic changed the frame by nothing
measurable, so the loop is not spending its time on arithmetic. It is spending it on twelve
*dependent* texture loads, each at a coordinate the previous iteration's arithmetic produced.

Not kept. It also moved 19 pixels of 1,024,000 through float rounding, and a change that alters the
image for no measured benefit has nothing to recommend it.

### Skipping the march where its answer cannot matter: 4.4% *slower*

The march is combined as `min(visibility, mix(1, contact, strength))`, and `mix(1, contact, strength)`
is bounded below by `1 - strength` whatever the march finds. So a fragment whose shadow-map term is
already at or under that bound takes the same value either way and the twelve loads are spent on a
number that is discarded. Adding `visibility > 1.0 - contactStrength` to the branch is exact in both
directions, and it is: **0 of 1,024,000 pixels differ.**

Eight interleaved process pairs, run in both orders so the ordering is not the cause:

| | with the skip | without |
| --- | --- | --- |
| GPU median | 14.09 ms | 13.50 ms |
| scene pass | 11.60 | 10.98 |

**+0.59 ms, +4.4%, slower, in 7 of 8 pairs.** A bit-exact early-out that does strictly less work
made the frame materially slower.

The mechanism is divergence. The branch guarding the march used to be *wave-uniform* -- it tests
`light.tangent.w` and `light.up.w`, which are properties of the light and identical across every
lane shading that light. Adding a per-fragment term makes it lane-varying, and a lane-varying branch
around a twelve-iteration loop with a dependent texture read in it is not free to enter: every lane
in the wave pays for the loop if any lane takes it, and the compiler must now handle the divergence
around it instead of hoisting a uniform test.

**Reverted, and recorded here at length, because "skip the work where it cannot matter" is the first
thing anyone will try.** It is exact, it is obviously correct, it is strictly less work, and it is
slower. In a fragment shader, saving work per *lane* saves nothing unless the whole wave skips.

## What follows

The march is bound by dependent depth-buffer reads, so the levers that can work are the ones that
change how many *fragments* march or how many *reads* each does, not how much arithmetic each does:

* **Fewer steps.** `contactSteps` is already policy-exposed and already tier-scaled (8 / 12 / 16 /
  24). Untested against image quality here, and a quality reduction, so it needs its own measurement
  and is not made by this ADR.
* **Marching once per pixel instead of once per fragment invocation.** §4.5 established that this
  renderer runs more fragment invocations than it has pixels, so a full-resolution screen-space
  contact pass would do the march fewer times at the same resolution. Sized rather than guessed: the
  march costs 2.36-2.95 ms at 1.02 Mpx and 4.39 ms at 2.05 Mpx, which fits ~0.9 ms fixed plus
  ~1.7 ms per megapixel -- so about a third of its cost at 1280x800 is resolution-independent and is
  the most such a pass could recover, before the pass's own cost. Against that, ADR-087 measured the
  contact term at *half* resolution blackening dense ground cover, and ADR-111 measured a
  depth-reconstructed normal moving 0.085% of the frame. **Not attempted. Recorded with its
  numbers so the next person decides on them rather than on the idea.**
* **Nothing about the cascade lookup.** It is masked, the mask is 1.1 ms of win, and PCSS inside it
  no longer measures.

## Not fixed

`shaders/water.wgsl` still calls `shadowFactor` with the wave normal (ADR-111 found this and left
it); water is not masked and pays the full lookup per pixel. Glowmere's water is a small share of
the frame -- the `water` arm is inside the noise floor -- so there is nothing to measure it against
here.
