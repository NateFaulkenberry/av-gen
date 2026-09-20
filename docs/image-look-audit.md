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

### 2.1a Past the display: the delivery end, which this map originally stopped short of

The table above ends at the output surface. `docs/offline-backend-audit.md` (the offline-render
branch, merged 2026-09-19) maps what happens *after* that, and two of its findings belong in any
honest account of this engine's colour pipeline. Cross-referenced rather than restated:

- **Until ADR-365, every movie this project had ever written was untagged.** No
  `AVVideoColorPropertiesKey` in the native backend, no `-color_primaries` / `-color_trc` /
  `-colorspace` in the ffmpeg arguments. Every player was guessing, and the guess was usually
  BT.709, which is why it went unnoticed. Both backends now tag BT.709 explicitly. This closes the
  last boundary in the map: the chain is Rec.709 from shading to file, and as of ADR-365 it finally
  *says so* at the one point a downstream tool reads.
- **There is no CPU tone map at all.** All five operators live only in `shaders/tonemap.wgsl`, and
  `render_job.cpp` refuses a CPU copy deliberately, on the grounds that a twin would be free to
  drift from the picture it claims to represent. That is consistent with this audit's §1 finding
  that tone mapping happens exactly once — it is the same fact from the other side — but it has a
  consequence worth naming here: **a path-traced frame cannot become a video frame without a GPU**,
  which costs the tracer the "no GPU required" property that makes it usable on a shared machine.
  The offline audit calls that an owner's decision and recommends porting the operators to C++ with
  a parity test against the WGSL. Nothing in the Image/Look work depends on the outcome, and the
  cinematic integration deliberately adds no sixth place where a curve could be applied.

Neither finding contradicts the map above; both extend it past the boundary it had stopped at.

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

Arms and results are tabulated in ADR-368. In summary: the zero arm is stable across repeats; the
two *shape* parameters moved off their defaults while the amounts are zero do **not** move it; each
of the four amounts on its own **does**; no two move it the same way; and it returns to the original
value after all of them, which is the arm that would catch a dependence on transient-pool
allocation order.

### 5.3 The end-to-end proof

One frame at t = 2.00 s (and the frame after it), 1920x1080, Release, `examples/hero/hero.json`,
all four arms serialised through `tools/gpu-lock.sh` in a single batch. The baseline is a **separate
worktree checked out at the parent commit `8f23d2ec` and built from scratch** — it contains no
`ImageLookIntegration`, no `post/look/*` parameters and no `fs_look*` entry points. The two
binaries' own sha256s differ (`d63f45de…` vs `8ab517b2…`), so this is not one binary compared with
itself. Compared by sha256 of the PNG.

| arm | binary | change | sha256 of frame 0 (first 24) |
|---|---|---|---|
| **A baseline** | parent commit `8f23d2ec` | none — the feature does not exist | `6a525a5756becac7700ea62d` |
| **B zero** | `agent/imagelook` | none — the feature exists and is at zero | **`6a525a5756becac7700ea62d`** |
| **C control (new)** | `agent/imagelook` | `post/look/colour = 0.6` | `3bca64b6993c087547c0bbb0` |
| **D control (old)** | `agent/imagelook` | `post/grade/saturation = 1.4` | `974ed699719682db2559088e` |

**A and B are byte-identical** — `cmp` reports no difference, on frame 0 and on frame 1. Adding the
cinematic integration to the engine changed nothing about a render that does not ask for it.

Both controls fire, and they are there for different reasons:

- **C** perturbs one of the *new* parameters. It is the arm that proves the probe can fail: if the
  new code were inert — never encoded, never reaching the shader — C would have matched B and the
  byte-identity result would have been worthless. It does not match.
- **D** perturbs an *old* parameter, one that exists in both binaries. It proves the render harness
  is sensitive to a project-parameter change at all, independently of anything this work added. A
  harness that produced one hash whatever you did to it would satisfy A = B for the wrong reason.

The parameter counts corroborate it from the other side, and they are the check that would have
caught a silent registration failure: the baseline loads **1025** parameters from `hero.json`; the
new binary loads **1032** from the same file. Exactly seven more, which is exactly the seven
`post/look/*` parameters — registered, reaching the application, and reported by the project loader
with `0 warning(s)`. Counted by name, not by file size: ADR-350's camera-bake loss shrank a file by
10,000 lines while its parameter count went *up*, so a size check would have said healthy.

**Repeated against main's tip after merging it.** The table above was taken with the baseline at
`8f23d2ec`, this branch's parent. After merging main (`598082c1`, which brought ADR-360 through
ADR-366) the whole experiment was rebuilt and re-run with the baseline moved to **main's tip**, so
the comparison is "main" against "main plus this feature" rather than against a commit main has
since moved past:

| arm | binary | frame 0 sha256 (first 24) |
|---|---|---|
| A baseline | main `598082c1` | `6a525a5756becac7700ea62d` |
| B zero | `agent/imagelook` (main + Image/Look) | **`6a525a5756becac7700ea62d`** |
| C control (new) | `post/look/colour = 0.6` | `3bca64b6993c087547c0bbb0` |
| D control (old) | `post/grade/saturation = 1.4` | `974ed699719682db2559088e` |

**Every hash is identical to the first run**, from four freshly built binaries across two different
baseline commits, and `cmp` again reports no difference between A and B on either frame. The
parameter counts moved with main — 1037 in the baseline against 1044 in the new binary — and the
delta is still exactly the seven `post/look/*` parameters. A result that survives a rebuild, a
merge, and a change of baseline is the one worth quoting; this project has been burned by
byte-identical pairs that were really one stale binary compared with itself, and four distinct
binaries with two distinct hashes between them is what rules that out.

---

## 6. Performance (§82/§83), measured in the live editor

ADR-355 is the reason this is not a headless measurement: the editor frame was 350 ms because of a
per-frame vertex rescan, and every diagnosis made headless was blind to the CPU stages that carried
it. `--profile-cpu` prints those stages only in the live editor.

**The load-bearing number is a count, not a duration.** With the integration at zero the chain
encodes *no pass for it*, and that is asserted in `tests/rendering/test_image_look_gpu.cpp` against
`PostStats::passes` rather than against a clock:

| arm | post passes |
|---|---|
| every amount zero | *N* |
| `atmospheric` on | *N* + 1 |
| `atmospheric` + `localContrast` on | *N* + 5 |

One pass for the atmospheric stage; four for the late look stage (quarter-resolution downsample, two
separable blur halves, the combine). A pass count cannot be perturbed by another agent's benchmark,
which is exactly why it is the thing pinned in a test — a timing assertion on a shared machine is a
flaky test with a respectable-looking face.

**Wall clock — retaken on a quiet machine, and the first figure withdrawn.**
`examples/hero/hero.json`, live editor, `--ui-script idle --frames 420 --profile-cpu`, 960x540
canvas (0.52 Mpx), 12 861 triangles, 9 draw calls, through `tools/gpu-lock.sh`, arms interleaved
off/on **three times** in one batch. Taken after waiting for the machine to go genuinely quiet (no
other agent's compile, test or render above 40% CPU, and macOS's XprotectService — which had been
at 552% scanning the freshly built binaries — back to zero).

The `--profile-cpu` table's columns are `min, median, mean, p95, p99, max`. The **median** is the
statistic to read: `gpu.frame`'s `min` is 0.000 in every arm because frames without a GPU timestamp
are counted, so the minimum is degenerate rather than fast.

| repeat | `gpu.frame` median, look off | `gpu.frame` median, look on |
|---|---|---|
| 1 | 4.784 | 5.177 |
| 2 | 4.915 | 5.046 |
| 3 | 4.784 | 4.784 |
| **best of three** | **4.784** | **4.784** |

**The two arms are indistinguishable, and the reason is that the instrument cannot resolve the
effect at this resolution.** Every distinct value observed across all six runs — 4.784, 4.915,
5.046, 5.177 — is a multiple of **0.131 ms** apart from its neighbour, which is this GPU timer's
quantisation step. The best median is identical in both arms and the distributions overlap. So the
honest statement is: **with all four controls on, the cost at 0.52 Mpx is at or below 0.131 ms, and
this measurement cannot put a number on it.** Four extra fullscreen passes at half a megapixel are
simply too cheap for this instrument; resolving them would need a larger canvas, and no such
measurement was taken, so no figure is claimed for one.

CPU is unaffected, which is the expected result and the one this measurement *can* resolve: `FRAME`
medians are 8.361 / 8.458 / 8.468 ms with the look off and 8.323 / 8.375 / 8.329 with it on — the
"on" arm is marginally *lower*, i.e. the difference is noise about a frame-pacing floor. No new row
appears in the `--profile-cpu` table in either arm, which is correct: every part of this feature is
GPU work reached through `PostSettings`, and nothing was added to the update path.

> **Withdrawn.** An earlier version of this section reported **+0.46 ms** for the same comparison
> (4.522 ms off against 4.981 ms on). That measurement was taken while three other agents were
> running, one of them a 100%-CPU windowed benchmark, and it was labelled as such — but labelling a
> contended number does not make it a number. Re-taken quiet and repeated three times, the effect
> disappears into the timer's quantisation. **The +0.46 ms figure was contention, not signal, and
> should not be quoted.** The lesson is the one ADR-170 already states and this is a fresh instance
> of: minima over repeats, and a single pair of runs is not a measurement even when it is honestly
> labelled.

One measurement was thrown away rather than reported: a first attempt showed the "on" arm *faster*
than the "off" arm (1.558 ms against 4.634 ms). The two arms had rendered different scenes — 5 889
triangles against 12 861 — because the project file for the second arm was still being written when
the batch launched. The tell was `upd.controller` reading 0.000 ms in one arm and 0.093 in the
other: the composition had not attached at all. A 3× "speed-up" from adding four post passes should
never have been believable, and the number is recorded here because the next person to see a
suspiciously good result should check the triangle count before the code.

---

## 7. Phase 0's eight captures — proposed, and mine rather than the owner's

The spec's §3 asks for "the **Phase 0 baseline**: the eight captures §51.2 lists, as committed scene
arms that regenerate". §51.2's text is **not in this repository** — only the revision, which names
the deliverable without enumerating it. The first pass of this work therefore declined to invent
eight and attribute them to the owner. This is the second pass, and the set below is **a proposal
of mine**. If the original list turns up and disagrees, `tools/make_imagelook_captures.py` is the
one file to change.

They regenerate rather than being committed as scene files, which is the convention
`tools/make_mcperf_arms.py` established and the reason it gives: the repository has already decided
not to carry twenty near-identical scenes, and the recipe is the part worth keeping.

```
tools/make_imagelook_captures.py          # write them into examples/imagelook/
tools/make_imagelook_captures.py --list   # the set and what each one pins
tools/make_imagelook_captures.py --clean  # remove them
```

Each isolates **one** decision in the chain, so a frame that moves says which stage moved it. They
are self-contained — procedural geometry, flat background, no external assets — so a missing asset
cannot be mistaken for a regression.

| arm | pins |
|---|---|
| `il-grey-ramp` | the tone curve: ten scene-linear greys from 0.002 to 50 |
| `il-hue-wheel` | hue through AgX's inset — the measurement `chroma-retention` exists to answer |
| `il-exposure` | the exposure stage at EV−2, far enough from 1 that the pass is definitely encoded |
| `il-bloom` | the pyramid's energy: one bright emitter at the shipped threshold |
| `il-defocus` | the shared gather with depth of field **and** the tilt-shift band on, which is the case that takes the larger circle |
| `il-wide-tier` | halation and the anamorphic streak, both off by default so nothing else notices them |
| `il-look` | the ADR-368 cinematic integration, all four controls non-zero |
| `il-disabled` | **the tripwire** — every optional stage off. See below. |

**Verified, not assumed.** All eight render, and all eight produce **distinct** hashes — a baseline
whose arms are secretly the same image detects nothing. And the pair that matters is confirmed
live: `il-look` against `il-disabled` is the same scene with only `post/look/*` differing, and it
moves **54.5% of pixels, by up to +83 levels**. So the tripwire has something real to guard.

`il-disabled` is the one to care about. Its job is to stay byte-identical across any change that
claims to be off by default — §60/§87 as a standing check rather than a one-off measurement
(ADR-368). A change there is either a real regression or a deliberate re-baseline, and there is no
third case.

**What this set does not cover**, said plainly so nobody reads it as complete: motion blur (it needs
a moving camera and these arms are deliberately static, because an arm whose framing depends on the
clock cannot be compared frame to frame), the volumetric march, the path tracer, and anything
needing real assets. Those are gaps in the proposal, not in the engine.

---

## 8. §68.1 measured against ADR-347's fog, which is the comparison the spec asked for first

The spec says of atmospheric integration: *"measure what atmospheric integration would add on top of
it before building a second mechanism."* The measurement was owed and is now taken. It is not
flattering to the feature, which is why it is here rather than in a commit message.

Four arms on one scene (`examples/imagelook/_il-atmos-*`, three lit boxes at 10, 18 and 34 m,
1280x720, one binary, through `tools/gpu-lock.sh`). Scene fog is `environment.fogDensity = 0.05`
with a horizon-blue `fogColor`; the post control is `post/look/atmospheric = 0.6`.

| arm | pixels changed vs `none` | worst channel delta |
|---|---|---|
| scene fog alone (ADR-347) | **32.1%** | **+109** |
| post atmospheric alone (§68.1) | 5.6% | +63 |
| both | 33.0% | +109 |
| **post atmospheric added on top of scene fog** | **1.56%** | **+3** |

**The last row is the finding.** On a scene that already has ADR-347's fog, turning
`post/look/atmospheric` up to 0.6 moves 1.6% of pixels by at most three code values. The two
mechanisms are substantially redundant, and scene fog is by far the stronger and the more physical
of the two — it is applied per surface at shading time with the sky's own horizon colour, where the
post control is a depth-driven tint applied to the composed frame.

**Conclusion, stated against my own feature:** `post/look/atmospheric` is **not** the way to get
aerial perspective in this engine, and a scene that wants haze should use `environment.fogDensity`.
The post control keeps a narrow justification — it reaches everything composited *after* shading,
which scene fog structurally cannot, and it needs no volumetric march — but on content that already
fogs, its marginal contribution is close to nothing. It is off by default and costs nothing when
off (ADR-368), so it stays; it should not be recommended, and the header comment on
`ImageLookIntegration` now says so.

**A defect in the first version of this measurement, recorded because the number was wrong in a way
that looked right.** The first run showed scene fog changing *zero* pixels, which would have been a
spectacular finding — ADR-347's fog not working at all. It was my probe. The scene generator wrote
its light as `{"kind": "light"}`, and a light is a top-level `"lights"` entry, not a node kind, so
the loader said `unknown node kind 'light'`, the boxes were unlit, the frame was near black and
there was nothing for fog to tint. **The render log said so and the frame did not** — a black frame
and a nearly-black frame are indistinguishable in a hash, and I would have reported a false defect
in someone else's subsystem if I had trusted the number over the log.

---

## 9. What is actually left (scoping against main at `414fb765`)

Asked to say plainly what remains rather than invent work. **Phases 0, 1 and 2 are done.** The tail
is three items, two of them small, and four candidates that are properly closed rather than
deferred.

### 9.1 Real, and done in this pass

**`bloomLevels` was a working control nobody could reach — the sixth instance of that family, and
the one that hid behind a plausible reason.** `applyPostJson` accepted the key and dropped it,
saying *"Fixed when the bloom pyramid is created, so there is no parameter to move."* That sentence
is false. `PostProcessor::run` clamps and reads `s.bloomLevels` **on every frame**, at
`post_processor.cpp:612` for the bloom pyramid and `:649` for halation's — both inside `run()`,
which begins at `:456`. And `tests/rendering/test_image_formation_gpu.cpp:362` has set it to 3 and
asserted the energy is unchanged since ADR-039, which is a test that only means anything if the
setting is live.

So it was not a control that could not work; it was a control nobody had wired, protected by a
reason that sounded sufficient. It is now `post/bloom/levels`, with the range equal to the clamp
`run()` already applies, a reader, a writer, an ADR-350 round trip and a GPU test that the pyramid
actually changes depth.

**`post_processor.hpp`'s disabled-path sentence is corrected.** It said *"with everything off and a
unit exposure the input is returned unchanged"*, and `docs/image-look-spec.md` quoted exactly that
as proof the spec's own no-change guarantee already existed. It did not, and the misquotation set
the shape of the whole §60 argument. The header now says what is true — a single composite pass,
identity only to within a float ulp — and points at the differential proof.

### 9.2 Closed, with the reason rather than a deferral

**§89, the AgX question: closed.** The brief says do not replace AgX without evidence. What the
engine had was not a bad operator but a broken **inverse** — `agx()` undid its own sRGB encode with
`pow(v, 2.2)` against a piecewise `linearToSrgb` (ADR-372). That is fixed, and fixing it removed
the only evidence that had ever been offered against AgX. **§89 asked for evidence before replacing
the operator; the evidence turned out to be a bug in the code around it.** Nothing further is owed.

**§56, versioning: closed, and adding a version would be the wrong move.** `LookPreset::fromJson`
is fully permissive — every field optional with a default, `format` never checked, unknown keys
ignored — so the look format has no migration surface to version, and a `version` field nothing
reads is the "built but unreachable" family again. Versioning that matters happens where
`post/look/*` actually lives: the **project** format, which has `kProjectFormatVersion` and real
migrations, and which already carries every parameter this work added.

**ADR-378's trap does not apply to this work.** That failure was an absolute error measured against
a relative tolerance, and the old form passed a shader perturbed by +15%. Checked every assertion in
`tests/rendering/test_image_look_gpu.cpp`: there is no `Approx`, no `epsilon`, no `margin` and no
percentage bound anywhere in the file. The §60 arms compare **hash equality**, `byteDiff::identical()`,
**exact float equality** on corner pixels and **exact integer** pass counts. The two threshold
assertions that exist (`changed > 200`, `towardTint > changed / 2`) are absolute counts against
absolute floors, and they can only fail by an effect vanishing — never by one growing, which is the
direction ADR-378's form was blind to. The eight Phase 0 captures assert nothing at all; they are
arms to be diffed.

### 9.3 §66's CPU readback: measured, material, and a decision rather than a side-effect

My first audit found the readback, reported it, and declined to touch it on the grounds that
removing it would *"trade a measured determinism guarantee for an unmeasured frame-time gain"*. The
gain is no longer unmeasured.

`PostProcessor::takeMeasurement` (`post_processor.cpp:343`) calls `MapAsync` on the 256-byte
metering buffer and then `context_.waitFor(future)` — a synchronous stall on the main thread —
reads two halves out of the mapped range and unmaps. It runs **only in automatic exposure mode**;
the manual path clears the pending flag and never maps.

So the measurement is automatic against manual, which isolates metering-plus-readback from
everything else. `examples/hero/hero.json`, live editor, `--ui-script idle --frames 420
--profile-cpu`, **3066x1770 (5.43 Mpx)**, 12 861 triangles, through `tools/gpu-lock.sh`, arms
interleaved three times. **Machine genuinely quiet** — zero other compiles, renders or test binaries
above 50% CPU for the duration. Minima over repeats (ADR-170):

| | `gpu.frame` medians | best | `FRAME` medians | best |
|---|---|---|---|---|
| manual | 22.020 / 22.020 / 22.086 | **22.020** | 23.132 / 23.134 / 23.251 | **23.132** |
| automatic | 22.086 / 22.020 / 22.151 | **22.020** | 24.709 / 24.799 / 24.819 | **24.709** |

- **GPU: +0.000 ms.** The six metering passes — a quarter-resolution prefilter and 4x4 reductions
  to 1x1 — cost nothing measurable.
- **CPU: +1.577 ms per frame**, and the two arms' three-sample ranges do not overlap (23.13–23.25
  against 24.71–24.82), so this is signal, not spread.

Since the GPU side is free, **the 1.58 ms is the blocking map**. On this frame that is **6.4% of a
23 ms frame, on the main thread, in automatic exposure mode**. §66 says no CPU readback for
exposure; there is one, and it costs that.

**Not removed here, and this is a recommendation rather than a deferral.** The stall is what buys
ADR-037's determinism: frame *N* consumes frame *N−1*'s measurement through a blocking map
specifically so there is no timing race, and `test_image_formation_gpu.cpp` asserts two offline
renders are bit-identical over 40 frames. Removing it means moving the exposure state GPU-side, or
double-buffering the readback so a copy has two frames to land. **The double-buffer is the small
one** — it stays a pure function of prior frames, so determinism survives — but it changes the
documented "frame *N* uses frame *N−1*" to *N−2*, which changes the temporal response of every
auto-exposure scene and is therefore a re-baseline of exactly the kind this project insists be taken
deliberately.

That is an ADR-sized change to the exposure loop, not the short tail this pass was scoped to.
**Recommended, costed at 1.58 ms per frame, and handed over rather than taken.**


