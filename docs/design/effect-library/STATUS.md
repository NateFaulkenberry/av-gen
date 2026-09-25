# Effect Library: status

**Paused, 2026-09-25, after Wave 3, at the owner's request** (the effort moves to a project build-out on the
current stack). The longer handoff, with the full tables, is `roadmap.md` § "Status".

## Shipped (in main)

| Wave | ADR | Kinds |
|---|---|---|
| instance model | 702 | 0–7: Comet, Aurora, Vortex, Meteor Shower, Volumetric Fog, Tornado, Ground Pulse, Travel Beam |
| 1 | 703 | 8–13: Glow, Pulse, Bloom Source, Trail, Space Warp, Particle Emitter |
| 2 | 716 | 14–32: Orbit, Spiral, Float, Shake, Bounce, Shockwave, Ripple, Dissolve, Growth, Breathing, Organic Pulsation, Bioluminescence, Pulsing Veins, Fresnel, Rim Light, Color Cycling, Velocity Distortion, Motion Smear, Stars |
| 3 | 719 | 33–47: Lightning, Arc, Electric Field, Discharge, Plasma, Energy Shield, Force Field, Charge-Up, Heat Shimmer, Gravitational Lens, Light Beam, Halo, Bubble, Portal (no Remote View), Reality Tear |

The next free kind number is 48 (`src/world/effects/effect_kind.hpp`; numbers are explicit and append-only).

## Partly built

**Nothing is partly built.** All four Wave 3 slices finished and merged, and there is no WIP branch. The known
gaps inside shipped types are listed in each wave's ADR under "Findings not fixed here":
- SIGNALS has no path for an effect to publish a signal (Charge-Up's `fx.<id>.charge`).
- LIGHTMOD has no modulate half (Pulse on a Light owner; a spot-shaped Light Beam pool light).
- XFORM has no Light or Camera owners.
- Triggers have no panel editor.

## Still to do

- **Wave 4:** FXPOST and REDRAW.
- **Wave 5:** TEMPORAL, the MEDIUM shadowed march, and LOCALTIME.
- **Wave 6:** migrations and heavy geometry.

`roadmap.md` has each wave's full effect list.

## Open look notes (not ruled on by the owner)

- **Wave 3:**
  - Light Beam reads as haze, not a crisp beam;
  - the blue Plasma close-up has subtle strands;
  - the Heat Shimmer Campfire preset is subtle;
  - the Gravitational Lens Einstein ring is faint on a smooth sky, and its horizon darkens fog in front of it;
  - Reality Tear's Crystal Crack shear barely shows;
  - Discharge sparks are ribbon streaks, not EMIT;
  - Electric Field sits on the owner's bounds, not its mesh.
- **Wave 2:** the alpha-mask `discard` ordering in `pbr_shade` is recorded, not moved.
- **Resolved:** Energy Blast and Explosion are thinned (owner: yes); the Tree of Life routes are the approved look (ADR-712).
- **Seek:** the UFO-stack film's play = scrub test stays hidden (`[.known-defect][adr700]`, about 0.018 m).
- **Fog:** the owner's rulings are recorded in ADR-717 and ADR-718.

## How to resume

1. Read ADR-702 → 703 → 716 → 719, then `shared-infrastructure.md` (FXPOST and REDRAW for Wave 4).
2. Reserve kind numbers per slice.
3. Give each slice a file set it owns.
4. Gate every primitive byte-identical against the pre-change binary.
5. Look at on/off sheets of every type in the Glowmere film and the UFO stack, not only in a fixture.

Review sheets are in `~/Desktop/av-gen-review/5-effect-library-wave1`, `10-effect-library-wave2` and `16-effect-library-wave3`.
