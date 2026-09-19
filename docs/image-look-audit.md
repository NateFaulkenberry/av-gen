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

---

## 2. The colour-space map

§51.2 asks for this in explicit names, with no "linear-ish". Every row was read off the source.

| stage | encoding | primaries | where |
|---|---|---|---|
| base colour / emissive textures on disk | **sRGB, hardware-decoded on sample** (`RGBA8UnormSrgb`) | sRGB | `src/assets/gltf_loader.cpp:721`, `:731`; mapped at `src/gpu/texture.cpp:109` |
| normal / metallic-roughness / occlusion textures | **linear** (`RGBA8Unorm`), never decoded | n/a | `gltf_loader.cpp:725`, `:728`, `:734` |
| mip generation for sRGB textures | decoded to linear, filtered, re-encoded | sRGB | `src/gpu/texture.cpp:26–47` |
| HDRI / environment map | **linear float throughout**; an LDR environment is rejected outright and the procedural sky used instead | sRGB (implicit) | `engine.cpp:2386` loads with `srgb=false`; `scene_renderer.cpp:518` requires `isHdr()`; uploaded `RGBA16Float` |
| lighting / shading output | **scene-linear HDR**, uncalibrated units — an emissive of 30 means "30", not 30 cd/m² | sRGB/Rec.709 (implicit) | `kHdrFormat = RGBA16Float`, `scene_renderer.hpp:679` |
| exposure applied | scene-linear, multiplicative | — | `fs_exposure`, chain stage 1 |
| bloom extraction | **scene-linear, on Rec.709 luminance**, in *exposed* units | Rec.709 Y | `luminance()` = `(0.2126, 0.7152, 0.0722)`, `shaders/post.wgsl:56` |
| **cinematic integration (new)** | scene-linear: atmospheric before defocus, colour/contrast/wrap after the grade | Rec.709 Y for the luminance terms | `shaders/post.wgsl`, `fs_look_atmos` / `fs_look` |
| colour grade | **scene-linear**, contrast in log space about 0.18 | — | `fs_composite`, `shaders/post.wgsl:355` |
| AgX in | scene-linear → inset matrix → log2 over `[-12.474, +4.026]` EV | AgX inset (real, not approximated) | `shaders/tonemap.wgsl:51–61` |
| AgX curve | the **6th-order "approx" polynomial** (Wende), not the full sigmoid | — | `tonemap.wgsl:43–48` |
| AgX out | outset matrix → **`pow(x, 2.2)` to return to display-linear** | — | `tonemap.wgsl:63–65` |
| vignette | **display-linear, after the curve, before the OETF** — multiplicative | — | `tonemap.wgsl:141–144` |
| film grain | **display-linear, after the curve, before the OETF** — additive, clamped to 0..1 | — | `tonemap.wgsl:145–148` |
| display encode | **exact IEC 61966-2-1 piecewise sRGB OETF**, in the shader | sRGB | `linearToSrgb`, `tonemap.wgsl:110–114`, called at `:149` |
| output surface | `BGRA8Unorm` / `RGBA8Unorm` — **never an `*Srgb` format** | sRGB | `src/gpu/surface.cpp:45`, `src/app/application.cpp:1981`, `src/app/render_job.cpp:294` |
| HDR (Rec.2100 / PQ / scRGB) output | **does not exist** | — | no EDR/PQ/HDR10 handling anywhere; ADR-002 records the gap |

### 2.1 The OETF is applied exactly once — checked, because it is the classic double

There is **no `*Srgb` render target anywhere in the engine.** The only `RGBA8UnormSrgb` in the tree
is the *input* texture format (`src/gpu/texture.cpp:109`), i.e. hardware decode on sampling.
`tonemapPipelineFor` takes the target format verbatim (`scene_renderer.cpp:1365`) and every caller
passes a non-sRGB `*Unorm`, so the hardware writes the shader's bytes through unchanged. **One
encode, in `linearToSrgb`. No double.**

### 2.2 Two findings in the colour map

**AgX's round trip out of its own encoding is not its inverse.** `tonemap.wgsl:64–65` undoes AgX's
sRGB-encoded output with a **pure `pow(x, 2.2)`**, and `linearToSrgb` at `:110–114` then re-encodes
with the **exact piecewise IEC curve**. Those two are not inverses. The residual is largest in the
toe, where the piecewise function's linear segment (`12.92·c` below 0.0031308) and `x^2.2` diverge
most — a few code values on dark pixels. It affects **only** the AgX branch; ACES, Reinhard, PBR
Neutral and clamp return display-linear directly and round-trip cleanly. AgX is the default
operator, so this is on by default.

Not fixed here, and deliberately so. §89 says do not replace AgX without evidence, and this is not a
reason to replace it — it is a two-character change (`2.2` → a piecewise `srgbToLinear`) that would
alter **every existing render's shadow values**, invalidating every committed reference frame in the
repository. That is an owner's call about a re-baseline, not a side effect of the Image/Look work.
**Recommended, costed, and left.**

**Exposure is applied in two places, and neither reports the product.** The post chain's
`fs_exposure` (`post_processor.cpp:493`) applies the ADR-037 auto/manual exposure scale; the tone-map
pass separately multiplies by `scene.environment.brightness` (`scene_renderer.cpp:3055`, consumed at
`tonemap.wgsl:126`). These are two different quantities in series and it is not a bug — but
`PostStats::exposureScale` is only the first factor, so the number the UI and the profiler report is
not the total scale reaching the curve. Worth knowing before tuning: a scene that looks two stops
hot may have neither of its two exposure controls at fault individually.

---

## 3. The resource table

Over the transient pool (`src/gpu/transient_pool.cpp`) unless marked persistent.

| resource | producer | consumers | format | resolution | lifetime |
|---|---|---|---|---|---|
| HDR scene colour | scene pass | post chain, tonemap | `RGBA16Float` | output × `renderScale` | persistent, re-created on resize |
| depth | scene pass | defocus, motion blur, composite depth grade, **`fs_look_atmos`** | `Depth24Plus` | as above | persistent |
| normal + roughness | scene pass | AOV export, debug view | `RGBA16Float` | as above | persistent |
| velocity | scene pass | motion blur tiles | `RG16Float` | as above | persistent |
| emission | scene pass | bloom prefilter (selective bloom) | `RGBA16Float` | as above | persistent |
| identifier | scene pass | *nothing* — never bound into post (§1.5) | `R32Uint` | as above | persistent |
| linear depth | dedicated pass | AOV export | `R32Float` | as above | persistent |
| shadow atlas | shadow pass | lit pass | `Depth24Plus`, 2D array | 1024² default | persistent |
| shadow mask (ADR-087) | mask pass | lit pass | `RGBA16Float` | as above | persistent |
| meter chain | `encodeMetering` | next meter level, then a 256-byte readback | `RGBA16Float` | ¼ then ÷4 to 1×1 | transient, one frame |
| exposure result | `fs_exposure` | next stage | `RGBA16Float` | full | transient |
| **look/atmos (new)** | `fs_look_atmos` | next stage | `RGBA16Float` | full | transient, **only when `atmospheric > 0`** |
| defocus result | `fs_dof` | next stage | `RGBA16Float` | full | transient |
| velocity tiles / neighbours | tile passes | motion blur | `RG16Float` | ⌈size/tile⌉ | transient, released in-pass |
| lens result | `fs_lens` | next stage | `RGBA16Float` | full | transient |
| bloom pyramid down 0..N | prefilter + downsample | upsample, anamorphic source | `RGBA16Float` | ½, ¼, … | transient |
| bloom pyramid up | `fs_upsample` | composite | `RGBA16Float` | ½ | transient |
| halation pyramid | halation prefilter + downsample/upsample | wide tier | `RGBA16Float` | ¼, … | transient |
| wide tier | `fs_wide` | composite | `RGBA16Float` | ¼ | transient |
| composite result | `fs_composite` | next stage | `RGBA16Float` | full | transient, **always allocated** |
| **look/small (new)** | `fs_downsample` | look blur H | `RGBA16Float` | ¼ | transient, released in-stage |
| **look/blur H, V (new)** | `fs_look_blur` ×2 | `fs_look` | `RGBA16Float` | ¼ | transient, released in-stage |
| **look result (new)** | `fs_look` | next stage | `RGBA16Float` | full | transient, **only when colour/contrast/wrap > 0** |
| fxaa result | `fs_fxaa` | sharpen / tonemap | `RGBA16Float` | full | transient |
| sharpen result | `fs_sharpen` | tonemap | `RGBA16Float` | full | transient |
| final LDR | `fs_main` (tonemap) | swapchain / readback / encoder | `BGRA8Unorm` or `RGBA8Unorm` | output | per-frame target |

**Pool semantics**, because they decide what "transient" costs. The match key is the exact 4-tuple
`(width, height, format, usage)` plus `!inUse` (`transient_pool.cpp:11–18`); any mismatch, *including
a usage bit*, forces a fresh `CreateTexture`. Default usage is
`RenderAttachment | TextureBinding | CopySrc`. `release()` only flips `inUse`. `endFrame(keepFrames =
60)` is the sole reclaim point: it erases entries unused for 60 frames and then force-clears `inUse`
on everything remaining, so a pass that forgets to `release()` leaks for the rest of that frame only.

Note the consequence for the new look stage: it acquires four textures, and it `release()`s the three
intermediates as soon as they are consumed, so the steady-state pool growth is one full-resolution
target plus one quarter-resolution one.

---

## 4. Where the two renderers agree and differ

§75 asks that both renderers produce compatible signals. **The names agree. Three of the shared
quantities do not**, and the existing drift test could not have caught any of them because it
compares spellings. It has been extended rather than forked
(`tests/unit/test_pathtrace_aov_exr.cpp`).

| signal | rasteriser | path tracer | compatible? |
|---|---|---|---|
| vocabulary | `{normal, emission, depth, velocity, id, shadow}`, `render_settings.cpp:61` | the same minus `velocity`/`shadow`, plus `albedo` | **yes**, and tested |
| **beauty EXR** | `hdrOutputTexture()` — the **post-chain output**: past exposure, bloom, halation, grading, and now the cinematic integration; short only of the tone curve (`render_job.cpp:688`, `scene_renderer.cpp:3674`) | **raw radiance, no post at all** (`path_tracer.hpp:11`) | **no.** Both are truthfully "scene-linear before tone mapping". They are not the same image, and nothing reconciles them |
| `depth` units | view-space metres along camera forward (`linear_depth.wgsl:34`) | the same (`path_tracer.hpp:106`) | **yes** |
| `depth` background | **`1.0e7`** (`linear_depth.wgsl:28`) | **`-1`** (`path_tracer.hpp:106`) | **no.** `depth > 0` selects the whole frame in one and the geometry only in the other |
| motion | `velocity`, `RG16Float`, **UV units** (`scene_targets.hpp:9`) | `motion.X/Y`, **pixels** (`path_tracer.hpp:111`) | **no** — different name *and* different unit, differing by the resolution |
| `emission` | scene-pass target, pre-exposure | raw, pre-exposure | **yes** as a quantity; but it sits beside a beauty image that *is* post-exposure on one side and not the other |
| `id` | `R32Uint`, low 16 object / high 16 material | float-encoded `packPickId`, `-1` for a miss | **no** |
| `albedo` | none | present; the denoiser needs it | one-sided, recorded |
| `shadow` | present; **recomputed, not captured**, at high/offline tiers | none | one-sided, recorded |
| file layout | **one RGBA EXR per AOV**, so normals and velocity land in R/G/B | one multi-layer EXR with `normal.X/Y/Z`, `motion.X/Y` | **no** — the tracer applies the "a vector is not a colour" rule and the rasteriser does not. Debt already acknowledged at `render_settings.hpp:117` |
| tone mapping | exactly once, `shaders/tonemap.wgsl:128` | **never** — no tone map exists in `src/pathtrace/` | consistent with "tone mapping happens exactly once" |

The sharpest of these is `velocity`/`motion`, because `path_tracer.hpp:100` records `velocity` as
having *"no counterpart here yet"* while `writeFramebufferAovExr` writes `motion.X/Y` — the gap is
documented in the code as an **absence** when it is really a **rename plus a unit change**. That is
the kind of thing that is invisible until a compositor's motion vectors are wrong by a factor of
1920.

**One loose end found while checking the tracer, reported and not touched:**
`Framebuffer::worstAlbedo` (`path_tracer.hpp:110`) is allocated and filled but written to no EXR
channel and read by nothing, and `AlbedoProbeReport::format()` — the per-material table ADR-352
describes — is called only from tests, so a command-line `--pt-probe` run emits only a one-line
`log::warn` summary. The ADR's per-material breakdown is unreachable from the CLI.

---

## 5. §60/§87 — with integration at zero, the image is unchanged

The milestone. It is proved twice, because the in-process proof and the end-to-end proof fail in
different ways and neither subsumes the other.

**First, what the claim can mean.** §1.4 established that the chain was never bit-identical to its
own input: the composite always runs and is not the identity at default grade settings. So
"unchanged" is necessarily a **differential** claim — identical to *the same build with the feature
absent* — and anything comparing against `in.sceneHdr` would be a probe that cannot pass.

### 5.1 Was some of §68.1 already in the frame?

The spec asks this before any atmospheric code is written, and the answer is **yes, and the new
control is deliberately not the same mechanism.** ADR-347 gives the scene real fog that takes its
colour from the sky's own horizon, at a density it also corrected downward by 15×. That is aerial
perspective, done properly, at shading time.

What it structurally cannot reach is **everything composited after shading** — volumetrics, user
post layers, the bloom and wide tiers. `post/look/atmospheric` operates on the composed frame from
depth, which is the gap, and it is documented in `ImageLookIntegration` as not being a second fog so
that the next person does not turn both on and wonder why the distance went flat. **An author
wanting physical haze should still use ADR-347's fog; this is the image-side control.**

### 5.2 The in-process proof

`tests/rendering/test_image_look_gpu.cpp`, hashing the pre-tonemap float buffer
(`renderToImageFloat` → `hdrOutput_`) with `gpu::hashImage`, over float bit patterns.

Arms and results are tabulated in ADR-366. In summary: the zero arm is stable across repeats; the
two *shape* parameters moved off their defaults while the amounts are zero do **not** move it; each
of the four amounts on its own **does**; no two move it the same way; and it returns to the original
value after all of them, which is the arm that would catch a dependence on transient-pool
allocation order.

### 5.3 The end-to-end proof

<!-- MEASUREMENT -->
