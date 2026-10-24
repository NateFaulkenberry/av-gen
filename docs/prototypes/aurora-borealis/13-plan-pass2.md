# Aurora Borealis pass 2: the implementation plan (coordinator, 2026-10-24)

Synthesises 11-research-pass2-render.md (A) and 12-research-pass2-signals.md (B) against 10-brief-pass2.md.
The reports hold the measurements; this file holds the decisions. Owner approved B's five calls ("go").

## Decisions

| Question | Decision |
|---|---|
| Palette flicker (B §1) | Fix first: smoothed `signal.synthPresence` as depth (in progress) |
| River definition | ONE river curve (A's rounded-triangle loop, R0 2400-3000 m) feeds land, water, camera, pulses, edges |
| Loop length (~880 s at 22 m/s) | Accepted: a live scene loops; the song sees part of it |
| Camera height | Start at 85-95 m (the brief asks for a wide valley view), pitch +2-3°, target 1.5-2.2 km ahead, 24 mm; tune on a moving clip so the near water still carries aurora; owner judges at Gate A |
| Shoreline | No lip: continuous 7-15° slope through the waterline, jittered row offsets, spurs held behind the floor |
| Edge glow | Baked edge mask in mesh UVs (A probe F), 2 px AA line, palette colours from the aurora (lavender/magenta/blue/pale pink); importance falls with depth. Bindings per B §3.1, subset by `beat.count` hash |
| Water | Real river speed (speedOverride 2.5-3.5 m/s), flowSpeed 1, longer/faster ADR-1643 streaks, ripple <= 0.012; drum disturbance capped (B) |
| River pulse | `groundPulse` along the river, two instances = B's 2-slot ring (launch rules B §3), summed glow clamped to 1; snare flares live pulses only |
| Celestial sky | Fix the Milky Way params now (bandTilt ~1 rad, density <= 0.03, band 1.2-1.6). Engine S1 (band width/glow/colour as fields) and S2 (analytic planet disc) both approved, off by default, byte-identical tests, ADR 1661-1669. Four variants as project presets; the current sky stays the default unless the comparison clearly wins |
| Comets | Existing `comet` effect, one instance, `signal.cometCue` (B §3.5: ~5 per song, 8 s min gap, one alive), environment response via routes + a slower `groundPulse` |
| Snare attack | Drop the 40 ms attack (approved); bloom, if wanted, in the shader |
| Class-driven responses | Only soft or slow; never a sharp full-frame accent on the class kick (BT.1359) |
| Material op ceiling (48) | Trim the existing program before adding edges/waves/haze |
| Main-thread QoS (CPU p99) | Separate engine branch after this pass (approved), not in this scene branch |

## Gates (brief §11), and where the agent stops
- **Gate A, composition: STOP for owner review** — before/after from the same representative camera, plus a moving
  clip (~20 s) and a top-down map. Layout has needed owner steering every time; nothing else proceeds until approved.
- Gates B-G then run without stopping: edges, water, pulses, sky experiment (comparison sheet of the 4 variants), comets,
  integration. Each gate: stills/clip + headless Ultra A/B (B's protocol §6), recorded in PROGRESS.md.
- Final stop: report per brief §14, live acceptance on AC + the LG 60 Hz display (check `pmset -g batt` and the display
  first; if not met, say so and do not claim the target), both suites exit 0.

## Budget (headless Ultra p50, from A's 11.5-11.8 ms baseline)
landscape ~0 (A measured 10.6-11.3), edges +0.1, water ~0, pulses +0.1, sky +0.1, comet +1.5 while alive, polish +0.05:
~13 ms all-on, against B's 17 ms cap and the controller's 29.3 ms.
