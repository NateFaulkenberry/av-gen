# ADR-400: The water reflects a photograph of a different afternoon, and that stays true for now

- Status: Accepted (2026-09-20)
- Builds on ADR-345 (lighting and background stop being one choice — which created this),
  ADR-347 (the sea joins the cycle — the existing workaround, which calls itself one),
  ADR-099 (water), ADR-013 (IBL), ADR-049 (the HDRI sky), ADR-233 (the interactive rebuild
  deferral), ADR-396 (one description of the air — which is why the cheap fix here is the wrong
  one).
- **Outcome: not fixed. The cost is written down and the harness is built.**

## What was measured

Everything below was already *described* in three places — ADR-345 §Consequences, ADR-347's opening
paragraph, and a comment in `src/scene/day_night.cpp` beside the curve that works around it. ADR-385
is the rule that a stated reason is not evidence, so it was measured before it was reasoned about.

`examples/treeisland/tree-of-life-ocean-world.json`, 640×360, `env/dayNight/paused` with the phase
pinned, eight frames per arm so the deferred IBL rebuild has landed. Sky and water read as R−B over
two bands well clear of the horizon, taken on **both** sides of frame so a left-right gradient in
either cancels:

| phase | sky (r, g, b) | sky R−B | water (r, g, b) | water R−B | **gap** |
|---|---|---|---|---|---|
| dawn (0.25) | 136.1, 95.9, 90.6 | **+45.4** | 124.0, 118.8, 137.2 | −13.1 | **+58.5** |
| noon (0.50) | 130.2, 144.5, 164.7 | −34.5 | 151.0, 170.2, 192.9 | −41.9 | **+7.4** |
| sunset (0.75) | 149.6, 103.7, 101.8 | **+47.7** | 130.4, 121.9, 140.0 | −9.6 | **+57.3** |
| twilight (0.82) | 55.2, 35.2, 62.6 | −7.4 | 96.5, 103.0, 131.1 | −34.6 | +27.2 |

**Noon is the control, and it is the reading that makes the rest mean anything.** At noon the
procedural sky and the daylight map agree about what the sky looks like, and the gap collapses to
7.4 — an eighth of the warm ends'. A defect that produced a constant offset between two regions of
the frame would show the same gap at every phase, and this would be a measurement of the two bands
rather than of the day. It is not: the sky's own R−B swings 82 units across the cycle and the
water's swings 32, in the opposite direction at the ends.

So: the sky says sunset and the water still says noon, by about 58 sRGB units of R−B, at both warm
ends of every cycle. It reproduces.

ADR-347's own numbers were water R−B −6.3 at dawn and −2.6 at sunset; this reads −13.1 and −9.6
over a wider band that includes open water further from the island. The mitigation is doing
something real. The **gap to the sky** is what it never closed, and that is what is tabled above.

## Why

`shaders/water.wgsl:337-356` samples `prefilteredMap` — the environment cube — gated only on
`frame.envParams.w > 0.5`, "an IBL is live". `shaders/skybox.wgsl:95-117` has a *three-way* choice
and takes the analytic branch when `frame.skySunRadiance.w >= 0.5`, which is ADR-345's "a map is
lighting the scene and the scene asked for the procedural sky behind it". The water's gate does not
know that flag exists.

And the cube it samples is a **baked photograph**: `tree_of_life_day.exr`. Its dirty key is
`{scene pointer, identity, texture id, textureVersion}` and the sun is not in it — correctly, since
the map's pixels have not changed. The only thing that re-bakes it during a cycle is the day/night
map swap, which crosses twice, in late twilight and pre-dawn. Between those two frames the sun
travels through dawn, noon and sunset with the reflected sky frozen.

## Decision, and the three fixes that were costed

**Not fixed here.** Not because it is expensive — the cheap version is an afternoon — but because
each version is wrong in a way the measurement above does not capture.

### 1. Transliterate the analytic sky into `water.wgsl` (~an afternoon)

Give water the same three-way branch `skybox.wgsl` has; every uniform it needs is already in
`FrameUniforms`. Ten lines and a copy of `skyRadianceFrame`.

**This is ADR-396's defect one system along.** The analytic sky is *already* hand-copied twice —
`environment.wgsl:118-135` and `skybox.wgsl:29-46` — and `skybox.wgsl`'s own header says why that is
dangerous: *"the sky the camera sees and the sky the IBL was built from have to be the same sky, and
the way that goes wrong is two implementations drifting."* This branch spent a day deleting exactly
that pattern from the wind field, where the third copy had already silently lost a field. Adding a
third copy of the sky to buy a colour match is trading a visible defect for the invisible kind.

The honest version of this option is therefore **not ten lines**: it is first extracting the sky the
way `wind_field.wgsl` extracted the wind — a binding-free `skyRadianceFrom(params, dir)` that
`environment.wgsl`, `skybox.wgsl` and `water.wgsl` all call — with the CPU↔GPU parity test that
makes the extraction provable. That is a day, and it is worth doing **on its own merits** whether or
not the water ever uses it.

### 2. Water calls the analytic sky (after 1)

Still wrong, for a reason the shader diff does not show: **this is not water-only.**
`shaders/pbr_shade.wgsl:478-480` samples the same `prefilteredMap`. Every rough and metallic surface
in the world has the identical disagreement; water is only where it is visible, because water is
half the frame and mostly reflection.

Fixing water alone trades *"the sky disagrees with the water"* for *"the water disagrees with every
other reflective surface in the shot"*. Which of those is worse is an art-direction question and not
this agent's.

It also gives water a **sharp** mirror where it has a roughness-blurred one (`lod = envParams.y *
clamp(surface.z * 2.2, 0, 1)`), which invalidates ADR-099's ripple and glint tuning and ADR-347's
measured `waterReflection` curve — both of which are numbers the owner tuned by eye.

### 3. A second prefiltered cube, built from the analytic sky (the correct one)

The whole group-3 then reflects the sky the camera sees, `pbr_shade` included, and options 1 and 2's
objections both vanish. It costs:

- a second `processSky` chain: **10.3 ms** per bake, measured, `docs/performance/surfaces.md:55-72`;
- a second cube, irradiance and prefiltered set (256 / 32 / 128×6 mips) resident;
- a **widened group-3 bind layout**, which touches every shader that binds it — `pbr_shade`,
  `pbr_skinned`, `water`, `skybox`, `sdf_raymarch`, `reference`;
- a per-phase re-bake budget, which is the expensive part and the reason ADR-233's deferral
  machinery exists: a sky drag ran the existing chain on 46% of frames and took `render.record`
  from 0.96 ms median to 26.8 ms mean / 78.8 p95. The entire point here is that the sun is moving,
  so this bake would be on a schedule rather than on an edit.

That is a renderer change with a performance policy attached, and it is not proportionate to four
rows of a table inside one agent's defect list.

## What was built instead

`tests/rendering/test_water_sky_agreement_gpu.cpp`. ADR-347's assessment was that whoever fixes this
builds the first regression harness — **there is no test in the tree that asserts water reflection
content at all; every test that touches it sets `reflection = 0.0f` to remove it as a variable.**
That harness is now built, before the fix rather than after, so the decision above rests on a number
somebody can re-run.

It is deliberately **not a tripwire**. It asserts only what holds whether the defect is present or
gone — that it is reading real sky and real water, and that the sky's R−B swings across the cycle,
which is what would fail if the procedural background silently stopped being drawn — and it *prints*
the table. A test that asserted the current gap would fail the day somebody closed it.

Hidden behind `[.probe]`, so it is run deliberately:

```
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[water][sky][.probe]"
```

## Revisit when

- **Somebody does the sky extraction of option 1's first half on its own merits.** At that point
  option 2 costs ten lines rather than a day, and the remaining question is purely the
  art-direction one: is water agreeing with the sky and disagreeing with the other reflective
  surfaces better than the reverse? Ask the owner; do not decide it in a shader.
- The volume pass gains a shadow-atlas sample (ADR-360 named that trigger), which is the other
  change that wants a per-frame environment budget — the two should be costed together, because
  the re-bake schedule is the expensive half of option 3 and only has to be built once.
