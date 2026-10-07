# ADR-1161: An integrating route can be bounded

- Status: Accepted (proto/digital-mosh)
- Amends ADR-1041 (the integrate stage).
- Found by: DIGITAL MOSH. Its arc needs a memory of how much intense music has passed: a *dose* that a long drop
  fills and a breakdown drains.

## Problem

ADR-1041's integrate stage outputs the running total of the chain's value over time. A dose is the integral of
`energy - threshold`, but an unbounded total cannot be one:

- **A quiet intro builds a debt.** Two minutes at -0.2/s puts the total at -24. The target parameter clamps
  that to 0, but the state keeps the -24, so the first minute of the drop only repays the debt and the picture
  does not move.
- **A long climax builds a surplus.** The breakdown after it spends its whole length draining the surplus
  before the target leaves 1.

Clamping the target (its range, or the chain's `clamp` stage, which runs before integration) changes the
output and not the state, so neither helps.

## Decision

The chain has two new members, `integrateMin` and `integrateMax`. They default to -infinity and +infinity,
which is ADR-1041 unchanged. The running total itself is clamped to them every frame, so a bounded integral
saturates at a bound and starts back from it the moment the rate changes sign.

- **JSON:** `"chain": {"integrate": true, "integrateMin": 0, "integrateMax": 1}`. The bounds are written only
  when finite (JSON has no infinity), so every existing project round-trips byte for byte. A crossed pair is
  refused at load.
- **Editor:** the route row in the Control panel gains the `integrate` checkbox. It was reachable only from
  JSON before. When `integrate` is on, a `bounded` checkbox and a min/max pair appear.
- **Seek:** the bounded total is chain state like any other. It replays (ADR-901), and the replay key includes
  the bounds.

## Consequences

- A dose, a charge or a fatigue meter is one route: `energy → macros/dose`, offset -θ, integrate, bounded to
  [0, 1]. DIGITAL MOSH's arc is built on it.
- Tests (`[adr1161]`):
  - an hour of +0.5/s sits at 1, and one second of -0.25/s afterwards reads 0.75;
  - the floor holds, and a rise starts at once;
  - unbounded is unchanged;
  - the JSON round-trips, absent means unbounded, and a crossed pair is refused.

## Rejected alternatives

- **A new "dose" or "accumulator" source.** It would be a second integrator with its own UI, persistence and
  seek path, for one more clamp.
- **Clamping only the output.** That is the debt and surplus problem above.
