# ADR-344: Lighting and background stop being one choice

Status: accepted
Date: 2026-09-18
Completes the limitation recorded in ADR-343 §4.

## Context

The skybox was always whatever the IBL had been built from:

```cpp
const bool drawSky = ibl && (!ibl_.fromSky || scene.environment.sky.showBackground);
```

With an environment map bound, `ibl_.fromSky` is false and the background drawn is that map's cube.
There was no configuration in which an HDRI lit the scene and something else stood behind it.

The cost landed on the day/night cycle. ADR-343 gave the Tree of Life world `zenithColor`,
`horizonColor` and `groundColor` curves; the scene is lit by an HDRI; so those curves were
computed, tested, and had **no effect on any frame**. Not broken — unreachable, which is worse,
because everything still looked plausible and the sky simply had two states (day map, night map)
where the brief's §19 asked for a journey.

## Decision

Evaluate the analytic sky directly in the background pass, opt-in.

`shaders/environment.wgsl` already had the maths — `fn skyRadiance(dir, minRadius)` — but it reads
the environment *processor's* uniform block, which the scene pass cannot see. So:

* **Four vec4s appended to the END of `FrameUniforms`**: zenith + haze width, horizon + sun angular
  radius, ground + sun glow width, sun colour + the enable flag. Appended rather than inserted
  because seventeen offsets are asserted in that header and inserting moves all of them; the
  codebase already uses the append pattern (`atmosCount` is appended after the world effects "for
  the same reason those were appended after `lights` -- no offset above moves"). The size
  `static_assert` is written as a sum precisely to catch the WGSL side drifting, and it did its job.
* **`skyRadiance` copied into `skybox.wgsl`** reading `frame.*`. Deliberately the same maths: the
  sky the camera sees and the sky the IBL was built from have to be the same sky, and the way that
  goes wrong is two implementations drifting. The one difference is the trailing intensity
  multiply, left to the caller because the background pass already applies `skyExtra.z`.
* **`Environment::proceduralSkyBackground`**, default false, scene key of the same name. No
  existing world moves.

**The decision is now a predicate, not an expression.** `scene::skyBackgroundFor(env, haveIbl,
iblFromSky)` returns `FlatColour`, `Analytic` or `IblCube`, and lives in `scene_types.hpp` beside
the struct it reads. Inline in a render pass the only way to learn what that logic did was to
render — and a render is exactly what was not catching this.

## What it measures

The sky across the eight showcase phases, before and after. Before, it had two values because it
was the HDRI:

| | midnight | dawn | noon | sunset | twilight |
|---|---|---|---|---|---|
| sky, sRGB | 0 1 4 | **137 95 90** | 131 146 166 | **150 104 102** | **52 30 60** |

Warm pink at dawn, warm orange at sunset, violet at twilight. That is §19's journey, and it was
inert an hour ago.

The test's control is the one that fails on the old coupling: the same environment with the flag
off must still return `IblCube`. An exhaustive sweep asserts all sixteen input combinations land on
exactly one of three answers, with all three occurring — a predicate returning one constant would
satisfy every other assertion in the file.

## Consequences

**The analytic sky has no clouds.** The HDRI's cumulus are gone from any scene that turns this on.
That is the trade: a sky whose colours move, or a sky with weather in it. The Tree of Life world
takes the first because the cycle is the point; a scene that wants the clouds simply leaves the
flag off and is byte-identical to before.

**Reflections still sample the cube, not the analytic sky.** The water therefore mirrors the HDRI
while the visible sky is procedural, and they disagree at dawn and sunset — ADR-346 works around
the worst of it by turning the water's reflection down at the warm ends and carrying the colour in
the water's body, which closes the gap from R−B −33 to −6. Closing the rest means feeding the
analytic sky into reflections, which is a larger change and is not this one.
