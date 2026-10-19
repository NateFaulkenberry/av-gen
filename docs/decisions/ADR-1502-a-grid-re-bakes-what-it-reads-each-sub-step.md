# ADR-1502: A grid re-bakes what it reads at each of its sub-steps

- Status: Accepted (art/generative-pass)
- Amends ADR-1407 (bake grids) and ADR-1119 (seek and catch-up). Changes nothing for a scene in which no grid reads a
  bake grid.
- Found by: the Generative pass. An RD Pins render started mid-track (`--range 160:162`) showed the right pattern on
  its first frame, then lost most of it within a few frames. The same happened on main before this pass.

## Problem

A grid reads other grids through its input fields (`regimeField`, `injectField`, ...), and every field read on the GPU
sees the shared grid table. That table is published from the ping-pong buffers once, at the end of a frame. Two
things followed:

- **Seeks were wrong.** A seek or a fresh render at t replays every sub-step from 0 to t in one frame (the catch-up).
  Every replayed step read the table as it was before the frame: zeros on a cold start. RD Pins' regime map (a bake of
  four moving fields) therefore read as labyrinth everywhere for 160 s of replay. When the real map arrived on the
  next frame, the spot territory collapsed. Measured on a test plane: seek against play, max |difference| 0.62.
- **Play lagged by one frame.** The bake was published after the steps that read it.

## Decision

Before each of its sub-steps, a grid that reads a bake grid re-runs that bake at the sub-step's own clock (the
sub-step's field block) and publishes it to the table. The read is followed through compounds, and covers the regime,
inject, frequency, velocity, conductivity and deposit fields. The step's field reads then see exactly that bake, in
play and in a catch-up alike, so a seek replays bit for bit what play did, independent of the frame rate.

- **Cost:** one bake dispatch, one compute-pass break and one table copy per sub-step, on grids that read a bake only.
  RD Pins: a 128² bake twice a frame at 30 fps. A 160 s seek (9,600 sub-steps) rendered its 12 frames in 5.7 s,
  catch-up included.
- **Exact, not checkpoint-granular:** the coordinator asked for exactness first, and the measured cost made the
  checkpoint-granular fallback unnecessary.
- **The table after a frame** holds the bake at the frame's last sub-step, not at the frame's own clock. This
  differs by less than one sub-step, and only for a bake that a grid reads.

Grids that read a simulated (non-bake) grid keep the one-frame publication lag. They are stateful, and re-running them
per step would be a different change.

## Consequences

- Seek equals play for every grid that reads a bake. The examples affected, each rendered before and after, look
  right (`~/Desktop/av-gen-review/70-generative-pass/proofs/adr1502-bake-readers-60-160s.png`, first and twelfth
  frames):
  - RD Pins (`examples/rd-pins`, the regime bake);
  - RD Pins (R&D) (`examples/experiments/rd-pins`);
  - RD Bloom (`examples/experiments/rd-bloom`);
  - RD Relief (`examples/experiments/rd-relief`).
  The flow experiments read simulated vector grids and are unchanged.
- Test: `A grid reading a bake grid replays a seek exactly as it played` (`[simulation][gpu][rd][adr1502]`). An RD
  plane whose regime is a travelling wave, baked: a fresh frame at 5 s equals 150 played frames at 30 fps exactly
  (max |difference| 0; 0.62 before this change). A frozen regime grows a different pattern, so the read is real.
