# Glowmere Valley 2 — the baseline the original must still hold

Phase 1 deliverable 4. Captured 2026-09-14 on `agent/glowmere-valley-2` at `c232776`, before any
change to the tree. Nothing in this document was measured after an edit, because there are no edits.

The point of a baseline is to be able to say later, with a number, that Glowmere Valley 2 did not
break Glowmere Valley. What follows is split into the part that is **evidence** and the part that
is **not**, because half of what was measured turned out not to be.

---

## 1. Build

```
cmake --preset release -S . -B build/release -DCPM_SOURCE_CACHE=…/.cache/cpm
cmake --build build/release -j 8
```

Configure: exit 0. Build: exit 0, 924 targets, no warnings promoted. Binaries at
`build/release/src/avgen`, `build/release/tests/avgen_tests`, `build/release/tests/avgen_render_tests`.

## 2. CPU tests

`./build/release/tests/avgen_tests` — **exit 0.**

```
test cases:   1583 |   1580 passed | 3 skipped
assertions: 493647 | 493647 passed
```

Three skips are unconditional-skip cases, not failures. This is the number a later phase has to
reproduce or exceed; a Glowmere Valley 2 that lowers it has broken something.

## 3. The original scene still loads and renders

`examples/world/glowmere-stylized.scene.json` — the scene named **"Glowmere Valley"**, shipped in
`examples/index.json` as *Glowmere Valley - Painterly* — loads in an Offline engine, renders at
1280×800 at a fixed t = 4.0 s, and was **looked at**, not only timed:

`build/representation-ceiling/baseline--as-authored-.png`

The frame is correct and recognisably Glowmere: the elder's warm amber gills against an otherwise
entirely cool frame, the teal-to-violet ladder across the understorey, the moon raking the far
hillside, the treeline reading as silhouette. The palette discipline of
`docs/visual-cookbook/bioluminescence.md` — one warm object in a cool world — is intact and is doing
the work it is supposed to do.

Two things are visible in that frame that bear directly on the brief and are recorded here as
observations, not as defects introduced by anything:

1. **The elder's cap is a perfect ellipse.** Its rim is circular, its gills are radially uniform,
   and it reads as a lathe turning rather than as an organism. `docs/stylized-glowmere.md` already
   lists "an overly regular hero" as an open defect; this is what that looks like. It is the single
   strongest argument for the brief's §7.
2. **The water in frame is a stream, not a river.** It is a short bright ribbon behind the elder
   that neither enters nor leaves the frame with any direction. Whatever else is true of the
   original, it has no river in the geographic sense the brief asks for.

## 4. Structural quantities — this is the evidence

`avgen_render_tests "[.perf][representation]"` under `tools/gpu-lock.sh`. 1280×800, fixed
t = 4.0 s, three interleaved runs in one process session, ADR-150's instrument used unmodified.

| arm | triangles | visible instances |
|---|---:|---:|
| baseline (as authored) | **273,819** | **2,020** |
| below 8 px radius culled | 265,204 | 1,319 |
| below 40 px radius culled | 147,221 | 143 |
| below 200 px radius culled | 101,480 | 7 |

**Every one of these reproduced exactly across all three runs.** They are a function of the scene
and the camera, not of the machine, so they are comparable across sessions and are the right thing
to assert against later.

### The scene has grown 3.6% since ADR-151

ADR-151 recorded 264,305 / 260,866 / 137,543 / 99,820 for these same four arms. The baseline arm is
**+9,514 triangles (+3.6%)** and the 40 px arm is **+9,678 (+7.0%)**. Same instrument, same scene
file path, same camera, same resolution, same fixed second — so this is the scene changing, not the
measurement. Fifteen commits landed between ADR-151 and `c232776`, including ADR-158's aim-follow
and a revert of post-processing work. Which commit added the geometry is not established here and
is not needed for Phase 1; it is recorded so that nobody later reads ADR-151's 264,305 as the
current number.

## 5. The timings are not evidence, and the reason is the interesting part

The same runs reported a baseline frame of **50.99 ms** and a scene pass of **19.60 ms**. ADR-151
reports 13.57 ms and 10.88 ms for the same arm. That is 3.8×, and the temptation is to file it as a
regression.

It is not one. `ps` taken during the run:

```
23.0  ./build/release/src/avgen
19.2  WindowServer
11.4  ./build/release/tests/avgen_render_tests
load averages: 6.33 11.02 15.72
```

**An interactive `avgen` window was on the GPU for the whole measurement.** `tools/gpu-lock.sh`
serialises *agents*; it has no claim over a window a human opened, and nothing in the lock's design
pretends otherwise. So the run held the lock, obeyed every rule in the protocol, and still measured
a contended device.

This is worth stating as a rule rather than as an incident, because the repo's measurement
discipline does not currently contain it:

> **The GPU lock does not establish exclusivity — it establishes exclusivity *among agents*.** A
> timing taken while `avgen` is running is not evidence, and `pgrep avgen` is as much a part of the
> protocol as holding the lock. A probe must prove it established the state it claims to measure,
> and "the GPU is idle" is part of that state.

What the contamination does **not** touch is §4: the arms ran in the same contended session as each
other, and the triangle and instance counts are computed on the CPU from the scene and the frustum.
Those reproduced exactly three times. That is why this document reports them as the baseline and
reports the milliseconds only as a record of having been taken.

**No frame-time baseline for Glowmere exists as of this document.** Establishing one is Phase 2's
first task and needs a machine with no window open on it.

## 6. What a later phase must re-assert

| Claim | How |
|---|---|
| the original still builds | configure + build, exit 0 |
| the original's tests still pass | `avgen_tests`, ≥ 1580 passed, 0 failed |
| the original still renders correctly | `[.perf][representation]` baseline arm, capture looked at |
| the original's geometry is unchanged | baseline arm triangles == 273,819 exactly |
| Glowmere Valley 2 did not displace it | `examples/index.json` still lists all seven Glowmere entries |

## 6a. Postscript: the ADR-151 discrepancy, recorded and not chased

Re-running this instrument on a quiet machine during Phase 3 produced a result worth leaving here for
whoever does the renderer gap-closure work.

**In that run the 40 px deletion arm was worth −0.5% of the scene pass.** ADR-151 measured −17.5% for
the same arm on the same scene and built its central argument on it ("the only real number in this
ADR"). Cross-session timings are not comparable and this overturns nothing — but the arms *within* one
run are comparable to each other, and within that run the band ADR-151 called real was inside the 2%
noise floor.

Also worth having: the same instrument gave **15.93 ms** for the baseline arm in one quiet-machine run
and **13.57 ms** in another, where 13.566 ms is ADR-151's published figure to three decimals. Both runs
passed the `pgrep avgen` check on both sides. So ADR-170's clause is necessary and not sufficient, and
a 17% confounder survives it. Thermal state is the obvious candidate and nothing instruments it.

Neither is chased here. A deliberate re-run belongs with the renderer work, not with a scene phase.

## 7. Verified vs assumed

**Verified:** the build, the test counts, the four arms' triangle and instance counts (three
reproductions each), the rendered frame at full size, the contending `avgen` process.

**Assumed:** that the +3.6% triangle delta is scene content rather than a change in how triangles
are counted. Not checked — the counter's code was not read. If a later phase finds the counter
changed, this section is wrong and ADR-151's numbers are still current.
