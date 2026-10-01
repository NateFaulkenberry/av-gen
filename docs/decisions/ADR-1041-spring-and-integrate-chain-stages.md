# ADR-1041: Spring and integrate route-chain stages: continuous musical modulation

- Status: Accepted (2026-09-30), proto/liminal-space
- Extends ADR-011 and ADR-900; seek-exact through ADR-901. Implemented in `src/params/processor.*`,
  `src/params/serialization.cpp`, the checkpoint keys in `src/app/engine.cpp` and
  `src/scene/composition.cpp`, and the liveness rule in `src/params/liveness.cpp`.
- Tests: `tests/integration/test_liminal_motion.cpp` (`[liminal][adr1041]`).

## Context

The owner's §4 complaint about the Procedural Space POC: geometry "stuck between states and rapidly
switching between configurations". That was scene states and sample-and-hold sources. The brief asks
for continuous autonomous motion plus smooth musical modulation, and the addendum for bass as a
whole-world breathing and silence as a real absence of activity. The one-pole (attack/decay) smooths,
but its output has a corner at every input change, and nothing turned a rate into a position.

## Decision

Two stages at the end of `ProcessorChain`, after the envelope:

- **`springHz` / `springDamping`**: a second-order follower `x'' = w^2 (u - x) - 2 zeta w x'`, `w = 2 pi
  springHz`, seeded at rest on the first input, integrated by semi-implicit Euler in equal sub-steps of at
  most 1/480 s (stable to 50 Hz). Velocity is continuous, so a change eases in and out; zeta 1 is
  critical, < 1 overshoots and settles (breathing), > 1 is sluggish. Off at 0.
- **`integrate`**: after remap, the chain outputs the running integral of its value over time (a double).
  A pace becomes a distance and a flow speed a phase; the output never jumps and a zero rate holds it.

Both keep all their state in `ProcessorChain::State`, which ADR-901 already replays on the 60 Hz grid and
carries in checkpoints; both are mixed into the two checkpoint keys. JSON writes them only when used.

## Consequences

- Measured: a critically damped step never overshoots and changes velocity by at most w^2 dt^2 per
  frame; the response at 30, 60 and 144 fps agrees within 0.02 at shared instants; spring and integrate
  routes land a seek where a play does, bit for bit at 60 fps, with and without audio.
- **Never put a depth (ADR-900) on an integrating route.** Depth scales what the route writes, which for
  an integrator is the whole accumulated total: changing it moves the camera by the depth change times the
  distance travelled. Gate the rate before the chain instead (a threshold, or the source).
- The liveness `flat-chain` rule read an integrator as dead (its output after one zero-length step is 0 for
  every input); it now reads an integrator's rate.
- Not in the route panel's UI; set them in the project JSON.
