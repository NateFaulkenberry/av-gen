# ADR-450: The nebulae get their own resolution, and ADR-390 §6's rejection is reversed

- Status: Accepted (2026-09-20).
- Reverses ADR-390 §6/§5's rejection of the ADR-139 half-resolution pattern, explicitly, on a
  measurement rather than on a re-argument.
- Extends ADR-393 (the Cosmic Ocean's dispatch, cell-boundary rule and first measurements),
  ADR-390 (the effect), ADR-170 (minima over repeats), ADR-182 (a probe that can fail),
  ADR-389 (a coefficient is invalidated by a change in the distribution it was tuned against),
  ADR-139 (the reduced-resolution buffer pattern).

---

## 1. Why ADR-390 §6 was wrong, and why the reason matters more than the instance

ADR-390 considered a low-resolution nebula buffer and rejected it as "complexity bought against a
cost that was measured not to exist". The cost it relied on was §6's calibration:

> **~0.02 ms per full-1080p-frame per five-octave fBM.**

That constant was measured on `glowmere-cosmos.wgsl`: a **2D value noise over a `sin` hash**. It was
then applied to `coFbm`, which is **3D value noise — eight corner hashes and a trilinear blend per
octave** — with a **domain warp in front of it that costs a second full fBM evaluation before the
first one is sampled.** The ratio between those two per-octave costs is roughly the factor by which
ADR-390 §6's whole budget table was wrong.

This is ADR-389's rule with "an fBM" as the quantity: *a coefficient tuned against a quantity is
invalidated by any change to that quantity's distribution, not only by a change to its units.* It
is the second time this week a coefficient has been invalidated that way, and **the pattern is now
more useful than either instance**. The general form worth carrying forward:

> A per-unit cost constant is only transferable between two things that are the same *kind* of
> thing. "An fBM" is not a kind; "a 2D value-noise fBM over a sin hash" is.

## 2. The attribution, and the statistic that decides it

1920x1080, Realtime, `--ab cosmic` and friends, 4 blocks x 200 frames, through `tools/gpu-lock.sh`,
delta read off per-block GPU **minima** by `tools/ab_minima.py`:

| component | ms | share |
| --- | --- | --- |
| **nebulae** | **1.44** | **56%** |
| cosmic dust | 0.79 | 31% |
| star strata | 0.13 | 5% |
| base: the draw, deep space, palette, recede | 0.33 | 13% |
| sum | 2.69 | against an independently measured total of 2.556 |

The components sum to within 5% of the total measured on its own, which is the check that the arms
are not overlapping.

### The same quantity, measured two ways, disagreeing by 3.3x

This is ADR-170's rule with a worked counterexample, and it is recorded because **a rule with a
counterexample survives and a rule without one gets re-litigated.**

| reading | statistic | machine | nebulae |
| --- | --- | --- | --- |
| first | `--ab` headline (**median**) | load ~20; five agents compiling and running suites | ~0.4 ms, **17%** |
| second | per-block **minima** | quiet window, load ~5 | **1.44 ms, 56%** |

These are not two estimates of one thing. On an M2 the GPU shares a memory bus with the CPU, so a
sibling worktree's CPU suite inflates every median while leaving the least-disturbed frame alone:
**the median was measuring the contention and the minimum was measuring the effect.** The first
reading was used — by me, in a report — to argue that this ADR's change would not pay. It argued the
opposite of the truth.

`tools/ab_minima.py` prints the spread of the minima within an arm, so a disturbed run announces
itself instead of being averaged into a clean one; `tools/ab_until_clean.sh` retries until that
spread is small. Both exist because of this.

## 3. What was built

The two nebulae — and **only** they — move to a pass of their own at a fraction of the frame's
resolution. Stars, planets, galaxies, dust and events stay at full resolution.

The asymmetry is the design. A nebula is the lowest-frequency thing in the frame, which is what
makes it safe to reduce. **A star is the opposite**: it is a sub-pixel point whose whole appearance
is high frequency, and halving its resolution is how a star field starts to crawl. A resolution
lever applied to the whole effect would have bought the same time and cost the thing the effect is
for.

Three decisions that are load-bearing:

**Two attachments, not one.** `cosmicOceanAt` puts the star strata *between* the two nebulae: the
far one occludes the galaxies and the stars, the near one occludes the receded result of all of
that. One buffer would mean choosing an order in the reduced pass and losing the layer in between.
**Coverage travels in alpha beside radiance**, because coverage is not decoration — it is what
occludes the strata behind each cloud, and a buffer carrying only colour would let stars shine
through a nebula.

**`RGBA16Float`, not a packed format.** `RG11B10Ufloat` is samplable everywhere and **renderable
only behind an optional device feature**; a sibling branch claimed a memory saving on it that the
hardware had not agreed to. At a quarter of each axis the pair is ~2 MB at 1080p, so the saving a
packed format would buy does not exist, and a saving that does not exist is not worth a capability
query and a fallback path. It also has to be *float*: coverage is fine in 0..1 but the radiance is
HDR, and a UNORM target would clip a bright filament silently.

**Two bind group layouts.** The reduced pass gets the uniform alone. Handing it the textures it is
about to render into is a read-write hazard the validator is right to refuse, and a dummy binding
to keep a single layout would have hidden the hazard rather than removed it.

## 4. The edges, which is where this pattern goes wrong

ADR-393's rule — *a per-cell body with no neighbour search must have a profile that is zero at the
cell boundary, and the cap belongs at the profile, not at the brightness* — has an upsampling
cousin, and a reduced buffer magnified back up is its natural home. Two things were done about it,
and then it was checked rather than asserted.

- **The sampler is `ClampToEdge`.** A bilinear tap at the frame's border reaches half a texel
  outside the buffer; `Repeat` would wrap the sky round to the opposite side of the screen and put
  a seam down two edges of every frame.
- **The fine detail is protected by a mechanism that was already there.** `pixelAngle` comes from
  the derivative of the view direction, so in the reduced pass it is automatically two to four times
  larger, and every point-like term inside `coNebula` — the shimmer above all — fades itself by its
  own solid-angle ratio. Evaluating at a quarter of the resolution therefore fades the fine detail
  *more* rather than point-sampling it and magnifying the aliasing on the way back up.

**Checked, at 20x amplification** (`renders/cosmicocean/14-quarter-vs-full-diff-x20.png`): faint
scattered speckle where stars sit, no structure, no block edges, and no seam at the frame border.

## 5. Quarter, and it is not close

Both fractions were built as arms (`cosmicnebhalf`, `cosmicnebquarter`) and rendered from the hero
camera, because which one ships is a look and not a number.

| comparison | pixels differing at all | by > 2 levels | by > 8 levels | mean |
| --- | --- | --- | --- | --- |
| half vs full | 0.75% | 0.006% | 0.0018% | 0.003 levels |
| **quarter vs full** | **0.83%** | **0.007%** | **0.0022%** | **0.003 levels** |
| *control:* nebulae **off** vs full | **80.94%** | **62.90%** | **16.61%** | **3.32 levels** |

The control is the row that makes the other two mean anything, and it is here for ADR-182's reason:
without it, "quarter looks the same as full" is equally consistent with *the nebulae are barely
drawn at all*, and the comparison would have been between two ways of rendering nothing. **The
nebulae paint 81% of the frame. Rendering them at a sixteenth of the pixels moves 0.8% of it.**

Quarter is barely distinguishable from half, and half is barely distinguishable from full, so the
fraction is chosen at the cheap end. **I would ship quarter.**

## 6. What is still open

The timing of the two arms is **not yet measured cleanly**. The machine has been disturbed
continuously — a performance regression is being bisected on it, and the two attempts so far came
back with the minima themselves spread by 19 and 20 ms, which is a number with no relationship to
the effect. The retry is queued. On the attribution above, quarter should recover most of the
nebulae's 1.44 ms and land the effect near the 1.2 ms target, but *should* is an estimate and this
ADR has already recorded what estimates are worth here.

Preview's `cosmicNebulaScale` is 0.25 and Realtime's is 1.0 pending that measurement; the point of
this ADR is that the lever exists, is correct, and is free to the eye.

## 7. Found on the way: three identical hashes

`CosmicQualityScale` gained `nebulaScale` and the aggregate initialiser in `scene_renderer.cpp` was
not updated, so it kept its default of 1.0. The reduced pass ran, wrote its pair, and the composite
ignored it — the shader's "sample rather than evaluate" flag is packed from that field.

Full, half and quarter then rendered **byte-identical frames**: sequence hash `a36722fe1b146cec`,
three times. That is what caught it. An aggregate initialiser that silently leaves a new field at
its default is the reader-without-a-writer family again, and comparing the hash of three arms that
are *supposed* to differ is what makes it loud instead of silent.
