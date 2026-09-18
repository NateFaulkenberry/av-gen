# ADR-341: One dayPhase, and the sky you cannot have

Status: accepted
Date: 2026-09-18
Supersedes ADR-340's "the HDRI is never the visible sky", by the owner's decision. See §4.

## Context

The Tree of Life island had to gain an ocean and a day/night cycle, with one authoritative time of
day driving the sky, sun, moonlight, stars, atmosphere, water, environment maps and the tree's
bioluminescence — so that the world reads as one thing changing state rather than eight animations
running at once.

## Decisions

### 1. `dayPhase` is a pure function of the clock

`scene::DayNight` (`src/scene/day_night.{hpp,cpp}`). `phaseAt` is
`fract(seconds / cycleSeconds + offset)`; there is no accumulator anywhere. This follows the
`LfoSource::update` precedent (`renderTime * rate + phase`) and it is what makes scrub equal play,
an offline frame N equal a live second N, and 5,000 cycles later land on the same phase to 1e-4.
`resolveDayNight` is likewise pure in phase.

Wrapping is by construction, not by care: every curve is a **circular** keyframe list, so 0.99 →
0.01 is a 0.02 step like any other. `tests/unit/test_day_night.cpp` asserts the seam step is no
larger than the worst 0.02 step elsewhere in the cycle, and carries the ADR-182 control that the
other assertions need — midnight and noon must differ by a lot, because every other check in that
case would pass on a constant environment. 110 assertions, green.

Bindings are **named in the scene** (`sunLight`, `moonLight`, `starNodes`, `glowNodes`, `dayMap`,
`nightMap`) rather than discovered. A system that looked for a light called "sun" would work here
and quietly do nothing on the next world.

### 2. The ocean is a flooded terrain, and two geometric facts shaped it

Water in this engine lives on a terrain node (`docs/water.md`), so the ocean is a terrain whose
`seaLevel` is 90 units above its bed. That reuses the existing ~1,800-line water system rather than
building a second one, as the brief requires. 256 chunks, all wet, 32,768 triangles, 2 ms.

* **A finite plane cannot reach the horizon.** For its far edge to fall inside one pixel at this
  camera it would have to be ~218,000 units out. At 3,000 units the edge sat 4.2° below the
  horizon and the frame had **two horizons** — the exact failure this was warned about. At 20,000
  it is 0.63°, and fog closes the rest. Verified in a frame, not calculated and assumed.
* **The horizon of a flat sea is at the camera's eye height, always.** Lowering the water does not
  move it; only the camera does. So the framing was solved rather than nudged: camera
  `(185, 14, 178)` puts the island's lowest rock at −13.7°, the crown at +15.4° and the horizon at
  +1.3°, all inside the ±18° half-fov, and the island hangs *against* the sea instead of appearing
  to rest on it.

### 3. Two `skyIntensity`s, and the one that matters

ADR-049 split `Environment::skyIntensity` (the **visible background pass**) from
`SkySettings::intensity` (the sky **as an IBL source**). The cycle was driving only the second.
Midnight rendered at 102/110/125 sRGB — a daylight sky at midnight — which would have quietly cost
the night the Glowmere contrast the whole design depends on. Driving the first gives 31/36/47.

A related trap, caught only because a red test colour was rendered: the scene had inherited
`"skybox": false` from the art pass, and `showSkybox` gates the entire sky pass. For one round of
renders neither the procedural sky *nor* the HDRI was drawn and the flat `background` colour was
painting the sky — while every number still looked plausible.

### 4. The HDRI is the visible sky, and the procedural sky colours are therefore inert

The owner chose to use the night HDRI's moon as *the* moon, which requires the map to be visible.
This supersedes ADR-340's "neither HDRI is ever drawn as the visible sky".

**The engine ties the IBL source to the skybox.** `drawSky = ibl && (!ibl_.fromSky || sky.showBackground)`:
with a map bound, `ibl_.fromSky` is false and the skybox drawn is the map's cube. There is no
configuration that gives HDRI *lighting* with a procedural *visible sky* — `showSkybox: false`
draws nothing at all. So:

* The visible sky is the HDRI. The day map's clouds and the night map's moon and Milky Way are the
  sky, and the double-moon question is settled the other way from ADR-340: there is one moon, and
  it is the map's, because no procedural moon was built.
* **`zenithColor`, `horizonColor` and `groundColor` are consequently inert in this scene.** They
  remain in `DayNightSettings` and still drive any scene with no map bound, but here the sky's
  colour journey comes from the map swap, `skyIntensity`, fog, water and light colour. That is a
  real reduction against the brief's §19 and it is not hidden: the curves exist, are tested, and
  do nothing here.
* **The map swap is a swap, not a blend.** The renderer holds one environment cube. `hdriBlend` is
  shaped to cross 0.5 where `hdriIntensity` is at its lowest, so the change lands where it is least
  visible; the test asserts exactly two crossings per cycle and that the contribution at each is
  under 45% of the noon peak. A true crossfade needs two IBL chains and a blend in shading, which
  is renderer work this pass did not do.

## What the cycle measures

Eight phases, one camera, island held still, eight distinct hashes:

| | midnight | dawn | noon | sunset | twilight | night |
|---|---|---|---|---|---|---|
| sky | 31 36 47 | 132 148 166 | 158 173 190 | 135 151 169 | 98 114 133 | 33 38 49 |
| water | 29 37 62 | 123 96 98 | 128 143 161 | 134 94 96 | 79 58 91 | 31 37 64 |
| island | 9.3 | 101.7 | 98.5 | 105.2 | 54.2 | 10.2 |

The canopy's blue/red ratio is **2.08 at midnight and 1.15 at noon** — the Glowmere becoming
dominant at night, as a number rather than an impression.

## Consequences

The sky-colour curves are dead weight in this scene until either a two-map blend or an
IBL/skybox decoupling exists. Both are renderer changes; the second is the smaller one and would
restore the brief's §19 in full.
