# ADR-348: A system the application ran and did not keep

Status: accepted
Date: 2026-09-19

## What was wrong

The owner went looking for the day/night controls and they were not there. They were right.

`DayNightSettings` — the cycle length, the phase, the sun's arc, every colour curve, the bindings —
**registered no parameters at all.** `composition.cpp` parsed `environment.dayNight` with a local
reader lambda, filled the struct, and stopped. No UI, no modulation, no keyframing, no timeline
automation, no scrub. The cycle ran perfectly and was reachable only by hand-editing scene JSON.

**And it was worse than that: there was no writer either.** The block was read at load and never
emitted at save, so a scene that carried a cycle and was saved **lost the entire block** — curves,
bindings, cycle length. That is not a missing panel row, it is data loss, and it is the same defect
as the 94 orphaned `atmos/*` parameters this project has already been bitten by once.

ADR-225 states the rule: *a setting the application does not keep is not a setting.* I wrote this
system and broke that rule in the most complete way available.

## What is registered, and what deliberately is not

The owner's instruction on the curves was explicit: *"Dont expose sky color then if it's too much
work just whatever user Params make sense."* So the colour curves stay scene-authored. A wall of
per-stop swatches is a control surface nobody reaches for while looking at a scene, and the spec's
own warning applies — a technically complete panel can still be a poor product.

What is registered is the timing and the geometry, which is what "control over its timing" meant:

```
env/dayNight/enabled  paused  dayPhase  cycleSeconds  phaseOffset
env/dayNight/sun/peakElevationDeg  azimuthAtDawnDeg  azimuthSweepDeg
env/dayNight/sun/intensityScale  moon/intensityScale  stars/brightnessScale
env/dayNight/hdri/intensityScale  glow/influence  fog/horizonBlend
```

`dayPhase` is normalised 0–1 and is the one that matters: sliding it walks the whole environment
through the cycle. The sun angles are in **degrees**, converted at the panel edge — `0.78 rad` is
an inscrutable knob and `45°` is not. The `*Scale` fields already existed as the
multiplier-over-the-authored-curve control and were simply unreachable too.

## The sweep, which found more

Checking whether this was one oversight or a pattern. It was a pattern.

| setting | before |
|---|---|
| `env/skybox` (`showSkybox`) | unreachable |
| `env/proceduralSkyBackground` | unreachable |
| `env/lightFromEnvironment` | unreachable |
| `env/skyBloom` | unreachable |
| **`WaterSettings`** | **6 of 23 fields reachable** |

The water is the significant one. ADR-099 chose six properties as "the ones worth moving" — glow,
sparkle, ripple, flowSpeed, swell, foam. The water-world spec asks for a different nine, and **none
of them were reachable**: `clarity`, `maxOpacity`, `fresnel`, `reflection`, `roughness`,
`refraction`, `rippleScale`, `shallow`, `shallowColor`, `deepColor`. A scene could not change how
clear its water was, what colour it went with depth, or how much sky it reflected, without editing
JSON. All ten are now parameters; ADR-099's six are untouched.

`clarity` and the spec's "Absorption" are the same idea from two ends — metres of water the bed
stays visible through — so only clarity is exposed rather than shipping two controls that fight.

## Consequences

The scene writer now emits `dayNight`, including the curves as keyframe lists, and the reader
parses them back. Both halves are needed: writing a block the reader ignores is the same defect one
layer along, and `applyDefaults` would have quietly restored the defaults over an author's edited
stop. `fill` only writes into an empty list, so anything parsed survives it.

The tests are the two that would have caught the original: one asserts every path is registered and
carries the control that an unregistered path is *not* found; the other saves, reloads, and saves
again, and asserts a non-default colour stop survives both round trips. Neither needs a GPU or a
frame — which is the uncomfortable part, because nothing was stopping them from existing.
