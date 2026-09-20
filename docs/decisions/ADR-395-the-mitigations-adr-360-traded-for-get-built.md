# ADR-395: The mitigations ADR-360 traded for get built, and the third fact it did not name

- Status: Accepted (2026-09-20)
- Builds on ADR-360 (particle determinism relaxed, mitigations promised), ADR-091 (two-tier
  simulation authority), ADR-015 (GPU particles, revision 2: no atomics), ADR-012 (the time model),
  ADR-182 (a probe that cannot fail proves nothing).

## Problem

ADR-360 relaxed determinism for particle systems on the owner's decision, and named what it was
trading for. Nothing it named was built. Verified against `main` at 9084db5d:

- `FrameTime::frameNonce()` had six call sites — AO, the tonemap dither, the volume march — and not
  one of them was particle code.
- The bounded opt-in warm-up did not exist anywhere, under any name.

`FixedStepClock::seek` zeroing `frameIndex` was also on that list. **It is not a defect and it is
not changed here.** `frameNonce`'s own documentation says `frameIndex` counts from wherever the
render started, `tests/unit/test_core.cpp` asserts it, and `restartAt` exists so an offline render
can say "frame f is at start + f/fps". The defect was never the clock; it was that particles keyed
randomness to a counter whose documented meaning is "frames drawn", not "position on the timeline".

## What was measured, before

`tests/rendering/test_particle_determinism_gpu.cpp`, two *fresh* renderers — so pool history is
identical and cannot be the variable — handed the same `renderTime` and different frame indices:

| arm | result |
|---|---|
| t = 1.75 s, `frameIndex` 0 | 426 lit pixels |
| t = 1.75 s, `frameIndex` 97 | 410 lit pixels |
| byte difference | **7656 of 147456, max delta 235** |

That is not scrub fidelity. `RealtimeClock::seek` does not reset `frameIndex` at all, so in the
live application it is the number of frames since the window opened — an arbitrary number that is
never 0. An offline render of the same second therefore never agreed with the frame the owner was
looking at when they pressed render. **That was the serious half, and the brief was right to ask
whether it was true.** It was.

The control in the same test — a different timeline second at the same frame index — differs, which
is what stops the assertion passing because nothing was emitted.

## Decision

1. **The spawn hash is keyed to `frameNonce()`.** `Params` gains `nonce: vec4<u32>`; `cs_emit`
   hashes `(slot, nonce.x + seed*7919, salt)`. `sim.z` remains the frame index and remains what the
   trail-history stride counts, because "write every Nth frame" is a rate and not a seed. Nothing
   else in `particles.wgsl` may key randomness to it, and the comment above it says so.

2. **Emission becomes a pure function of the timeline.** `floor(rate*t) - floor(rate*(t-dt))`
   replaces `emitCarry`. At a constant rate from t = 0 this is *arithmetically identical* to the
   carry — the carry's running total is `floor(rate*t)` — so no render that starts at zero moves by
   one particle. What it adds is the case a carry cannot express: a frame at t > 0 with no carry to
   inherit. It is also partition independent, so one stalled frame and two short ones emit the same
   total.

3. **A bounded, opt-in warm-up.** `ParticleFrameContext::warmUpFrames`, capped at 240, steps the
   pools that many frames at the timeline seconds immediately preceding the frame about to be
   drawn. Off by default: ADR-360 kept the hard pool reset on seek deliberately, because
   `EntityWorld::seek` already re-simulates up to 90 s per scrub click and is the measured cause of
   the app's scrub lag, and this would be a second re-simulation stacked on the first. Reachable
   from the batch path as `--particle-warmup <n>`, which is the case ADR-360 actually cared about.

   Each warm-up step is its own command buffer. That is not an oversight: a pool's per-system
   uniforms are one buffer, and `Queue::WriteBuffer` orders against *submits* rather than
   interleaving inside an encoder, so W steps sharing one encoder would all read the last uniform
   written and the warm-up would be W copies of one frame.

## What was measured, after

A drizzle at 6000 particles/second with a 0.5 s lifetime, alive count at t = 2.0 s:

| arm | alive |
|---|---|
| full render, played 0 → 2 s | 2900 |
| partial render opening at t = 2, no warm-up | **100** |
| partial render opening at t = 2, 36 warm-up frames | **2900** |

Exactly 2900, not approximately: the warm-up window (0.6 s) exceeds the lifetime, so every particle
alive at the head was born inside it, and decision 2 makes each of those frames emit the count a
full render emitted. 100 is one frame's worth — the bloom ADR-360 describes, as a number.

The nonce arm is now byte-identical across frame indices, and its control still differs.

## The third fact, which ADR-360 did not name

**A spliced render is still not bit-identical to the full one, and this does not make it so.**

Slot assignment comes from the compaction's dead list. The list is sorted (the scatter pass writes
both lists in slot order), so it is a function of *which slots are alive*, which is a function of
pool history rather than of time. A warm-up starting from an empty pool therefore lands the same
particles — same birth second, same nonce — in different slots from a full render, and `rand3`
keys on the slot, so they get different velocities. The field is statistically the same and
pixel-for-pixel it is not.

Closing it means keying the spawn hash to the **emission ordinal** within the frame rather than to
the slot, so the set of particles born at t is a pure function of t and the slot only decides where
one is stored. That is a five-line change and it reshuffles every particle scene in the repository,
which is a different decision from this one. ADR-360 promised the nonce and the warm-up. It did not
promise this, and it is not taken here.

The coordinator's standing quotation of ADR-091 as "scrub must equal play" is worth correcting in
passing, because it changes what this ADR is for: ADR-091 is *two-tier simulation authority* and
documents some entities as not frame-accurate under scrub. The contract these mitigations serve is
ADR-360's own — *scrub may differ from play; two renders of the same range may not differ from each
other* — and the gap above is a gap in **splicing two different ranges**, not in re-rendering one.

## Consequences

- `--particle-warmup` costs n extra compute submits at the head of a render and on any seek that
  reaches the renderer. It is off unless asked for, so nothing that exists today pays it.
- The warm-up's step is the frame's own delta when it has one, and 1/60 when it does not (an
  offline render's first frame has no delta yet). A render at another frame rate whose first frame
  carries no delta warms on 60 fps spacing and lands on slightly different seconds than the full
  render would have. Bounded and small; named here rather than discovered later.
- `Pool::emitCarry` is gone. Anything that was reading a partially accumulated spawn as state is
  reading a pure function now.

## Revisit when

- Somebody needs a spliced render to be bit-identical, at which point the emission ordinal above is
  the change, and it should be made once and measured against every shipped particle scene.
- The warm-up acquires a second consumer. It is already frame-counted and capped for that reason;
  see ADR-397.
