# ADR-2000: A crest throw crouches on the rising tile, pops at the crest's top and flies on one clip; the Throw Lab

- Status: Accepted (Musical Geography, CPD-R7, the owner's note on `cpd-r4b-closepass1-sea-day-45-53s.mp4`,
  2026-11-11), scene/musical-geography. Amends ADR-1997 and ADR-1998 (the throw's timing and its clips).
- Code:
  - the hop's `Crouch` phase, the crest-top launch, `throwClipPop`, `throwClipDown`, `throwCrouchRate`,
    `throwClipLift` (`src/entity/behaviors.cpp`);
  - `EntityState::clipOrigin` / `clipRate` (`src/entity/behavior.hpp`), `LocomotionState::clipClock`
    (`src/entity/locomotion.hpp`), `EntityWorld::applyClipClock` (`src/entity/entity.*`), the animation sink's
    rebase on that clock (`Composition::AnimationSink::setLocomotion`);
  - `Composition::lowestFootY` (the lab's feet), `tools/throw_lab.py`, `tests/integration/test_throw_lab.cpp`.
- Tests: `[adr2000]` in `tests/integration/test_tile_motion.cpp`: each throw crouches first on the drawn tile top, on
  the clip clock, the pop still ahead; the launch is at the crest's top under the body (to a millimetre on the 60 Hz
  grid) with the clip at its pop; the whole flight stays on the jump state, its clip never past its touchdown. The
  existing seek tests (`[adr1997]`, `[adr1998]`, `HopScene`) run with it.

## Context

The owner: "around 1 second you see the alien levitating off the surface of the tile, as if the wave is passing
directly under him already, the 'height' of his crouch should match the peak of the waveform ... when he jumps that
popping animation will be timed with the crest of the wave (tile height max) ... at the top of the jump ... it looks
like he hangs for a second, transitions to a different animation state, then falls."

Measured in the Throw Lab (a flat grid, one 1.2 m wave, 48 m, 8 m/s, sharpness 0.55, crest at 3.00 s; art's alien and
hop settings), the ADR-1998 timing (`throwlab-plot-before.png`, `throwlab-before.csv`):
- **Levitating: the body leaves the tile while the clip is still crouching.** The throw launched at the swing's
  fastest rise (2.38 s, 0.62 s before the crest's top) and started the `Jumping` clip there. That clip is authored in
  place and keeps the feet on its floor until clip 0.60 s (its pop), then lifts them itself, up to 1.52 m at 1.94x,
  back on the floor at 1.12 s (probed on a still tile). So for 0.6 s the alien rose 3 m in its crouch pose, above the
  tile.
- **Launch against the crest: 0.62 s early.** The visible crest's top passed under the alien when it was already
  3 m up.
- **The apex hang: a state switch to a hover.** `Jumping` ran 2.38-3.42 s (clip 0-1.04), then at the apex the hop
  switched to `Fall`, which the cast maps to `Floating` (a 6.5 s hover with the feet held 0.7 m above the root): a
  0.25 s cross-fade (the feet snap 1.0 -> 0.2 -> 0.7 m above the root) and 1.15 s of hovering, then `Landing`.

## Decision

1. **A crouch on the tile first.** When a rising swing arms a throw, the body looks ahead to the crest's top (where
   the rise falls to nothing) as well as the fastest rise. It enters a `Crouch` phase at `crest - throwClipPop /
   throwCrouchRate` (mid-crouch when it armed later): it stands on the tile's drawn top, riding the crest up, activity
   `Jump`, the jump clip on its own clock so its pop lands on the crest's top.
2. **The pop at the crest's top**, at `tossGain` x the swing's fastest rise (as before), less `throwClipLift`: the arc
   gives up that much apex because the clip lifts the feet itself (0 by default; the aliens' clip lifts 1.52 m).
3. **One clip through the flight.** The jump state is kept through the whole flight (no `Fall`, no hover), its clip
   stretched so the airborne part (`throwClipPop` to `throwClipDown`) spans the flight: the clip's own in-place jump
   rises, tucks and falls with the arc, and its feet come down as the body lands. `Landing` follows unchanged.
4. **A behaviour's own clip clock.** `EntityState::clipOrigin` / `clipRate` carry it to the animation layer, which
   (re)starts the state at that origin and rate, once per change. Deterministic: the clock is part of the behaviour's
   checkpointed state, and the seek publishes it as a play does.
5. **The Throw Lab** (`tools/throw_lab.py`, the hidden `[measure][throwlab]` case): from a built MG scene it writes a
   lab project (flat grid, one scripted wave, the art's alien standing where the crest passes, a side or a close
   camera), measures per frame the tile top, the crest alone, the body, the drawn root, the posed feet
   (`lowestFootY`), the state, clip time and blend, and the launch, plots them on one axis, and renders the clip
   through the GPU lock. A measure takes about 3 s; a 7 s clip about a minute. Not registered as an Engineering Lab
   (no menu entry): it is a measure aid, like the CPD-R6 trace.
6. `AVGEN_THROW_ADR1998=1` is the A/B arm (ADR-1998's timing: the fastest rise, no crouch, the fall clip at the apex).

## Consequences

Measured in the lab, same wave and settings (`throwlab-plot-after.png`, with `throwClipLift` 1.5):
- the feet stay on the drawn tile top through the crouch (2.40-3.00 s), riding 2.4 m of crest;
- the launch is the crest's top (3.00 s, the tile at its highest);
- one `Jumping` state from 2.40 s to touchdown at 4.95 s, the feet's path one smooth arc (apex 6.85 m, the body's
  arc 5.3 m + the clip's own 1.5 m), then `Landing`.

The pop now comes at the crest's top rather than its fastest rise, so the alien leaves from higher (the crest's
height above its rise point). The crouch reads the clip's first 0.6 s at rate 1; `throwCrouchRate` slows it.
