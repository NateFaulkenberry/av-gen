# The Particle / VFX Lab

Lab #10 of the Engineering Lab Suite (ADR-261, `docs/engineering-labs.md`). Registered in
`src/labs/lab.cpp`; fixture `examples/labs/particle-vfx-lab.scene.json`; cases
`examples/labs/particle/cases.json`, reachable as `avgen --lab-case particle:<n>`.

**The question**: *how many particles are alive, where, and what are they writing?*

**What it owns**: emission, simulation, compaction and the indirect draw — and what particles write
into the velocity target.

**What it does not own**: the world-effect fields that push them. Those are authored scene data and
belong to the World Effects work. The boundary is visible in the fixture: no system here carries a
field force, and the two systems the counting cases read carry no curl turbulence either.

---

## 1. The pipeline, pass by pass

Five compute dispatches per system per frame, in one pass, and the order is load bearing because
dispatches inside a pass are ordered and later ones see earlier writes.

| # | Entry point | What it decides |
|---|---|---|
| 1 | `cs_emit` | which slots this frame's spawns take, and what they are |
| 2 | `cs_simulate` | age, kill, integrate; writes an alive flag per slot |
| 3 | `cs_scan_reduce` | the alive count of each block of 1024 slots |
| 4 | `cs_scan_top` | the exclusive scan of the block sums, the counters, and the indirect draw args |
| 5 | `cs_scan_scatter` | every alive slot's rank, and therefore the alive and dead lists |

There are **no atomics anywhere** (ADR-015 revision 2). Which slot a spawn takes, its seeds and the
draw order are all pure functions of (slot, frame, parameters), which is what makes any of this
measurable at all: two runs of the same frame sequence produce bit-identical buffers.

Both compaction outputs come out of one scan, so `deadList` and `aliveList` are both in slot order
and `alive + dead == capacity` is a property of the pass rather than of the scene. Case 3 is that
invariant, checked on a full pool, an empty one and the frames between.

## 2. What emission is a function of

Two things are added, and nothing in the repository except `ParticleRenderer::update` distinguishes
them:

- The **rate**: `floor(spawnRate * t) - floor(spawnRate * (t - dt))`. ADR-395 made this a pure
  function of the timeline rather than a carry accumulated since the render started. At a constant
  rate from t = 0 it is arithmetically identical to the carry it replaced; what it adds is the case
  a carry cannot express, a frame at t > 0 with no history to inherit.
- The **burst**: `sys.burst`, added whole, every frame.

Then `cs_emit` clamps the total to the free slots the previous frame's compaction left. That clamp
is what `saturated` in the fixture exists to make visible: same emitter as `metered`, capacity 256
instead of 8192, and it must settle at 256 rather than at 1500.

## 3. The randomness

`pcg3d(slot, nonce + seed * 7919, salt)`, where `nonce` is `FrameTime::frameNonce()` — the position
on the **timeline**, quantised to 1/240 s. It used to be the frame index, and ADR-395 has the
measurement: two fresh renderers handed the same `renderTime` and frame indices 0 and 97 differed
over 7656 of 147456 bytes. The frame index counts from wherever the render started, and in the live
application it counts from when the window opened, so an offline render never agreed with the frame
the owner was looking at when they pressed render.

The **slot** is still in the hash, and that is the remaining gap ADR-395 names: slot assignment
comes from the compaction's dead list, which is a function of pool history rather than of time. Two
renders of the same range agree; a spliced render is statistically the same field and not the same
pixels. Case 6 is where to read that, and it says to compare the pool occupancy rather than the
frame, because the occupancy is the part that is exact.

## 4. The instruments

- `ParticleRenderer::readCounts(i)` — the GPU's own `aliveCount` / `deadCount`, read back. This is
  the lab's primary instrument and it is deliberately not the image: an additive billboard field
  saturates, and a saturated region no longer reports its own population.
- `ParticleRenderer::readDrawArgs(i)` — the eight integers the compaction wrote for the two indirect
  draws. ADR-387 in one call: a correct count in the counters proves nothing about what
  `DrawIndirect` was handed.
- `ParticleRenderer::readTrailHistory(i, n)` — the ribbon ring, for ADR-040 questions.
- `ParticleStats` — systems, capacity, `emittedThisFrame` (the request, not the grant), dispatches.
- The lab's overlay profile: `points` and `bounds`. Not `transformTrail`: particles are not
  entities, that overlay draws from the entity history and would draw nothing here, and an overlay
  that draws nothing reads as a measurement that came back empty.
- `--particle-warmup <n>` (ADR-395) — the bounded pre-roll, for questions about the head of a range.

## 5. The fixture, and its own control

`examples/labs/particle-vfx-lab.scene.json`. Four systems, left to right at x = −9, −3, 3, 9, each
isolating one decision:

| system | capacity | rate | burst | lifetime | what it isolates |
|---|---|---|---|---|---|
| `metered` | 8192 | 3000/s | 0 | 0.5 s | the rate, unclamped: settles at 1500 |
| `saturated` | 256 | 3000/s | 0 | 0.5 s | the clamp: the same emitter, bound by capacity |
| `trailed` | 512 | 120/s | 0 | 1.2 s | ADR-040 ribbons, and the one per-frame write still keyed to the frame index |
| `burst` | 4096 | 0 | 64 | 0.8 s | the impulse, separated from the rate |

`metered` and `saturated` are **the same emitter**, and that is the design: the control shares the
frame with the measurement, so it cannot be explained away by anything about the frame — the
exposure, the tone curve, the camera, the seed. A control that needs a second run can always be
argued with.

No field forces, no wind, no attractors, no curl turbulence on the two systems the counting cases
read. Every number this lab measures should be arithmetic over `spawnRate`, lifetime and capacity,
and a fixture that stirred them with a noise field would make every reading a joint measurement of
this lab's code and the World Effects work's.

## 6. Findings

### 6.1 An authored `burst` was inert — **fixed**

The `burst` system rendered nothing on the fixture's first frame.

`registerParticleParameters` (`src/scene/particles.cpp`) seeded `particles/<name>/burst` with a
hard-coded `0.0f` while every neighbour — `spawnRate`, `spread`, `position` — was seeded from the
scene's authored value. So the first `applyParticleParameters` overwrote whatever the file said.
The key parsed, round-tripped through `particlesToJson`, and appeared in the panel, and did nothing.

The comment three lines below it in the same function already describes this defect, about
`extent`: *"It was a multiplier over the authored value, defaulting to 1.0, and that is what this
defect was."* Same defect, one line up, unfixed.

Seeded from `s.burst` now. Every scene that ships authors `burst: 0`, so no existing frame moves.
Case 5, and ADR-399.

### 6.2 The frame index still reaches the trail stride — **not a defect, written down**

`shaders/particles.wgsl` decides whether to write a trail history sample with
`u32(params.sim.z) % stride`, and `sim.z` is the frame index. So the *phase* of the ribbon's samples
depends on where the render started, and a scrubbed frame's ribbon is not the played frame's.

This is inside ADR-360's relaxation and it is not keyed randomness — "write every Nth frame" is a
rate, not a seed, and the alternative (deriving a frame counter from the nonce) needs a frame rate
the shader does not have. Named here so the next person finds the answer rather than the question.
`trailed` in the fixture is the system to reproduce it on.

## 7. What this lab's case format cannot say

A case carries a fixture, a time, a camera, a size, a tier, pass arms and quality arms. It **cannot
set a parameter**. That is a deliberate limit of `labs::LabCase` and it bites here more than
elsewhere, because most particle arms are parameter arms: a different `spawnRate`, a different
capacity, `--particle-warmup`. Those live in `tests/rendering/test_particle_lab_gpu.cpp`, which is
why cases 2, 3 and 4 name the test that asserts them rather than carrying an expectation a reader
would have to check by eye.

## 8. How to use it

```
avgen --lab-case particle:1                  # the bench
avgen --lab-case particle:2 --print          # the recipe, without running it
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[particle][lab]"
```

GPU tests take `tools/gpu-lock.sh` (ADR-170), minima over repeats, never means. `avgen_tests` is
the CPU suite and `avgen_render_tests` is the GPU one; a CPU-only run does not take the lock.
