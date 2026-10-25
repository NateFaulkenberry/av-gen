# ADR-1666: A splash throws light, and a path wave's light reaches deeper under the water

- **Status:** Accepted (scene/aurora-borealis).
- **Extends:** ADR-1662 (path waves) and ADR-1663 (the splash, and the land term's light under the water). It uses the
  path wave's two free lanes, `WaveGpu::sparkle.x` and `sparkle.w`, so no struct grows.
- **Asked for by** the Aurora Borealis owner's pass-9 notes:
  - "we lost the big splash look ... as if someone is dropping a big ball into the water"
  - "the under water balls beams of light should illuminate under water more"

## Code

- `src/world/wave_effect.{hpp,cpp}`:
  - `PathShape::splashGlow`, the splash's light, packed in `sparkle.x`;
  - `PathShape::bedReach`, in metres, packed in `sparkle.w`;
  - validation for both: finite and not negative.
- `src/world/effects/kinds/wave_rows.hpp`: the two rows, Splash light and Light under water. Each is written only when
  set.
- `shaders/wave_effects.wgsl`:
  - **`wavePathWater`:** a flash at the launch point and a bright ring running out with the splash's rings. It is the
    wave's own colour, scaled by `splash x envelope x splashGlow`, and it is gone in about a second. It is dimmed under
    250 m from the camera, so the ring running toward the lens does not wash the bottom of the frame.
  - **`wavePathLand`:** under the water the light now fades between `bedReach / 2` and `bedReach` metres down, and is
    at least `bedReach` wide. With `bedReach` at 0 it is 40 m, which is ADR-1663's constant: the same expressions,
    `smoothstep(20, 40, ...)` and `max(halo x 2.5, 40)`.

## Tests

- `tests/unit/test_wave_path.cpp` (`[adr1666]`):
  - At 0, a path wave packs and writes exactly as before.
  - Each value packs in its lane and round-trips through JSON.
  - A negative value is refused.
- `tests/rendering/test_wave_path_gpu.cpp` (`[adr1662]`): with no path wave lit, the water is still byte-identical.

## Context

**The splash.** The splash (ADR-1663) only rippled the surface, and pass 4 halved it. That is commit 08c7c9ce:

| | Kick | Snare | Toms |
|---|---|---|---|
| splash, before | 0.07 | 0.05 | 0.06 |
| splash, after | 0.035 | 0.025 | 0.03 |
| near wake, before | 1.6 | 1.3 | 1.3 |
| near wake, after | 0.8 | 0.6 | 0.6 |

The near water had looked permanently churned, and the values were never restored. The water's light also fades in from
150 m to 500 m (`nearFade`), and the pulses launch 160 m out. So the impact itself showed almost nothing: a heavy ball
dropped in the water needs a burst of light at the impact, not only rings.

**The light under the water.** It stopped 40 m down. The riverbed has been 75 m deep mid-river since pass 8, so most of
it was never lit.

## Decision

Two per-wave values, both zero by default.

- **Splash light:** a flash of light at the impact, plus a ring of light that runs out with the splash's rings and
  breaks the mirror as it goes.
- **Reach of the light under the water:** a deeper and wider pool of light on the bed and the fish.

Aurora Borealis sets its values in `look.py` (`SPLASH`, `BED_REACH`).

## Rejected

- **A crown of geometry, or particles, rising at the impact.** The engine draws path waves on the water and the land
  only, and a burst above the water would need a new draw. The light ring, the flash and the ripples breaking the mirror
  carry the impact.
- **The splash light at full strength near the camera.** A first take swung the frame by 0.08-0.096 in 267 ms at 3.4 s,
  where a kick, a snare and the Moog's entrance coincide. The flash gate is 0.05.

## Addendum (pass 10)

- **The splash lands behind the camera.** Aurora Borealis launches the pulses 40 m behind it (`startOffset` -40), so
  the impact and its flash are never seen. To carry the rings well into view, they now run at 300 m/s, were 170, and
  fade over 1.2 s, were 0.8 (`kSplashSpeed`, `kSplashLife`).
- **The wake's ripples are feathered** in over their first 25 m and out over the last 40% of the wake. They stopped
  at hard lines at the front and at the wake's end, straight edges across the ripple field trailing each pulse: the
  owner's "ripple seam".
