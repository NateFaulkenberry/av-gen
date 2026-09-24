# Time Dilation and Per-Entity Local Time

This page belongs to the [Effect Library](README.md). It is the design behind Time Dilation,
Freeze-Frame (Pose), Delayed Motion (Stepped) and the Reverse (Motion) boundary. The visual partner
is Time-Warp Distortion ([catalog-distortion.md](catalog-distortion.md#time-warp-distortion--timewarp)).

**Constraint (from the brief): the global clock does not change.** Audio, the transport, the
timeline and every other owner run on transport time `t`. Dilation is a *per-owner* reinterpretation
of time, and it must keep ADR-700's guarantee that a scrubbed frame equals a played frame.

## 1. What the engine does today

Every fact below was verified in the code.

- **One clock.** `EffectContext::seconds` is "the transport clock, and the only clock"
  (`src/world/effects/effect_timing.hpp:138`). ADR-091 splits authority into two tiers. The **baked
  tier** is pure `Track::evaluate(t)`. The **live tier** is stateful behaviours, which are reset on
  seek.
- **ADR-700: a seek resumes from a checkpoint.**
  - A seek runs on a fixed grid, where step `k` is the instant `k/60.0`.
  - A checkpoint is taken every film second as a replay passes it. It holds every `Entity` *whole*,
    copied by its implicit copy constructor, plus the director (`Staging`).
  - A seek restores the checkpoint strictly before the target and replays forward.
  - The property this design relies on is stated in ADR-700: *"A member added to `Entity` or to any
    behaviour is therefore in every checkpoint without anyone listing it."*
- **Where entity position comes from.** A node's transform comes from parameter tracks, which are
  pure in t. The entity layer then adds `motion_.position + state_.travel`, which is integrated
  state (`src/entity/entity.cpp:2157-2163`). Velocity is a backward difference that the replay
  rebuilds (`entity.hpp:595-601`, ADR-545).
- **Particles** are the one documented relaxation. Pools reset on seek, and warm-up is opt-in
  (ADR-360/395/521).

## 2. The model

Every owner `e` that has a Time Dilation effect gets a **local clock**:

```
τ_e(0) = 0
τ_e(k) = τ_e(k−1) + r_e(k) · Δ           Δ = 1/60, k = grid step
τ_e(t) = τ_e(⌊t/Δ⌋) + r_e(⌊t/Δ⌋) · (t − ⌊t/Δ⌋·Δ)   (an off-grid target's short step, as ADR-700 §1)
```

`r_e(k)` is the dilation rate. It is the *final* value of the parameter `fx/<id>/rate` (base, plus
routes, plus automation) at grid instant `k/60`. For a Region (World-owned Time Bubble), `r_e` is the
parameter times a falloff of the entity's distance to the region. That distance uses the entity's
position at step `k−1`, so the chain is causal.

**Why integrate on the grid.** Playback evaluates `frame/60.0`. A seek replays `k/60.0`. If `τ`
advanced by the render frame's delta, a 30 fps play, a 120 fps play and a scrub would each produce
a different `τ`. On the grid they cannot differ. This is the same reason `cameraVelocityOnTimeline`
differences in timeline seconds (`effect_timing.hpp:142-144`).

## 3. Where τ lives, and why it survives a seek

| Owner kind | Storage | Exactness |
|---|---|---|
| Simulated body (EntityWorld) | `LocalClock{τ, r_prev}` is a **member of `Entity`** | Exact. It is in every checkpoint with no listing, and the replay advances it the way the play did (ADR-700 §3). |
| Track-driven node (baked tier) | The engine keeps a `LocalClock` in a small `ClockBank`. The bank is included in the EntityWorld checkpoint as part of the host half, beside `Staging`. | Exact, on the same argument. |
| World region | Rate is per entity, so each affected entity carries its own clock. | Exact. |

**What the rate may be driven by.** The rate's final at replay step `k` must equal its final at play
frame `k`. That holds for:

- the timeline;
- `time.*`, `beat.*`, `lfo.*`, `music.*`;
- `audio.*` from the offline analysis track (`AnalysisTrack::at(seconds)`,
  `src/analysis/analysis_track.hpp`);
- `env.*` sources that are functions of t.

It does **not** hold for MIDI, `control.*` or live audio input. Those are live-tier by ADR-091, and
a dilation driven by them scrubs as the live tier scrubs, that is, not frame-accurately. The panel
says so when a route from a live source targets `rate`.

**Unverified dependency.** This design assumes that the seek replay step evaluates modulation-route
finals for the parameters it reads at each grid step. The replay step is `EntityWorld::replayStep(now,
stepDt, i, bus, hooks, …)` at `src/entity/entity.cpp:1776`. It receives the signal bus and runs a
`hooks->before` for the director tier first. ADR-700 says that
landing on a checkpoint without replaying "left the parameter finals as the reset left them rather
than as the target step wrote them", which implies that replay steps write finals. The implementer
must confirm that *routes* (not only director writes) are applied per replay step before relying on
a routed rate. If they are not, the fallback is to evaluate the rate parameter's base plus its
timeline automation directly at `k/60`. Both are pure. The fallback only loses non-timeline routes
during replay.

## 4. What reads τ instead of t

The owner and everything that belongs to it read `τ`:

1. **Its tracks.** Parameter tracks bound under `nodes/<name>/…` are evaluated at `τ`. This needs a
   per-prefix time map in the timeline's evaluate. It is the one engine change outside the effect
   system that dilation needs.
2. **Its simulation.** The body's step uses `dt' = r·Δ`.
   - Behaviours, navigation, the locomotion limiter and animation clip sampling all see `dt'`.
   - At `r = 0`, `dt' = 0`, so every backward difference must guard division by `dt'`. The measured
     velocity keeps its last value, which is what a frozen body should report.
3. **Its skeletal animation.** Clip time advances by `dt'`. Motion-matching search runs on its own
   cadence in local time.
4. **Its effects.** When an effect's owner has a clock, the evaluator hands the effect
   `EffectContext::seconds = τ`. Envelopes, noise time, pulse phase and TRIGGER ages are then local
   (`age = τ(t) − τ(t0)`).
   - **Exception:** activation windows (`Activation::Window`, shot-driven activations) stay in
     transport time, because they are cues aligned to the music and the cut.
5. **Its particles.** EMIT systems owned by the entity simulate with `dt·r` and emit by
   `floor(rate·τ)` differences. This needs a per-system dt override in `particle_renderer`.

**What never reads τ:**

- the camera, which is the observer and is what makes bullet time;
- audio;
- the transport;
- other owners;
- HIST, which samples on the transport grid, so a slowed body's trail is dense and a fast one's is
  sparse, which is what a trail should show.

## 5. Negative and zero rates

- **Zero** (Freeze-Frame, Pose) is exact and cheap. The body is stepped with `dt' = 0`.
- **Negative for track-driven nodes** is allowed down to `τ ≥ 0`. The tracks are pure, so evaluating
  them at a decreasing τ is well defined.
- **Negative for simulated bodies is not allowed.** A behaviour cannot be un-simulated. A rewind look
  for a simulated body is **Reverse (Motion)**, which is a *visual* replay of HIST. The body keeps
  simulating forward underneath, then the visual blends back to it. The design prefers an honest
  visual effect over a simulation rewind that would need state snapshots at every step, which is a
  cost ADR-700 already rejected for whole-history replay.

## 6. Rendering consequences

- `prevModel` holds the *rendered* previous transform, so a slowed body has a smaller screen velocity
  and less motion blur. That is physically correct: a slow-motion camera also shows less blur.
  "Shutter-true slow motion", where blur keeps its authored length, is an optional FXL flag that
  scales the velocity written by `1/r`, clamped.
- The visual partner, Time-Warp Distortion, can route `owner.localTimeRate → delayFrames`, because
  the clock publishes its rate as `entity.<name>.timeRate` on the signal bus (see
  [parameters-and-modulation.md](parameters-and-modulation.md)).

## 7. Cost

- **CPU:** one multiply-add per clocked owner per step, plus evaluating that owner's tracks at τ
  instead of reusing the global evaluation, which is a second pass over *its* tracks only.
- **Replay:** unchanged in complexity. Dilated bodies step as often as before.
- **Checkpoints:** +16 bytes per clocked entity.
- **Performance class:** Low.

## 8. What proves it

These tests are extensions of the ones that already exist:

- **Play vs scrub digest.** Extend `tests/unit/test_seek_checkpoints.cpp` and `test_glowmere_scrub.cpp`
  with a body under dilation, with its rate routed from `beat.phase` and from a timeline curve. The
  digest at T must match between a play and a scrub, including past the old 90 s window.
- **Frame-rate independence.** Play at 30 and 120 fps. `τ` must be equal at matching t.
- **Identity.** With `rate = 1` and the effect enabled, frames must be byte-identical to the effect
  disabled. This is the engine's standing "default-neutral" rule; see ADR-372.
- **Freeze.** At `rate = 0` the body's transform is constant, its measured velocity is finite, and
  the camera still moves.
