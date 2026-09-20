# ADR-385: A stated reason is not evidence, and the metering readback becomes a ring

- Status: Accepted (2026-09-19)
- Builds on ADR-037 (exposure and the metering loop's determinism), ADR-039 (image formation),
  ADR-170 (one GPU, one run), ADR-182 (a probe that cannot fail), ADR-350 (a system the application
  ran and did not keep), ADR-368 (a disabled path is a property of the encoder), ADR-372 (AgX's
  inverse, and three things built but unreachable).
- Closes the Image/Look tail. `docs/image-look-audit.md` §9.

## 1. A stated reason is not evidence

`post/bloom/levels` did not exist. The scene block's reader said:

```cpp
if (key == "bloomLevels") {
    // Fixed when the bloom pyramid is created, so there is no parameter to move. Accepted
    // and ignored rather than warned about: the scenes that name it are not wrong to.
    continue;
}
```

**Every clause of that comment is false.** `PostProcessor::run` clamps and reads `s.bloomLevels`
**on every frame** — `post_processor.cpp:612` for the bloom pyramid and `:649` for halation's, both
inside `run()`, which begins at `:456`. Nothing is fixed at creation. And
`tests/rendering/test_image_formation_gpu.cpp:362` has set it to 3 and asserted the pyramid's energy
is unchanged **since ADR-039**, which is a test that only means anything if the setting is live.

So this was not a control that could not work. It was **a working control nobody had wired,
protected by a justification that was false** — and that is why it survived five separate rounds of
removing this exact family (ADR-372 alone removed three). Everyone who looked at it read the reason
and moved on. The reason was load-bearing and nobody load-tested it.

**A stated reason is not evidence.** The next instance of this family will also come wearing an
explanation, and the explanation is the thing to check first, because it is what stops anyone else
from checking.

Fixed: `post/bloom/levels`, range equal to the clamp `run()` already applies, reader, writer, an
ADR-350 round trip checked by named key, and a GPU test that the pyramid changes depth, that the
frame changes with it, and that it is inert when bloom is off.

The same pass corrected `post_processor.hpp`'s claim that *"with everything off and a unit exposure
the input is returned unchanged"*. That sentence was quoted into `docs/image-look-spec.md` as proof
that the spec's own §60 guarantee already existed. It did not, and the misquotation shaped the whole
§60 argument. **The spec was wrong because the header was wrong**, so the header is what changed.

## 2. The metering readback becomes a two-slot ring

§66 of the Image/Look brief says: no CPU readback for exposure. There was one.
`PostProcessor::takeMeasurement` mapped a 256-byte buffer and blocked the main thread on
`context_.waitFor`.

**Why it cost anything.** With a single buffer, frame *N* maps the copy issued by frame *N−1*. On a
pipelined renderer the CPU runs ahead, so that copy has often not executed, and the blocking map
drains the pipeline. It is not the map that is slow; it is the sync point.

**Measured before**, automatic against manual exposure (manual never maps, so the pair isolates it),
`examples/hero/hero.json`, live editor, 420 frames, 3066x1770 (5.43 Mpx), three interleaved repeats,
quiet machine, minima over repeats:

| | `gpu.frame` best | `FRAME` best |
|---|---|---|
| manual | 22.020 | 23.132 |
| automatic | 22.020 | **24.709** |

GPU **+0.000 ms** — the six metering passes cost nothing measurable. CPU **+1.577 ms**, with
non-overlapping three-sample ranges. 6.4% of the frame, on the main thread.

**The fix** is a two-slot ring. `takeMeasurement` reads the slot `encodeMetering` is about to
overwrite, which with two slots is the one written two frames back; the copy has long landed and the
map does not stall. Determinism survives because the map is still blocking and still ordered: what
ADR-037 asserts is *which* frame's measurement is used, not how long the CPU waited for it.

**Measured after**, same harness:

| | `FRAME` best, manual | `FRAME` best, automatic | delta |
|---|---|---|---|
| before | 23.132 | 24.709 | **+1.577 ms** |
| after | 22.511 | 22.238 | **−0.273 ms** |

Automatic exposure is now **indistinguishable from manual** — the −0.273 ms is inside the arms' own
spread. The absolute numbers moved between sessions (machine state); the paired delta is the
measurement, which is why the design is paired.

## 3. The latency question, and the answer that surprised me

Going from "frame *N* uses *N−1*" to "*N−2*" ought to add a frame of adaptation latency. The claim
to falsify was that this is invisible. **It is not invisible; it does not exist.**

`tests/rendering/test_image_look_gpu.cpp`, hidden test `[.adapt]`: a **ten-fold step** in scene
brightness at frame 40 — deliberately the worst case, because the day/night cycle crosses dusk over
tens of seconds and an extra 17 ms could not show there. 120 frames, EV recorded per frame. Run at
one slot (which reproduces the old behaviour exactly) and at two — one constant, everything else
identical:

| | first frame EV moves | max &#124;Δ EV&#124; vs 2 slots |
|---|---|---|
| 1 slot | 42 | **0.00000000** |
| 2 slots | 42 | — |
| **4 slots (control)** | **44** | **0.10001** |

**One slot and two slots are byte-identical over all 120 frames.** The loop already had a two-frame
floor and a two-slot ring fits inside it.

**The four-slot arm is why that means anything.** Byte-identical curves are exactly what a build
that never picked up the change would also produce, and the two files being identical was suspicious
enough to stop on. The control lags by exactly two frames and 0.100 EV, which proves the mechanism
is live and the comparison is not vacuous (ADR-182). Binary mtimes were checked against curve mtimes
as well.

One limitation, stated rather than buried: the harness renders through `renderToImageFloat`, which
synchronises every frame, so it measures the **offline/deterministic** path. In the live editor the
CPU runs ahead and the ring does trade one frame. From the four-slot control, two extra frames cost
at most 0.100 EV transiently, so one costs at most **~0.05 EV** — a twentieth of a stop, during an
instantaneous ten-fold change, and gone within a frame.

## 4. The re-baseline: nothing moved, and that is checkable

Condition three was to re-baseline in its own commit and say which committed frames moved.

**None.** `ExposureSettings::mode` defaults to `Manual` (`scene_types.hpp:43`), and **no committed
project in `examples/` sets `camera/exposure/mode` to 1** — the `camera/exposure/minEv` entries that
appear in several projects are the ADR-037 highlight-guard idiom and are inert under manual mode.
So no shipped frame touches the metering path at all and none can move. Independently, §3 shows the
offline EV curve is unchanged even for a scene that *did* use automatic exposure.

This is why there is no separate re-baseline commit: there is nothing to re-baseline.

## Consequences

- Automatic exposure is no longer a 1.58 ms tax on the main thread, and §66 is satisfied in
  substance rather than by argument.
- `resetExposure` must clear **every** slot and the write cursor, not just the current one, or a
  scene swap leaves a stale reading that surfaces two frames later — the bug the reset exists to
  prevent, arriving further from its cause. Same for the manual-exposure branch in `run()`.
- Anyone changing `kMeterSlots` should re-run `[.adapt]` at the old and new values. It is the arm
  that tells the difference between "no effect" and "no rebuild".
