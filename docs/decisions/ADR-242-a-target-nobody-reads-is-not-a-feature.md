# ADR-242: A target nobody reads is not a feature

**Status:** Accepted
**Date:** 2026-09-15
**Context:** Level 3 §B — "AOV export to disk"
**Follows:** ADR-035 (the auxiliary targets), ADR-020 (the offline render job), ADR-212 (an offline
render may spend pixels), ADR-182 (a probe that cannot fail proves nothing)

## Context

The Level 3 assessment states the gap and, in the same paragraph, the oddity that makes it worth
closing:

> The renderer already produces normal+roughness, velocity, emission and object/material IDs in HDR
> and can display them. Nothing writes them beside the beauty pass, and depth is not an exportable
> target. Note the standing oddity: **normal+roughness is written every fragment and read by nothing**
> on the normal path — measured at zero cost, but an export is the thing that would give it a
> consumer.

Two corrections to that statement, found by reading rather than assuming. **Depth is exportable**:
`SceneRenderer::linearDepthTexture()` is an R32Float target written on the normal path, so the "not
exportable" note is stale. And "read by nothing" is precisely right about normal+roughness — an
entire per-fragment write with no consumer outside a debug view.

The mandate also sets the bar for adding anything here, and it is the right one:

> None should be added without saying what question it answers — the project's own rule is that a
> target must correspond to meaningful renderer state, and seven new views nobody uses is the failure
> mode §14 warns about from the other direction.

## Decision

`--aov <list>`, and `render.aovs` in a project. Off by default. Five names, each a target the scene
pass already writes, each with a consumer named:

| name | target | question it answers |
|---|---|---|
| `normal` | normal+roughness, RGBA16F | relighting in comp. The pass with no reader. **Oct-decoded on the way out — see §3.** |
| `emission` | emission, RGBA16F | a glow pass, before bloom and before tone mapping |
| `depth` | linear depth, R32Float | depth of field, fog, depth merge against other renders |
| `velocity` | velocity, RG16Float | motion blur in comp |
| `id` | identifier, R32Uint | per-object mattes |

Nothing is here because it was cheap. `albedo`, `metallic`, `shadow`, `volumetric contribution` and
`exposure` are all listed in the mandate as missing debug views and none is added: each would need a
new target or a new pass, and none of them has been asked for by anything.

### 1. A separate ring, because the sequence hash is the deliverable's proof

The beauty readback returns frames in enqueue order and `RenderJob` builds its per-frame hashes and
its sequence hash from that order. Interleaving five more copies per frame into that ring would
corrupt the one thing an offline render exists to be able to prove — that frame *f* depends on
nothing but the project and *f*.

So AOVs get their own `ReadbackRing`, drained beside the beauty ring and never through `handleFrame`.
They do not touch the frame hashes, the sequence hash or the frame count. They do count toward
`framesWritten`, because they are work and a progress bar that reaches the end should mean the work
is done.

**Slots = one per AOV.** Not three. The ring hands slots out in order, so with one per AOV each slot
sees the same format every frame instead of resizing its staging buffer as five different formats
rotate past it. It is also the back-pressure: an enqueue blocks until a slot frees, which bounds how
many float images can exist at once without a second mechanism to do it.

### 2. Three new readback formats, all expanding to RGBA float

The ring knew RGBA8 and RGBA16Float. Velocity is RG16Float, depth is R32Float and identifiers are
R32Uint. Each unpacks to `ImageF` — a single-channel target replicating into RGB so the file is
viewable and every channel carries the same number, velocity filling R and G — because the consumer
is `writeExr`, which takes RGBA floats. One unpacked shape, one writer.

### 3. The normal pass is decoded, because the target is not a normal

**The export as first written shipped a `normal` pass that was not a normal, and it took a test to
find it.** The scene pass writes `vec4(octEncode(n), roughness, flags)` — octahedral encoding, so red
and green are an encoded pair rather than the x and y of anything. Copied out raw the file is
entirely plausible: smooth, continuous, varying over every surface, sensible in a viewer. It is also
useless, because nothing downstream can relight from it.

Nothing about the file's *appearance* would ever have revealed that. The assertion that a normal is a
**unit vector** did, on the first run, at **0 of 3456 geometry pixels** — a failure that missed by
everything rather than by a tolerance, which is the signature of a wrong encoding. That run is also
the control arm this fix would otherwise have needed: the test is proven capable of detecting the
exact defect, because it detected it.

`decodeNormalRoughness` is the CPU inverse of `octDecode` in `shaders/common.wgsl`, matching it line
for line, applied on the way to the file. It is done on the CPU rather than in a shader because there
is no pass to put it in — the export is a copy, not a draw, and standing up a render pipeline to move
four floats per pixel would cost more than it saves.

One detail is a decision rather than an implementation: **a texel no geometry wrote is `(0,0,0,0)`,
which decodes to a unit vector pointing straight at the camera.** Left alone, the sky comes back as a
valid-looking normal everywhere, which is worse than an obviously empty one. Those texels are written
as zero, so the pass also carries a matte of where there is a surface at all — half of what anyone
wants a normal pass for.

The general lesson is the one this repository keeps paying for: **a target's meaning is not its
layout**, and an export that copies bytes without knowing which of the two it is moving will ship
something that looks right.

### 4. Half for three of them, 32-bit float for two

`depth` and `id` are written as 32-bit. A half carries integers exactly only to 2048, so an
identifier above that would come back as a *different object*; and a depth in metres against a far
plane of several hundred would be quantised to a ladder. The other three came from half targets and
are exact as halves.

### 5. AOV export and supersampling are refused together

The auxiliary targets are sized to the **scaled** resolution (`resize()` rescales before
`createAuxTargets`), so under ADR-212's supersampling an AOV is twice the beauty frame — and it
cannot be resolved down with the beauty pass's filter. **Averaging two normals is not a normal,
averaging two identifiers is a third object, and averaging two depths across a silhouette is a
surface that is not there.** Three of the five have no correct downsample.

Refused at `validate()`, naming both flags, rather than written at a size that does not match the
frames or resolved by an operation that is wrong. This collision is live rather than theoretical:
ADR-243 has just switched supersampling on for both Glowmere projects.

### 6. One file per AOV per frame, not one layered EXR

`frame_000123.exr` and `frame_000123.normal.exr` side by side. Separate sequences are what a
compositor reads, and `writeExr` writes a single RGBA part. A multi-layer EXR is the better file and
a larger change to the writer; it is recorded as not done rather than half-built.

The AOV directory is resolved separately from the beauty output, because for a **video** render
`outputPath` is a file and `movie.mov/frame_000000.normal.exr` is not a path. A video with passes
beside it is a real request: the movie for everyone, the passes for the compositor.

## What was measured

`tests/rendering/test_render_job.cpp`. The test's design is the decision as much as the export is:
**every assertion is a property only the right target has**, because a pass that writes a
plausible-looking wrong buffer is the failure this repository keeps writing ADRs about.

| pass | the property asserted |
|---|---|
| `normal` | a **unit vector**, on >90% of the pixels that have geometry to have one, and a roughness in [0,1] in alpha. This is the assertion that caught the oct-encoding. |
| `depth` | the orb's mean depth is positive and **less than the background's** |
| `emission` | **exceeds 1.0**, which a tone-mapped buffer written here by mistake could not |
| `velocity` | **not all zero**, because the fixture scales the orb across the rendered range |
| `id` | **more than one distinct value** — without this every row above is vacuous |

The cross-check is the strongest of them: `id` decides which pixels are the orb and `depth`, read
back from a different target through a different format path, agrees about where it is. Neither
could fake that alone.

The file count is asserted as `10 + 10 * 5` rather than `>= 10`. A count that only reached ten would
mean the AOVs were enqueued and silently dropped, which is exactly what an export whose ring was
never drained would look like from outside.

## Consequences

**A test that asserts a property rather than a shape paid for itself inside an hour.** Every one of
the five passes existed and every file was written, the right size, with plausible contents, before
the normal pass was found to be undecodable. "The file exists and looks like data" would have shipped
it.

**The unread target has a reader.** `normal+roughness` has been written every fragment since ADR-035
for nothing. That is no longer true, and the measured zero cost of writing it is now buying
something.

**Not a deliverable by itself.** An AOV sequence is an input to somebody else's compositor. Nothing
in this engine reads one back, and `readExr` in the tests is the only consumer that exists.

**The mandate's §B statement is corrected rather than merely satisfied.** Depth was exportable
before this ADR; what was missing was anything that exported it.

**Still not done:** a multi-layer EXR; `albedo`, `metallic`, `shadow`, `volumetric` and `exposure`,
none of which has a consumer; and an AOV path that survives supersampling, which needs a resolve
per-target — nearest for identifiers, renormalising for normals, and a silhouette-aware one for
depth — rather than one filter for all five.
