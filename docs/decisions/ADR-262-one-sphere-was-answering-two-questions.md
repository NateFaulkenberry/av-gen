# ADR-262: One sphere was answering two questions, and a rung change drew nothing

Status: accepted
Date: 2026-09-17

## Context

`shaders/cull.wgsl` forms one bounding sphere per instance and makes two kinds of decision with it:
three rejections (frustum, `maxDistance`, `minScreenRadius`) and one selection (which of four LOD
rungs to draw). The LOD / Geometry Lab was opened on the second of those, with one question handed
to it: the thresholds `28 / 11 / 4` px in `src/scene/composition.cpp` had just started acting on a
radius that was corrected underneath them, and nobody had re-derived them.

Three things came out of it, and only one of them was the question that was asked.

### The radius

The sphere is centred on the **instance record position** — for a scatter, the point on the ground
the thing was planted at. A sphere about that centre has to reach the furthest corner of the source
*from the source's own origin*, or a tree can be discarded with its canopy on screen. That is
`rendering::sourceCullRadius`, and correcting it to that was right.

It is the wrong number for the ladder. The ladder is not asking whether anything is on screen but
how large the thing looks, and the gap between the two rules is **how far the artist put the
geometry from its origin** — which carries no information about size. Over Glowmere's thirteen
scatter layers that gap ranges from 1.08× (`ferns`) to 1.75× (`pines`), so the same authored
threshold of 28 px fired when an object's drawn radius was 11.1–18.7 px, where under the rule the
thresholds were authored against it was 14.8–22.6 px. Objects held their full mesh about a quarter
longer, per layer, for reasons unrelated to how big they were.

Both numbers are measured from the rendered image, not from a second copy of the rule: the lab
bisects the camera distance at which the GPU's own compacted lists stop putting the instance on rung
0, and then measures the silhouette the frame drew.

### The transition

`ProceduralRenderer` decided which LOD levels to record an indirect draw for from how many
consecutive frames the level had been empty **in the last completed cull readback** — and that
readback lags the drawn frame by one to three. A prediction of the present from the past, wrong at
exactly one moment: the frame an instance arrives on a level nothing has been on recently. The
object is then drawn by nothing at all until the readback catches up.

Measured on CommonTree_1 at 460 m: the arrival frame drew **0 lit pixels**, the identical frame once
settled drew 89. The optimisation that introduced it measured a *still* frame and found the image
bit-identical, which it is; the failure only exists under motion, which is the only condition a LOD
ladder operates in.

### The representation

ADR-029 made levels 2 and 3 camera-facing impostor quads, and the renderer set the billboard vertex
path for `level >= 2`. ADR-085 then gave a `Mesh` source a simplified mesh at every level. Nothing
told the renderer, so from that point rungs 2 and 3 of every scatter layer, every city piece and
every imported asset were drawn through the impostor path — vertices taken as offsets in the
camera's basis, skipping the source transform and the deformer stack. A 14 m production tree drew at
rung 2 as a 7.3 m flat shape centred on the foot of its trunk: at 90 m it occupied screen rows
362–419 where the full mesh occupies 304–424.

Two changes that were each right on their own, and stopped agreeing.

## Decision

**The cull pass carries two radii.** `limits.z` is the conservative sphere about the record and is
what the rejections use; `thresholds.w` is the tight sphere about the source's box and is what the
ladder measures with. `rendering::cullLodLevel` takes both, and 0 means "the same as the first",
which is what the pass did before they were separated. The thresholds `28 / 11 / 4` are unchanged —
what changed is the quantity they are compared against, back to the one they were authored and
validated against in ADR-085.

**Which levels are recorded as draws is a proof.** `rendering::objectLevelRange` gives the inclusive
range of rungs any record of an object could be on, from the same bounds `objectFullyCulled` already
uses, widened by everything that can move one instance's threshold: ADR-082's per-instance spread
and dead zone, and ADR-038's depth-band `detail`. A level outside the range is provably empty. The
frame-count heuristic and `ObjectState::emptyFrames` are gone.

**`scene::lodLevelIsImpostor(spec, level)` is the one statement of which levels are impostors.**
`makeLodMesh` branches on it and `ProceduralRenderer` asks it when it picks a vertex path and a
cull mode. Level index is never the answer again.

**`DebugViewOptions::lod` is wired**, to `ProceduralRenderer::readLodLevels` — a blocking readback of
`lodIndex`, the buffer `cs_cull_classify` writes and the compaction reads. It returns `fresh`, and
an object whose cull dispatches were not encoded this frame is drawn grey rather than rung 0,
because that buffer still holds the last frame that ran them.

## Consequences

Glowmere multicam, headless, same frames and the same seed, frame 30 of a 360-frame run:

| | before | after |
|---|---|---|
| visible instances | 494 | **494** |
| submitted triangles | 449,524 | **350,545** (−22.0%) |
| rungs 0/1/2/3 | 232 / 136 / 126 / 0 | 43 / 282 / 162 / 7 |
| indirect draws recorded | 130 | 200 |
| indirect draws skipped | 130 | 60 |
| GPU frame, min of the 13 logged frames | 15.60 ms | 12.71 ms |

The instance count is the control: the Visibility Lab's cull fix is untouched, and the triangles
come entirely from the ladder being back where its quality was measured. The 70 extra indirect draws
are the price of drawing the frame a rung changes on. (Timings on a machine shared with other
agents, load average 2.55; the structural numbers are the claim and the milliseconds are context.)

What the first threshold buys, measured because the cost side had always been reported and the
quality side never was: on CommonTree_1, rung 1 keeps 93–96% of the object's lit pixels and its
outline is within 1–3 px of rung 0's at every drawn radius from 8 px to 40 px. The ladder has
headroom above where it fires under either convention, which is why no threshold was moved.

ADR-085's own quality measure does not transfer to this question. "8.0% of pixels differ by more
than 2/255" is a *frame* number dominated by background; at the object level the same measure reads
92–98% at every size, because a third of the triangles is a different arrangement of leaves. It
cannot separate a visible swap from an invisible one on foliage. Mass and outline can.

**Not fixed, and recorded rather than rushed**: at 2.9% of its triangles CommonTree_1 comes back from
`assets::buildLodChain` with the bottom 31% of its bounding box gone — the trunk — while reporting a
relative error of 0.065 against a bounding-box move 5.3× larger. That contradicts
`src/assets/mesh_lod.hpp`'s claim that the error "never understates", and it means a selector
choosing a level by projected error would choose that one far too early. The fix belongs in
`buildLodChain`, as the same shape of guard ADR-085 added for a level larger than its predecessor,
and it changes every asset in the repository. Rung 3 carries 7 instances on the Glowmere baseline.

**What it cost to learn**, kept because the next person will be tempted the same way:

* A ladder verified by re-deriving the expected rung from the inputs the shader uses agrees by
  construction. Two files in the repository transcribe `cullLodLevel` and say so; every assertion
  the LOD Lab added is against the compacted lists or against lit pixels instead.
* The first pair of assets chosen for the "same size, different origin" probe — a tree and a rock —
  agreed to within 4%, because the tree's larger origin offset and the rock's wider silhouette
  cancelled. A pair is not a measurement. The thirteen-layer sweep is.
* An optimisation measured on a still frame is not measured. Both the empty-level skip and the
  radius conflation were invisible in every counter a still frame prints.
