# Renderer 2.0: what is left

Phases 0–4 are done. This is the standing list of what remains, ranked by the evidence that exists
rather than by the original brief's assumed ordering — which Phase 3 overturned once and Phase 4
corrected again. Each item records *why it is on the list*, so a future pass does not have to
re-derive it.

Measurements are at the editor's real canvas (2880×1166 = 3.36 Mpx) unless stated. That is not the
"1440×900" headless benchmarks quote: the editor renders into the dock centre at the display's
backing scale, so the benchmark figure is 1.30 Mpx and the thing you look at is 3.4–4.4 Mpx. Use
`--canvas-scale` and measure at both.

## Where the frame goes now

The scene pass is **fragment-bound**, not geometry-bound. The proof stands: the depth prepass
submits identical geometry through the same vertex stage for 0.20 ms while the lit pass costs
tens. `cpu(scene)` is 0.42 ms. **Nothing justified by "the CPU or the vertex stage is the
bottleneck" belongs on this list** — that rules out GPU-driven submission and draw-call merging
until a measurement says otherwise.

After Phase 4's shadow mask, the remaining large terms inside `directLighting`:

| term | share of the scene pass | notes |
|---|---:|---|
| contact march | ~19% | half-resolution is **proven not to work** — 30.2% of pixels move |
| clustered bioluminescent lights | ~18% | untouched by Phases 2–4 |

## Ranked

**1. Calibrate the LOD ratios against a moving camera.** The cheapest measured millisecond
available, and *newly possible*: until Phase 4 fixed `meshopt_simplifySloppy`'s overshoot, the
simplifier returned a fifth of what it was asked for, so turning the ratio knob did nothing
legible. Phase 4 deliberately spent 1.83 ms restoring the authored ladder; that is directly
recoverable if 35% at LOD1 turns out to be more than the transition needs. **It can only be
answered by watching the threshold**, which no phase has done — every run so far has been
headless. ADR-082's per-instance spread should soften the swap; whether it is enough is unverified.

**2. The clustered lights.** 18% of the pass and the only large untouched term. Phase 4's masking
trick does not transfer — a local light's index is per-froxel. Needs its own measurement first:
per-froxel light counts, and what the 32-light cap actually costs.

**3. The contact march.** 19%, now the largest single term. Half resolution is ruled out by
measurement. What is untested: varying the step count with screen-space size, or bounding the
march to the near field where it does its job. Note `RigLight::contactShadow` (Phase 4) already
lets a rig switch it off per light — worth 3.28 ms on Glowmere's two shadowless lights, at a cost
to the image, which is an art decision and not a default.

## Known and unfixed

- **`uploadMeshes` keys on `meshVersion`, which every fresh `Scene` starts at the same value.** Two
  scenes with one mesh each through one renderer both draw whichever uploaded first. A geometry A/B
  written the obvious way silently measures the same mesh twice. Phase 3 dodged it by bisecting
  through scene JSON; it is still there.
- **Rendering is not bit-reproducible under GPU contention** (empty-LOD suppression advances on
  whichever async cull readback landed). Pre-existing.
- **The engine is not byte-identical across different frame rates** — a scene with no rig in it
  differs between a 24 fps and a 48 fps render of the same second, because passes with temporal
  history have seen a different number of frames. Frame 0 is identical at both. Skinning was
  measured and contributes nothing to it.
- **LOD0 still uses `decimateMesh` for `meshBudget`** and is never vertex-cache optimised. Left
  alone twice, deliberately: changing it changes the near field.
- **The shadow mask's normal is geometric; the lit pass shades with the material's.** Residual 5.6%
  of pixels before half-resolution sampling. Fixing it wants a full-res RG16F attachment and a
  fragment shader in three renderers — not worth it against an 8.4% total.
- **A wide scene of small objects gets almost no cast shadows.** Found while building the ADR-089
  proof-of-concept: a 260-metre city of about 220 nodes reports `draws=95 shadowDraws=2`, and the
  streets come out flat. Not the light rig (removing it changes nothing), not the sun's elevation
  (11 or 38 degrees, same), and not instancing (a `single`-distribution probe box in the middle of
  the frame is culled too). `sceneRadius` is `max(bounds diagonal / 2, camera standoff)` and
  `shadowFar = sceneRadius * 3`, so a big ground plane pushes the cascade split points far past the
  geometry; three or four cascades then cover 900 metres and the per-cascade `aabbInsideFrustum`
  test rejects nearly every caster. Small scenes are fine (the alien fixture gets 7-9 shadow draws
  from 5 entities), and objects *near the camera* still cast -- the street trees and lamps do, the
  buildings do not. Worth measuring where the split points actually land before changing anything;
  the fix is probably to fit `shadowFar` to what the camera can *see* rather than to the whole
  scene's bounds.
- **GPU timing tests fail under `ctest -j4` when other work is on the GPU.** Happened three times
  in one session; each passed 3/3 standalone. `RESOURCE_LOCK gpu` serialises GPU tests against each
  other but cannot serialise against another process. The harness could detect a busy GPU and skip
  rather than fail.

  Sharpened 2026-09-11: `the shadow passes respond to shadow workload` failed **standalone, twice in
  four runs**, at a load average of 2.65 with no build and no other test running. The caster A/B
  measured ratios of 1.02 and 1.13 against a threshold of 1.8, and passed the other two runs. The
  competing work was **CrashPlan** (two processes, ~40% CPU between them) and WindowServer at 24% --
  so "another process" does not have to be another agent, and on a normal desktop it usually is not.
  Ruled out as a cause: water's forced depth prepass, which only adds `|| !scene.waters.empty()` and
  this scene has none.

  The test's own comment says the workload ratio is 6.2x and the threshold is 1.8, so a measurement
  of 1.02 is not a marginal miss -- the shadow pass timing is not tracking the work at all on those
  runs. Worth finding out whether the timestamp pair is being attributed to the wrong pass under
  preemption before loosening anything.

## Rejected on measurement, with numbers — do not retry without new evidence

- **Dynamic resolution.** It saturates *above* the target: a quarter of the pixels still leaves the
  frame at 19.20 ms against 16.67, and between 0.60 and 0.50 scale it moves only 1.71 ms because
  the invocation count is geometry-floored by then. A dynamic scaler would sit at its floor
  permanently and still miss. The static `--canvas-scale` is the right shape; 0.9 buys 12%.
- **Masking the contact march.** 30.2% of pixels move against 8.4% for the map term alone.
- **More taps in the shadow mask** (24 vs 12): 8.50% vs 8.42% differing, for 1.0 ms.
- **A software early-Z arm in the LOD chain:** removed 1.24 ms of 21.36 — the depth prepass already
  does that job.
- **Tile-splitting from attachment count** as the explanation for the per-pixel slope steepening:
  tested, not supported. Terrain alone runs 8.30 → 3.39 → 2.88 ns/pixel over 0.32 → 5.18 Mpx, and a
  fixed-geometry scene is flat across the same range with the same five attachments.

## Multi-material scatter assets cull and count their instances once per material

An asset with two materials becomes two `ProceduralGeometry` objects
(`src/scene/composition.cpp`, the `subs` loop) -- a full copy of the first, **instance cloud
included**, differing only in mesh, material and triangle budget. Each copy then gets its own cull
pass over the same instances.

Two consequences, one cosmetic and one real:

- **The instance counters double-count.** Measured exactly on `glowmere-dense`: the composer places
  153,776 instances and the renderer reports 161,084. The excess is 7,308, which is
  `CommonTree_1 (725) + TwistedTree_2 (348) + Flower_3_Group (6235)` -- precisely the three layers
  logged as "carries 2 materials; one instanced draw each". On `glowmere-low` the same sum is 301
  against an excess of 300. This is the counter the benchmark world flagged as not meaning what its
  name says, and this is why: `visible + culled` is a sum over *draw* objects, not over the world's
  population. A fix can gate the population totals on `source.assetPart == 0` (part 0 is the
  original; sub-parts are numbered from 1) while leaving `draws` per part, where it belongs.
- **The cull work is duplicated.** The instances are identical across parts, so the visible set is
  identical too, and the GPU computes it once per material. On `glowmere-dense` that is 7,308
  redundant instance tests per frame, ~4.8%. Sharing one cull result across an asset's parts is the
  optimisation; it needs the parts to share a visible-list buffer, which they currently do not.

Neither is urgent -- the frame cost is small and the counter misleads rather than misrenders -- but
the measurement is exact and reproducible, so the next person should not have to rediscover it.
