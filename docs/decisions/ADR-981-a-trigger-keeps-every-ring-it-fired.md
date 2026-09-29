# ADR-981: A trigger keeps every ring it fired

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, item 2 (`docs/glowmere-valley-3/art-pass/00-brief.md`): "When the next
4-beat pulse is triggered, the previous pulse/wave appears to be abruptly removed/reset."
**Amends:** ADR-702 (Ground Pulse and Travel Beam as effect types), the Wave 2 TRIGGER activation
**Implemented by:** `world::resolveWaveFronts`, `world::resolveWaves`, `world::kMaxGpuWaves` (8 -> 16),
`world::kMaxWaveFronts`, `FrameUniforms::waves` and its WGSL twin in `shaders/common.wgsl`
**Tests:** `tests/unit/test_wave_effects.cpp` (`[adr981]`), and the renderer layout guards

## Context

Glowmere Valley 3 fires each hero's Ground Pulse on a cue marker every bar ("hero pulse <hero>",
`tools/gv3/look.py`): a ring from the hero's feet, 13 m/s, 4.5 s of life, one marker every 1.846 s.

A `Trigger` activation's window was the LATEST event's, `[t0, t0 + delay + lifetime)`, and a surface wave drew
one front from it. So when the next marker fired, the window jumped to it: the ring then 24 m out, at full
strength, vanished in one frame and a new one started at the hero's feet. Every ring in the film was cut off 41%
of the way through its life. The Shockwave and the Ripple never had this: they draw one front per recent event
(`effectEventTimes`). The surface waves were written before TRIGGER existed and kept the single window.

## Decision

- **A trigger with a lifetime gives each event its own front** (`resolveWaveFronts`): up to `kMaxWaveFronts` (4)
  of the latest events, each living `[t, t + delay + lifetime)` with its own fade in and fade out. The newest
  front's window is the one `resolveActivationWindow` gives, so its record is bit for bit `resolveWave`'s, and a
  pulse whose rings never overlap is the pulse it was.
- **Every other activation, and a trigger with no lifetime** ("until the next one", by definition), keeps its
  one front.
- **Newest fronts are placed first** (`resolveWaves`, two passes): each instance's newest front in evaluation
  order, then the earlier ones. An old ring never takes a slot a new pulse needs; an instance is `Dropped` only
  when its newest front does not fit.
- **`kMaxGpuWaves` 8 -> 16.** The loop in `wave_effects.wgsl` runs over the live count, so empty slots cost
  nothing; the frame block grows 1152 bytes, and every offset after `waves` moves by that (asserted in
  `scene_renderer.hpp`, mirrored in `common.wgsl`, checked by the layout guards).

## Consequences

- Glowmere Valley 3's hero pulses keep their look, speed and timing; each ring now runs its full 58 m and fades
  over its last 1.2 s while the next one starts behind it, so two or three coexist while a hero leads a shot.
  Counted from the project's own markers: at most 7 fronts live at once in the whole film (the travel beam
  included), against 16 slots.
- The kick route (`timeline.kick -> fx/<id>/intensity`) lifts every live ring of that hero at once: they are one
  instance's fronts, and one instance has one intensity.
- A travel beam fired on markers further apart than its lifetime (Glowmere Valley 3's: 7.4 s apart, 3.2 s of
  life) is unchanged.
- Per-fragment cost is per live front; measured with the art pass's A/B (PROGRESS.md).
- **Amended the same day, after the art pass's one Critic run.** Running every ring its full 58 m at GV3's old
  timing (4.5 s, full strength to 3.3 s) carried rings through foreground the cut had always kept them out of:
  the first final measured 20-40% more mean luma than r7b in many shots and more clipping in nine (s65 0.22 ->
  0.48, s36 0.20 -> 0.47; the Critic's lighting -6.9 and technical quality -10.5 points). The engine is
  unchanged; GV3's timing is now 2.6 s with a 1.1 s fade (`look.HERO_PULSE_TIMING`): full strength to 1.5 s, at
  77% when the next bar's ring leaves, gone at 34 m -- two rings coexist briefly and the older always finishes.
  Measured at seven of the affected moments against r7b's project: within a few hundredths of its mean luma
  except where a new hero's ring is meant to show (PROGRESS.md, "The Critic").

## Rejected alternatives

- **Several fronts packed into one GPU record.** Cheaper per fragment, but a new record layout, new lanes for
  per-front envelopes and every packing test rewritten, for a loop that already skips empty slots.
- **Making it opt-in per instance.** A ring that vanishes mid-flight is never what anybody authored a lifetime
  for; the lifetime is the statement that each ring lives that long.
- **Changing the Trigger activation window itself** (for every type). Distortion and shell types answer "several
  fronts" their own way (`effectEventTimes`) and a window that reported the oldest live event would move them all.

## Revisit triggers

- A scene that fires one surface-wave instance faster than `lifetime / kMaxWaveFronts` (more than four rings of
  one instance alive), where the fifth-oldest ring would be cut.
- A frame that reports surface waves `Dropped`.
