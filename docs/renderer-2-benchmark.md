# Renderer 2.0, Phase 1: does the profiler tell the truth?

**Status: complete for the passes listed.** This is the measurement phase. Nothing here optimises
anything, and nothing here changes what is rendered — the Glowmere sequence hash is
`dc0860f8b2db9cf7` before and after (§8).

Phase 0's audit (`docs/renderer-2-architecture.md`) named three defects: the headline triangle
count answers the wrong question, submitted geometry is unmeasured, and pass timings under-report
by ~40% on a removal basis. All three are addressed below, one of them differently from how it was
framed. Two further defects turned up while measuring: the timestamp counter is quantised at
65,536 ns, and the frame reported fewer draws than indirect draws.

Every number below names the command that produced it.

---

## 0. How to reproduce, and the one thing that will ruin it

Apple M2 Max, release build (`cmake --build build/release -j8`), headless, `--tier realtime`,
180 frames at a fixed 30 Hz clock, 168 warmed frames, Glowmere
(`examples/world/glowmere-stylized.json`) unless stated.

**Nothing else may be using the GPU.** This is not a caution, it is the largest error term in the
whole document. The same command, same binary, same scene:

| condition | GPU frame median | p90 |
|---|---:|---:|
| quiet | 23.86 ms | 29.2 |
| one other `avgen` process | 103.28 ms | 121.7 |

and an "empty" 256×256 frame measures 3.7 ms alone and 10.4 ms with a neighbour. Before taking any
number, check:

```sh
pgrep -fl avgen        # must be empty
sysctl -n vm.loadavg   # first figure well under 3
```

Measurements taken against a busy machine are marked as such and are not used to draw conclusions.

---

## 1. Verdict: which passes measure correctly

Every pass tested responds to its own workload, with the direction and rough magnitude expected.
The battery is `tests/rendering/test_frame_profiler_gpu.cpp`; each row is a paired A/B run inside
one process, alternating arms in short rounds, reported as the **median of the per-round ratios**.

```sh
./build/release/tests/avgen_render_tests "[profiler]" -s
```

| pass | workload changed | factor | measured response | verdict |
|---|---|---:|---:|---|
| `scene` | triangles (same draws, same coverage) | ×1024 | **×13.0** | responds |
| `scene` | blended fullscreen overdraw (same triangles) | ×32 | **×31.1** | responds |
| `shadow` | shadow casters submitted (393 k → 2.44 M triangles) | ×6.2 | **×2.67** | responds |
| `shadow` | shadow map texels (same casters) | ×64 | **×1.6–2.7** | responds, weakly |
| `volume` | march steps | ×16 | **×6.09** | responds |
| `volume` | pixels | ×16 | **×13.6** | responds |
| `ao` | pixels | ×16 | **×10.0** | responds |
| whole frame | empty scene vs 48 blended layers | — | 0.66 ms vs 17.7 ms | responds |

Those ratios are one run of the battery. **The direction is stable and the magnitude is not**:
the volume-versus-pixels row read ×13.6 on a quiet machine and ×2.0 on a busy one, and the AO row
read ×10.0 and ×0.5. The test bars are set to catch a pass that does not respond at all, not to pin
a slope, and the magnitudes above should be treated as one observation each.

Two of those rows deserve a sentence rather than a tick.

**`scene` is geometry-bound and fragment-bound both**, which is consistent with phase 0 rather than
in tension with it: ×1024 the triangles moves it ×13, and ×32 the blended coverage moves it ×31.
Glowmere's scene pass barely moves with resolution because Glowmere's scene pass is not covering
many pixels many times over — not because the pass cannot see fragments.

**`shadow` responds weakly to shadow-map resolution.** Sixty-four times the texels buys between
1.6× and 2.7× the time over three observations, against 6.2× the casters buying 2.67×. The depth
passes are paying mostly for vertices and draws, which is the same story as `scene` — and it is
worth knowing before anyone proposes shrinking the cascades to buy frame time.

Both shadow rows needed a **denser workload than they started with** before they would say anything
stable. At a quarter of the geometry the light arm measured three ticks of the counter, and the A/B
returned 4/3, 5/3 or 8/3 depending on the minute. That is not the shadow pass failing to respond;
it is a three-tick measurement being asked a question it cannot answer. §2.

### What is *not* certified

- **The post chain per-pass.** Glowmere's bloom pyramid is twelve `post/bloom` passes, and ten of
  them report exactly `0.000` (`--log debug`, "in order:"). They are not free; they are under the
  counter's tick. Only the label's **sum** means anything, and at 0.13 ms that sum is two ticks.
- **`background`.** It reports 0.00 on Glowmere and the frame reports one unwritten timestamp. A
  render pass that issues no draws gets no end-of-pass timestamp from Metal, so the background
  clear's cost — whatever it is — is charged to the `depth` pass after it. Expected, documented in
  `gpu/timeline_math.hpp`, and now named rather than merely counted:
  `FrameTimeline::unwrittenLabels()`.
- **`clusters`, `particles`, `post/composite`, `post/fxaa`, `tonemap`** at 0.07 ms each. One tick.
  See §2.

---

## 2. The counter ticks every 65,536 ns, and that decides what can be A/B'd

```sh
./build/release/tests/avgen_render_tests "the timestamp counter's resolution is small enough to measure a pass" -s
```

> smallest non-zero interval 65536 ns, gcd of 279 intervals 65536 ns

Every interval the driver reports on this machine is an exact multiple of **65,536 ns = 0.065536
ms**. Not approximately — the greatest common divisor of 279 consecutive intervals is exactly that.
So Glowmere's per-pass table at 1440×900 reads, in ticks:

| label | ms | ticks |
|---|---:|---:|
| scene | 20.71 | 316 |
| shadow | 0.92 | 14 |
| volume | 0.85 | 13 |
| cull / depth | 0.33 | 5 |
| ao | 0.26 | 4 |
| post/bloom | 0.13 | 2 |
| clusters / particles / post/composite / post/fxaa / tonemap | 0.07 | **1** |

A one-tick pass carries ±100% quantisation error. A five-tick pass carries ±20%. **Do not A/B
anything under about 0.2 ms** on a single frame's reading; only the median over many frames
recovers anything below that, because the quantisation dithers against the pass's own variation.

This is a property of the hardware counter, not of the instrument, and it explains why phase 0's
small labels all clustered at 0.07–0.13 ms: those are one and two ticks.

---

## 3. The removal discrepancy: mostly run-to-run variance, and the rest is attributable

Phase 0 measured `--disable volume` saving 1.44 ms while the volume pass reported 0.85, and
hypothesised that a pass's timestamps do not capture its resource transitions or its targets'
bandwidth. Tested, not assumed.

The test is a paired A/B on a quiet machine, three rounds, alternating arms:

```sh
for r in 1 2 3; do for arm in on off; do
  if [ "$arm" = off ]; then D=(--disable volume); else D=(); fi
  ./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
      --frames 180 --fps 30 --size 1440x900 --tier realtime "${D[@]}" | grep "gpu frame median"
done; done
```

| round | frame, volume on | frame, volume off | saved | `scene` on | `scene` off | `scene` delta |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 23.86 | 22.61 | 1.25 | 20.71 | 20.32 | +0.39 |
| 2 | 23.59 | 22.81 | 0.78 | 20.51 | 20.58 | −0.07 |
| 3 | 23.79 | 22.74 | 1.05 | 20.71 | 20.51 | +0.20 |

**Median saving 1.05 ms against 0.85 ms reported: a 0.20 ms discrepancy, not 0.6.** Phase 0's
larger figure came from comparing two single runs; three paired rounds put the saving anywhere
between 0.78 and 1.25 ms, and 1.44 is not far outside that.

Where the 0.20 ms goes is answerable rather than mysterious, because the timeline's intervals
partition the frame exactly — a cost the volume pass is not charged has been charged to some other
pass, and there is nowhere else for it to be. Across all six runs **every label except `scene` is
identical to the tick**: shadow 0.92, cull 0.33, depth 0.33, ao 0.26, post/bloom 0.13, clusters
0.07, particles 0.07, post/composite 0.07, post/fxaa 0.07, tonemap 0.07. The entire residual sits
on `scene`, the pass that immediately precedes the volume march and shares its colour target.

And it is not distinguishable from noise. The `scene` pass's spread *within* an arm is 0.20 ms
(volume on: 20.71, 20.51, 20.71) and 0.26 ms (volume off: 20.32, 20.58, 20.51) — the same size as
the 0.20 ms difference *between* arms, and three ticks of the counter.

**Conclusion.** The hypothesis is not supported as a significant effect. A pass's timestamps do not
capture the cost its targets impose on its neighbours, and that cost is real, but on this frame it
is ≤0.2 ms, it lands on the adjacent pass, and the partition property means it can always be found
rather than lost. Reported pass times are a floor by about 20% on a removal basis, not 40%.

`rendering::attributeRemoval()` does this arithmetic for any A/B and is pinned by
`tests/unit/test_render_stats.cpp`; the GPU battery runs the same A/B in-process
("removing a phase is attributed across the whole frame, not just to its own label").

---

## 4. Submitted geometry: Glowmere draws 4.2% of what it contains

```sh
AVGEN_FRAME_COUNTERS=1 ./build/release/src/avgen --headless \
    --project examples/world/glowmere-stylized.json --frames 90 --fps 30 --size 1440x900 --tier realtime
```

> submitted: camera 636397 tris / 2479 inst / 140 draws (46 estimated, 0 unmeasured); depth 636397 /
> 2479 / 140; shadow 1176964 / 1615 / 197 over 67 casters; logical 15107542 tris / 116664 inst;
> binds 100pipe 863group 477vb 477ib (279 redundant avoided); passes 7render 2compute 20unclassified

| | before | after |
|---|---|---|
| headline `tris=` | 15,154,902 | **636,397** |
| what it meant | source triangles × instance records, pre-LOD, pre-cull | triangles the lit scene pass submitted |
| `draws=` | 112 (beside `indirect 154`) | **143** |
| submitted triangles, whole frame | *not measured* | 636k camera + 636k depth + **1.18 M shadow** = 2.45 M |

Three things fall out of that line and none of them were visible before.

1. **The camera pass submits 4.2% of the world's triangles.** Culling and LOD are already doing a
   great deal of work, and until now no number said so.
2. **Shadows submit nearly twice what the camera does.** 1.18 M triangles across two cascades
   against 636 k for the camera. Half the frame's geometry bill is shadow depth. That is a phase 2
   and 3 target nobody had a number for.
3. **The depth prepass duplicates the camera exactly** — 636,397 triangles, 140 draws, twice. Known
   by design; now costed.

The `46 estimated` is the honest part. Those are indirect draws whose instance count came from the
last *completed* cull readback rather than this frame's, because the count is written by the GPU
after the draw is recorded and reading it back would stall the frame being measured. In a still
scene that is exact; under a fast camera cut it lags one to three frames. `0 unmeasured` means no
draw was left out of the totals. See ADR-077.

The pre-cull figure has not been deleted, only renamed: `geometry.logicalTriangles`, 15,107,542.

**Pinned by** `tests/rendering/test_frame_profiler_gpu.cpp`, "submitted geometry is counted apart
from the geometry the world contains": a 1,600-instance scatter reports 19,200 logical triangles
either way, 19,200 submitted with culling off (and `0 estimated`, because a direct draw's instance
count is the CPU's own), and 3,484 submitted with culling on (and `3 estimated`).

### What the state counters do and do not cover

`100 pipe, 863 group, 477 vb, 477 ib, 279 redundant avoided` covers what `SceneRenderer` and
`ProceduralRenderer` record. The particle, SDF, post, AO, volume and shadow-internal renderers are
not instrumented — they are not this phase's files — so each figure is a **floor**. The `279
redundant avoided` is the procedural renderer's state tracking alone; the scene renderer's own
entity loop sets the pipeline and both bind groups for every item whether or not they changed, and
that is deliberately counted as recorded rather than as changed, because it is exactly the
redundancy a later phase is meant to remove.

`passes 7render 2compute 20unclassified` is complete: the three sum to the 29 passes the timeline
measured. Only the two renderers above say what kind of pass they are marking; everything else is
counted as unclassified rather than guessed at.

---

## 5. The CPU frame breakdown

```sh
AVGEN_CPU_STAGES=1 ./build/release/src/avgen --headless \
    --project examples/world/glowmere-stylized.json --frames 180 --fps 30 --size 1440x900 --tier realtime
```

Glowmere at 1440×900, quiet machine, two consecutive samples 30 frames apart:

| stage | ms | | stage | ms |
|---|---:|---|---|---:|
| **lights** (frame uniforms, packing, cascades, froxel encode) | **0.149–0.166** | | post | 0.046–0.054 |
| **shadow** encode | **0.121–0.134** | | uploads | 0.021–0.023 |
| depth encode (prepass, linear depth, AO) | 0.083–0.086 | | objects | 0.020–0.024 |
| procedural (rebuild, upload, effector + cull encode) | 0.082–0.084 | | tonemap | 0.014–0.015 |
| scene encode | 0.079–0.094 | | particles / fields / volume / background | 0.002–0.009 |
| | | | sim / sdf | 0.000 |
| | | | **unattributed** | **0.0000** |

**Total encode: 0.635–0.706 ms** against a 23.9 ms GPU frame and a 26.9 ms wall clock. Phase 0's
conclusion that the CPU is not the bottleneck survives contact with a real breakdown — but it is
now a breakdown rather than two samples, and it says something phase 0 could not: the largest CPU
stage in the frame is **light and cascade setup**, not scene traversal.

The stages partition `render()` in submission order, so they sum to it: the unattributed residual
is 0.0000 ms, and a stage added without a mark would show up there. On the offline path `Finish`,
`Submit` and the queue wait are charged to their own stages too — for a 512×512 test frame that
reads `total 5.66 = ... + submit 0.321 + wait 5.051`, which is to say the wait *is* the GPU frame
and is not smeared across the encode stages.

---

## 6. Instrumentation overhead

**1.46 µs per Glowmere frame**, which is 0.2% of the 0.64 ms CPU encode and 0.006% of the 23.9 ms
GPU frame. Nothing was added to the GPU side at all.

This is a computed total of two directly measured quantities rather than one end-to-end
measurement, and deliberately so: an end-to-end A/B of the two binaries cannot resolve 1.5 µs. The
frame wall clock varies by ±1 ms between runs on a quiet machine and by ±40 ms on a busy one, so a
null result there would have meant nothing. The two measured quantities are, on this machine:

```sh
# scratch programme, source in the phase's working notes
steady_clock::now() 26.0 ns; per-draw counters 1.8 ns
```

and the counts are exact rather than estimated. Per frame: **23 clock reads** — one at the start of
`render()`, one in each of the fifteen stage marks, one for the total, and six in the offline
submission path (the live path pays 17, because it owns its own encoder) — and **477 recorded
draws** (140 camera, 140 depth and 197 shadow, from §4) at ten integer increments each.

`23 × 26.0 ns + 477 × 1.8 ns = 1.46 µs.`

---

## 7. Defects found

Fixed in this phase:

- **`tris=` answered the wrong question.** §4.
- **`draws=112` beside `indirect 154`.** `SceneRenderer` folded in the procedural draw count from a
  copy of `ProceduralStats` taken during `update()`, before any draw is recorded, so the counter it
  read was always zero and the `std::max` beside it fell through to one-draw-per-object for a
  renderer that issues up to four indirect draws per object. Now folded in at the end of encoding:
  `draws=143`.
- **An unwritten pass timestamp was counted but not named.** `unwritten() == 1` says some pass's
  cost was charged to its successor without saying which successor is therefore over-reported.
  `FrameTimeline::unwrittenLabels()` names them.

Found and **not** fixed, because they are behaviour changes and this phase only measures:

- **A scene's mesh uploads are keyed on `meshVersion`, which every fresh `scene::Scene` starts in
  the same place.** Two different scenes with one mesh each, rendered through one renderer, both
  draw whichever was uploaded first (`scene_renderer.cpp`, `uploadMeshes`: `if (scene.meshVersion ==
  meshVersion_ && meshes_.size() == scene.meshes.size()) return;`). A geometry A/B written the
  obvious way therefore measures the same mesh twice and reports a null result. The profiler battery
  works around it by stamping a version derived from the mesh's shape; a benchmark harness that does
  not know to do this will silently mismeasure.
- **The render is not reproducible when the GPU is shared.** The unmodified binary produced
  sequence hash `a9f493eb7afc0c87` on one contended run and `dc0860f8b2db9cf7` on five subsequent
  quiet runs of the identical command. The empty-LOD suppression (`ProceduralRenderer`'s
  `emptyFrames`) advances on whichever asynchronous cull readback has landed, which depends on wall
  clock, so a slow frame changes which LOD levels are recorded and therefore the image. This
  predates phase 1 and is not caused by it, but it means an image-diff regression test is only
  valid on an idle machine.
- **Ten of Glowmere's twelve `post/bloom` passes report exactly zero.** Not a bug in the
  instrument — they are under one tick of the counter — but it does mean the bloom pyramid cannot
  be optimised against per-pass numbers, only against the label's total or a removal A/B.

---

## 8. The image is unchanged

```sh
./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
    --render /tmp/rh --format png --range 8:8.05 --fps 30 --size 640x360
```

| binary | runs | sequence hash |
|---|---:|---|
| before phase 1 | 5 | `dc0860f8b2db9cf7` |
| after phase 1 | 3 | `dc0860f8b2db9cf7` |

`ctest --test-dir build/release -L unit -j4` — 974 tests, all pass (7 of them new).
`./build/release/tests/avgen_render_tests "[profiler]"` — 12 test cases, 1,922 assertions, all pass.

---

## 9. What phase 2 can now be evaluated against

- **Submitted triangles, per class of pass.** A culling or LOD change moves `camera`, `depth` and
  `shadow` independently and visibly. Glowmere's starting point is 636 k / 636 k / 1,177 k.
- **Shadow geometry is the largest single submission in the frame** and was invisible before.
- **The scene pass's 20.7 ms against 636 k submitted triangles** is the ratio phase 2 has to move.
- **Draw and bind counts**, with the caveat in §4 about their scope.
- **A removal A/B that is attributable**, not merely suggestive: `attributeRemoval()` will say
  which pass gave up the time.
- **Nothing under 0.2 ms.** The counter cannot see it.
