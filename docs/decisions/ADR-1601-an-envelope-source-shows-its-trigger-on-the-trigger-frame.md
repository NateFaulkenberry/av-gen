# ADR-1601: An envelope source shows its trigger on the trigger's own frame

- Status: Accepted (eng/living-prism-p0, The Living Prism P0-2)
- Amends ADR-900 (the rack's second pass for trigger readers), which gave the route chain this rule and not the source.
- A visible change to every `env.*` user (Digital Ocean's two envelopes, anyone following `docs/control.md`), accepted
  by the owner without a shim (03-integrated-report §14 item 4; ADR-441).
- Code: `src/signals/source.cpp` (`EnvelopeSource::update`).
- Tests: `tests/unit/test_sources.cpp` (`[envelope]`, `[adr1601]`): the ADSR timings shifted to the trigger frame; an
  instant attack peaks at exactly 1.0 on the event frame at 30 and 60 fps and the release starts on the next; a
  trigger on a frame with no time on it (a seek's landing) still lands an instant attack; the random source's
  sample-and-hold holds its new value from the trigger's own frame.

## Problem

`EnvelopeSource::update` stepped its stages by `dt` first and only then looked at this frame's trigger, so it published
the pre-trigger level on the event frame. The next frame ran the attack (instant at 0 ms) and then a whole frame of
decay or release before publishing. Measured by the Living Prism audit (probe B1, 02 §2.4):

- every `env.*` rose one frame after the event: `env.dKick` and a route-chain envelope into a macro knob (itself one
  frame late, ADR-1573) rose on the same frame, 36 of 36;
- the peak was never shown: the first visible value was 1 - dt/release, 0.72 at 60 fps and 0.44 at 30 fps for a 60 ms
  release. The amplitude depended on the frame rate.

At the live target of 30 fps that is 33 ms of latency and half the hit.

## Decision

The trigger is taken before stepping, and the attack is credited from the start of the frame it arrived in, as a route
chain credits an event's rise (ADR-900). The frame the attack completes in shows the full level and leaves the rest of
that frame's time unspent: the decay (or the release) runs from the next frame. A retrigger still continues from the
current level. A trigger on a frame with no time (dt 0) still lands an instant attack.

`RandomSource` was checked the same way and needed nothing: it takes its trigger before slewing, so a new value shows
on the trigger's frame (with `slewMs` 0). A test now pins that.

## Evidence (probe B1, `docs/prototypes/living-prism/probes/B1-transient-timing/`, re-run on this branch)

| | before (02 §2.4) | after |
|---|---|---|
| `macro.kick` rise frame minus `env.dKick` rise frame (the macro is +1 by ADR-1573) | 0, 36 of 36 | **1, 36 of 36** at 30 and at 60 fps: the envelope rises on the event's frame |
| `env.*` value on its rise frame, 13 detectors | 0.72 (60 fps) / 0.44 (30 fps), on the frame after | **1.0000** (min and median, every detector, both rates) |
| value the frame after the rise | | 0.7222 at 60 fps, 0.4444 at 30 fps (= 1 - dt/60 ms: the release starts there) |

## Consequences

- An `env.*` column in `--sonic-trace` (ADR-1571's convention for tracing events) now marks the event's own frame.
  Comparisons made before 2026-10-22 against `env.*` columns carry one frame of the old lag.
- Digital Ocean's two envelopes read a frame earlier and peak at full height at any frame rate.
