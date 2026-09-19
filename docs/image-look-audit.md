# Image / Look audit

Companion to [`docs/image-look-spec.md`](image-look-spec.md) (the revised §51–89 brief) and to
[`docs/image-formation.md`](image-formation.md), which this document verifies rather than replaces.

The spec's §5 asks for one thing before any code: read `docs/image-formation.md` and
`src/scene/post_settings.hpp` in full, then confirm, correct or flag as missing **every row of the
existing pass table**. That is §1 below. §2–§4 are the additions the spec asks for: the colour-space
map, the resource table, and where the two renderers agree and differ.

Everything here was read against `src/` at `8f23d2ec` (the merge of `agent/footik`), which is the
commit `agent/imagelook` branched from.

---

## 1. The pass table, verified

`docs/image-formation.md` lines 16–34 give an eleven-row chain. Nine rows are confirmed exactly as
written. Two rows are wrong and one row is missing entirely. The table below is the verification;
the notes after it carry the evidence.

| # | doc row | verdict | evidence |
|---|---|---|---|
| — | scene shading (HDR RGBA16Float) | **confirmed** | `PostProcessor::kHdrFormat = RGBA16Float`, `src/rendering/post_processor.hpp:129` |
| — | volumetric atmosphere | **confirmed** | runs before `PostProcessor::run`, `VolumeRenderer::enabled` gate at `scene_renderer.cpp:3128` |
| — | user post layers (`LayerStage::Post`) | **confirmed** | `scene_renderer.cpp:3614` filters `LayerStage::Post`, composited into `finalHdr` before `postIn.sceneHdr` is set at `:3649` |
| 1 | metering `fs_meter_prefilter`, `fs_meter_reduce` | **confirmed, one detail corrected** | `encodeMetering`, `post_processor.cpp:352`; called only when `exposure.mode == Automatic`, line 481 |
| 2 | exposure `fs_exposure` — "skipped when the scale is 1" | **corrected** | skipped when `abs(exposure - 1.0f) <= 1e-3f`, `post_processor.cpp:486`. A *deadband*, not an equality |
| 3 | depth of field `fs_dof` | **corrected — it is a shared defocus pass** | `post_processor.cpp:496`; runs for depth of field **or** the ADR-079 tilt-shift band, or both |
| 4 | motion blur (3 passes) | **confirmed** | `post_processor.cpp:522`; `fs_velocity_tile_max`, `fs_velocity_neighbour_max`, `fs_motion_blur` |
| 5 | lens `fs_lens` | **confirmed** | `post_processor.cpp:559` |
| 6 | bloom `fs_prefilter`, `fs_downsample`, `fs_upsample` | **confirmed** | `post_processor.cpp:570` |
| 7 | halation / anamorphic `fs_halation_prefilter` … `fs_wide` | **confirmed, two couplings undocumented** | `post_processor.cpp:618`, `664` |
| 8 | composite `fs_composite` | **confirmed — and it always runs** | `post_processor.cpp:726`, no enclosing condition |
| — | **FXAA `fs_fxaa` (ADR-059)** | **MISSING FROM THE DOC** | `post_processor.cpp:756`, `shaders/post.wgsl:718` |
| 9 | sharpen `fs_sharpen` | **confirmed, but the doc places it one row too early** | `post_processor.cpp:772`; FXAA precedes it |
| 10 | tone map (AgX default) | **confirmed** | `shaders/tonemap.wgsl:122` `fs_main`; `TonemapOperator::AgX` default, `post_settings.hpp:120` |
| 11 | output: vignette, film grain, sRGB encode | **confirmed** | `shaders/tonemap.wgsl:110` `linearToSrgb`, `:116` `hash12` |

### 1.1 The missing row: FXAA

The doc's numbered chain goes `8. composite → 9. sharpen`. The code runs a third output stage
between them. `PostProcessor::run` §7 (`post_processor.cpp:756`) encodes `fs_fxaa` when
`in.antialias && s.antialias > 1e-4f`, and the code comment states the ordering rationale the doc
never gives:

> Before sharpening, because sharpening an aliased edge fixes the contrast and keeps the stair
> step; and inside the HDR chain, where the pass can still be skipped without a target copy.

The pipeline member `fxaa_` (`post_processor.hpp:216`), the parameter `post/output/antialias`
(`post_settings.cpp:113`) and the ADR-187 diagnostic arm `PostFrameInputs::antialias`
(`post_processor.hpp:68`) all exist. `post_processor.hpp`'s own header comment (line 10) *does*
list `[fxaa]`, and so does `docs/image-look-spec.md`'s quoted chain. Only
`docs/image-formation.md`'s numbered table omits it — it predates ADR-059 and was not revisited.

**This is the single clearest "the map is older than the territory" finding in the audit**, and it
matters beyond bookkeeping: FXAA is a *spatial* filter running on scene-linear HDR immediately
before the tone curve, so anything §68 adds near the end of the chain has to decide which side of it
to sit on.

### 1.2 The two corrected rows

**Row 2, exposure.** The doc says "skipped when the scale is 1". The code skips when the scale is
within `1e-3` of 1. That is a deadband, and it is the right behaviour — an automatic exposure
settling asymptotically would otherwise encode a full-resolution pass forever to multiply by
1.0000001 — but "skipped when the scale is 1" invites the reader to assume a scale of 1.0005 is
applied. It is not. Recording it because §60's byte-identity proof depends on knowing exactly which
passes an "everything off" arm runs, and this is one of only two thresholds in the chain that is not
either `> 0` or `> 1e-4`.

**Row 3, depth of field.** The doc's row is labelled `depth of field  fs_dof  (needs undistorted
depth)`, and the doc discusses tilt-shift nowhere in the pass table. ADR-079's tilt-shift band is
not a separate pass: it is the *same* `fs_dof` gather with a different circle-of-confusion function,
and the shader takes the larger of the two circles. The gate is

```
(dofEnabled && dofMaxRadius > 0) || (tiltShiftEnabled && tiltShiftMaxRadius > 0)
```

so a scene with `post/dof/enabled` false and `post/tiltShift/enabled` true runs a pass the doc's
table says only depth of field can run. `src/scene/post_settings.hpp:83–96` documents the tilt-shift
properly; `docs/image-formation.md` never mentions it. The correct row label is **defocus (depth of
field and the ADR-079 tilt-shift band, one shared gather)**.

### 1.3 Confirmed, with couplings the doc does not state

**The halation pyramid borrows two of bloom's parameters.** `post_processor.cpp:618` builds the
halation pyramid with `std::clamp(s.bloomLevels, 1, 8)` levels and passes `s.bloomKnee` as the
halation prefilter's knee. There is no `post/halation/levels` and no `post/halation/knee`. So
`post/bloom/knee` — documented and presented as a bloom control — silently also sets the softness of
halation's threshold. Not a defect, but an author tuning bloom's knee is moving halation too and
nothing tells them.

**The bloom pyramid is built for anamorphic even when bloom is off.** `pyramidOn = bloomOn ||
anamorphicOn` (`post_processor.cpp:574`). The doc states this correctly at line 239–240. Confirmed.

**The composite always runs.** `post_processor.cpp:726` has no enclosing `if`. The doc says so at
line 36–37 and is right. See §1.4, which is where this stops being a footnote.

### 1.4 The disabled-path guarantee: the doc is right and the header is wrong

Two statements in the tree disagree, and the spec quotes the wrong one.

`docs/image-formation.md:36–37`:

> Passes that are not needed are not encoded: with everything off and a unit exposure scale, the
> chain is **one pass (the composite, which always runs because it carries the grade)**.

`src/rendering/post_processor.hpp:17–18` — and `docs/image-look-spec.md` §1 quotes exactly this as
"§60. It exists":

> Passes run only when their settings are active; with everything off and a unit exposure **the
> input is returned unchanged.**

The header's claim is false as written, and the doc's is true. The input is *not* returned
unchanged: `run()` always encodes the composite and always returns a pool texture, never
`in.sceneHdr`. And `fs_composite` at default settings is not the identity function. With
`contrast = 1`, `saturation = 1`, `temperature = tint = hueShift = 0`, `lift = 0`, `gamma = 1`,
`gain = 1` and no depth layers, a pixel still goes through

```wgsl
color = 0.18 * pow(max(color, vec3(1e-5)) / 0.18, vec3(1.0));   // log-space contrast, exponent 1
color = mix(vec3(luminance(color)), color, 1.0);                 // saturation about luminance
color = pow(max(color * 1.0 + 0.0, vec3(0.0)), vec3(1.0 / 1.0)); // lift / gamma / gain
```

Three things happen to a value that is supposed to be untouched:

1. `pow(x, 1.0)` is not guaranteed to return `x`. On a GPU it lowers to `exp2(1.0 * log2(x))`, which
   for most finite `x` is off by an ulp or two. It runs **twice** (the contrast and the gamma).
2. `max(color, 1e-5)` **clamps every channel below 1e-5 up to 1e-5**. A true black pixel leaves the
   composite at 1e-5, not 0. Scene-linear 1e-5 is far below anything AgX will resolve, so it is
   invisible — but it is not "unchanged", and it means the disabled chain is not idempotent.
3. the divide by `0.18` and multiply by `0.18` do not cancel exactly in binary floating point.

None of this is a bug to fix — the composite carries the grade and has to run — but it decides the
shape of the §60 proof, and it is why the milestone has to be stated carefully:

> **§60 is a differential claim, not an absolute one.** "With integration at zero the image is
> unchanged" can only mean *identical to the same build with the integration code absent*, because
> the chain was never bit-identical to its own input in the first place. The correct experiment is
> `hash(pre-tonemap float buffer)` with the new code compiled in and set to zero, against
> `hash(pre-tonemap float buffer)` at the parent commit — plus a control that perturbs one new
> setting and moves the hash. Comparing against `in.sceneHdr` would be a probe that can never pass,
> which is ADR-182's failure from the other direction.

**Recommended correction to `post_processor.hpp:17–18`**, so the two sources stop disagreeing: *"…
with everything off and a unit exposure the chain is a single composite pass carrying the grade,
which at default grade settings is the identity to within a float ulp."*

### 1.5 The stale paragraph: selective post

`docs/image-formation.md:290–294` says:

> The other two need `PostFrameInputs::emission` and `PostFrameInputs::identifier`, which the
> renderer **does not fill in yet** (ADR-035 is a separate piece of work). Until it does, the post
> chain binds 1x1 placeholders …

Half of this is now out of date. `src/rendering/scene_renderer.cpp:3656` sets
`postIn.emission = emission_.view` unconditionally, with a comment recording that the target *"has
always been written — the debug view reads it — and was never handed to the post chain, so
`post/bloom/emissionWeight` resolved, ran and changed nothing."* `postIn.velocity` is likewise filled
(line 3651).

So of the three selective-post effects in the doc's table at line 284–287:

| effect | mask | doc says | actually |
|---|---|---|---|
| bloom | emission target | not wired | **wired** — `scene_renderer.cpp:3656` |
| halation | warm highlights | works today | works today (derived from colour, needs no target) |
| sharpen | identifier target | not wired | **still not wired** — nothing assigns `postIn.identifier` |

`post/output/sharpenId` is therefore the one parameter in `PostSettings` that is registered,
round-trips, reaches the shader, and cannot have an effect, because
`fs_sharpen` gates the mask on `identifierAvailable` which is `in.identifier ? 1 : 0` and no caller
ever sets it. It is not a parameter for an unbuilt feature — the shader path is built and tested —
but it is a parameter for an **unreachable** one. Flagged under the spec's "reachability is a
separate question from correctness" rather than proposed for removal: the fix is one line in the
renderer, and it is not this work's line to write.

### 1.6 Findings the pass table does not cover

**There is a blocking CPU readback in the exposure loop.** The spec's §66 says *"no CPU readback for
exposure … if one exists, that is a separate finding, not a licence."* One exists.
`PostProcessor::takeMeasurement` (`post_processor.cpp:324`) calls `MapAsync` on `meterReadback_`
and then `context_.waitFor(future)` — a synchronous stall — reads two halves out of the mapped
range, and unmaps. `encodeMetering` ends with `CopyTextureToBuffer` into that same buffer.

This is deliberate and documented: `docs/image-formation.md:111–118` explains that frame *N*
consumes frame *N-1*'s measurement *"read back with a blocking map so there is no timing race"*, and
that this is what makes two offline renders bit-identical — asserted over 40 frames in
`tests/rendering/test_image_formation_gpu.cpp`. The determinism argument is sound and the cost is
one frame of latency plus one stall on a 256-byte buffer.

**It is nevertheless a readback, it is on the live editor's frame path, and it only happens in
automatic exposure mode** (`post_processor.cpp:481` — the `else` branch clears `meterPending_`).
Reported, not touched: removing it would trade a measured determinism guarantee for an unmeasured
frame-time gain, which is exactly the trade this repo's measurement discipline exists to prevent.
The conservative reading of §66 is that it forbids *introducing* one, and the Image/Look work
introduces none.

**`PostSettings` has four fields that are not registered as parameters.** Counted against
`registerPostParameters` (`post_settings.cpp:63–140`):

| field | registered? | reachable by a user? |
|---|---|---|
| `bloomLevels` | no | no — fixed at pyramid creation; `applyPostJson` accepts and ignores the key deliberately (`post_settings.cpp:224`) |
| `motionBlurSamples` | **no** | no |
| `motionBlurMaxRadius` | **no** | no |
| `motionBlurTileSize` | **no** | no |
| `exposureDeltaSeconds`, `exposureReset` | no | correct — per-frame runtime state, not authored |
| `exposure`, `lens` | no (here) | yes — owned by `camera/*` (ADR-037) |

The three motion-blur fields are read by the shader every frame (`post_processor.cpp:522–558`),
have sensible non-trivial defaults, and there is no way for a scene or a project to change any of
them. `post/motionBlur/amount` is the only registered motion-blur control. This is the same shape as
ADR-350's failure — live state the application uses and the file cannot carry — just smaller, and it
predates this work. **Not fixed here**: the spec forbids registering parameters for features not
built, but these *are* built; they are simply out of scope for Image/Look, and adding three
parameters to a subsystem I am not otherwise touching would put un-round-tripped surface in the same
commit as the §60 proof. Recorded for the owner.

### 1.7 Reader / writer / registration status of the existing surface (ADR-350)

| path | reader | writer | registration | verdict |
|---|---|---|---|---|
| project `parameters` block → every `post/*` | `params::loadProject` | `params::saveProject`, `src/params/serialization.cpp:319` — writes every parameter with `flags().serialized` | `registerPostParameters`, `src/app/engine.cpp:76`, re-registered on scene swap at `:187` | **sound** — the writer is generic, so registration alone buys the round trip |
| scene file `post` block | `scene::applyPostJson` → `Composition::postJson_`, `src/scene/composition.cpp:7721` | `composition.cpp:7163` — `j["post"] = postJson_` | applied as parameter **base** values | **sound but echo-only** — see below |

The scene `post` block has a writer, so it does not repeat ADR-350's `DayNightSettings` failure. But
the writer echoes back **the JSON that was read**, not the live state. Moving `post/grade/contrast`
in the editor and saving the *scene* leaves the scene's `post` block at its old value; the new value
lives in the *project's* `parameters` block. That is consistent with the memory note that a
project's parameters override its scene, and `applyPostJson`'s own comment says base values are
chosen precisely so "the project's `parameters` block is applied after the scene loads and therefore
still wins". Confirmed as intended, recorded because it is surprising.

One real gap: **`applyPostJson` reads a subset of what is registered.** Its float table
(`post_settings.cpp:160–176`) omits `temperature`, `tint`, `hueShift`, `anamorphicStretch`,
`anamorphicGhosts`, `focusDistance`, `focusRange`, `dofMaxRadius` and `motionBlurAmount`; its bool
table omits `anamorphicEnabled`, `dofEnabled` and `dofPhysical`; and it handles no `vec3` at all, so
`lift`, `gamma`, `gain`, `halationTint` and `anamorphicTint` cannot be authored in a scene's `post`
block. A scene naming any of them gets `log::warn("post.{}: unknown key, ignored")` — so it is
**loud, not silent**, which is why it has survived. Recorded; anything this work adds to the block
will be wired on both sides.
