# ADR-354: A shadow-casting key that cast nothing, and the range that is the same number for every scene

Status: accepted
Date: 2026-09-19
Extends ADR-034 (cascaded shadow maps), ADR-112 (the shadowed range and the texel target),
ADR-255/258 (the shadow AOV), ADR-264 (a project's parameters outrank its scene),
ADR-278 (a scene file authors a light), ADR-338/339 (the Tree of Life's five emissive layers).

*Numbered 354 because 348–353 were taken on other branches while this one was in flight.*

## Context

The owner asked for a dedicated cinematic lighting pass on `examples/treeisland/`'s cosmic Tree of
Life, and said the result was not dramatic enough. The scene already had a three-light rig with
roughly the right shape: a warm directional key at (1.0, 0.92, 0.78) and intensity 2.45, elevation
53.7 degrees, `castsShadow: true`, `shadowStrength: 0.85`, `softness: 3.5`, plus a rim and a fill.

So the interesting question was not "where do the lights go". It was why a rig that reads correctly
on paper did not read at all in a frame.

## Decisions

### 1. The key cast nothing, and the shadow AOV says so in one number

`--aov shadow` on the shipping scene returns, over all 1920x1080 pixels:

```
R  min=1.0000 max=1.0000 mean=1.0000
```

The key light's directional visibility is the constant 1.0 everywhere. Not "mostly lit". Not
"shadows too soft to see". **Exactly the constant.**

That constant has a name in this repository already. `--aov shadow` *refuses* to write itself for a
scene whose directional light has `castsShadow` off, on the grounds that "the shadow-map term of a
scene lit only by point, spot or area lights — or by a directional light with castsShadow off — is
the constant 1.0, and a constant that looks like a render is worse than a refusal" (ADR-242,
ADR-255). The deliverable was producing that constant with shadows **on**, and the AOV wrote it out
without complaint because the configuration passed the check.

The cause is ADR-112. The shadowed depth range is the smaller of two rules, and the second one —
the range at which the coarsest cascade's texel is still under `shadowTexelTarget` — is

```
resolvable = shadowTexelTarget * kShadowRangeReference / 2.12
           = 0.08 * 2048 / 2.12
           = 77.28 metres
```

`shadowTexelTarget` is a constant and `kShadowRangeReference` is deliberately fixed at 2048 so the
range does not move between a preview and a final. **Neither depends on the scene.** So 77.28 m is
the shadowed range of every scene in this repository whose world is bigger than about 26 m, and
`tests/unit/test_cosmic_key.cpp` asserts exactly that by computing it at scene radii of 5, 300 and
2737 and checking the last two are equal.

The Tree of Life is a monument photographed from 219.3 m away. Its nearest geometry is at about
130 m and its far edge at about 310. Every fragment in the picture sits past the last cascade,
where `shadowLookup` reports the lookup invalid and `shadowVisibility` returns 1.0.

This is not a bug in ADR-112 — the ADR is explicit that the rule exists to make "a wide scene of
small objects cast anything", and on a landscape walked through at human scale it does. It is a
rule with no way for a scene to disagree with it.

### 2. `environment.shadowRange`, and ADR-112's own words are the argument for it

A scene may now say how far its directional shadows reach, in metres. Zero — the default, and what
every scene that does not set it passes — leaves the automatic rule in charge, so no other picture
moves. It is clamped to the camera's own planes in `ShadowRenderer::update`, because a range past
the far plane fits cascades to a frustum the camera does not have and reads as the shadows
vanishing all over again.

It belongs to the scene for the reason `shadowCascades` does, and ADR-112 says so itself:

> Where the shadows stop is composition, and it must not move between a preview and a final.

The second half of that sentence is why the reference resolution stays fixed and why this is a
scene setting rather than a quality tier. The first half is why the scene is allowed to say it.

`examples/treeisland/tree-of-life-floating-island` sets 420, which covers the subject's depth span
with the 18% range-fade clear of it. The cost is texel size: the coarsest cascade goes from a
theoretical 8 cm to about 41 cm at the 2048 reference, so twig shadows are gone and bough and
canopy-mass shadows are what the frame gets. That is the trade this setting exists to let a scene
make, and a scene that sets it should set it to about the depth its subject spans and no further.

**Twenty-four other scenes stand further from their subject than 77 m** (see the report). Most are
landscapes whose near ground is inside the range and for which the rule is behaving as designed.
The ones in the same position as this scene — a single subject in a void, beyond the range — are
`examples/assetlod/tree-*` (219 m to 4,387 m), `examples/labs/lod-geometry-lab.scene.json` (640 m)
and `examples/treeisland/tree-of-life-ocean-world.scene.json` (257 m). They are not changed here;
they belong to branches in flight.

### 3. An authored light gets live parameters, and its angles are world angles

`lights/<name>/{enabled,intensity,color,azimuth,elevation,angularSize,shadowStrength}`, registered
for every light a scene file authors (ADR-278), defaulted to that light's own authored values — so
attaching them changes no frame until one is turned.

Azimuth and elevation describe **where the light comes from**: elevation above the horizon, azimuth
clockwise from +Z through +X, both in world space. Deliberately not `LightRig`'s camera-relative
frame. A `LightRig` light is placed relative to the subject and the camera so that one rig lights
any world; an authored light is a property of the world, and the brief's §15 names a sun that
swings round as the camera orbits as a failure. The two frames are both right and they are not
interchangeable, which is why this does not reuse the rig's.

`angularSize` is the apparent **diameter** of the source in degrees and drives
`PunctualLight::softness` one for one. That is a calibration, not a physical solid angle, and the
limits are worth stating: the renderer's penumbra is a PCSS filter width in shadow-map *texels*,
`shaders/shadows.wgsl` clamps the blocker search at `softness * 6` texels capped at 24, and the
filter radius is capped at 24 too. So the picture stops changing somewhere above four degrees, and
an "angular size" of 10 and one of 5 are the same shadow. Read it as "the sun is about a half, a
soft celestial source is two to four", and do not read it as a solid angle.

### 4. A shader layer's parameters could not be set by the project that declares it

`ShaderLayerSet::registerInputs` adds one parameter per ISF input under `shader/<layer>/<input>`.
`Engine::loadProjectDocument` calls `params::loadProject` at line 1675 and
`shaderLayers_.fromJson` at line 1701. So every `"shader/..."` a project wrote was met with
*"references unknown parameter; ignored"* and the layer drew its file defaults.

Not hypothetical, and invisible for the obvious reason: the Tree of Life's cosmos layer has eleven
inputs and no project in the repository has ever set one, because until now writing one did
nothing. Nobody could author the thing that would have shown it.

Fixed as a second, narrow pass over the `shader/` prefix after the layers load, rather than by
reordering `loadProject` — which also installs sources, routes and presets, and whose blast radius
is every project. This pass touches exactly the paths that could not previously be set, so no
existing picture moves.

### 5. The emission was what flattened the tree, and lowering it is what §12 asks for

The brief says, in bold, not to solve the lighting problem by increasing the Tree of Life's
emission. The `_ck-ctl-noglow` control — every `emissiveBoost` at 0 — answers the question the
other way round, and answers it in a frame: with emission off the canopy has warm highlights on the
leaf faces towards the key, dark green underneath and a readable terminator on the trunk. With
ADR-338's shipping values it has none of that.

Measured, on the same camera: key-to-shadow luminance ratio **18.5** with emission off and **9.3**
with it on. The emission is costing a factor of two in the contrast the brief is asking for.

So the four channels come down — foliage 2.2 to 0.5, twigs 1.5 to 0.55, tracery 5.0 to 2.4, lumens
4.0 to 2.4 — which is "complement rather than flatten" and is the opposite of the lever §12
forbids. ADR-338's values are not discarded: they ship as the **Glowmere Bloom** preset in the same
project, one click away, because the owner may want the bioluminescence back and this is an art
direction decision they are entitled to reverse with four numbers.

### 6. There is no light shaft to be had, so no beam knobs are registered

`shaders/volume.wgsl` says it in a comment:

> There is no shadowing in the fog: a beam is the falloff of a local emitter, not an occluded
> shaft.

and its `lightRadiance` returns a **directional** light's colour with no attenuation and no shadow
lookup at all. A directional key in the volumetric therefore in-scatters uniformly through the
whole marched volume: a flat haze over the frame, not a crepuscular ray. §13's "visible primarily
because of environmental particles, strongest near the illuminated environment, naturally fading
through space" is not reachable without teaching the march to sample the shadow atlas, which is a
renderer change in a scene that already renders at 0.1 fps offline.

So the volumetric beam is **not built**, and §18's four Volumetric Beam parameters are **not
registered**. A knob that does nothing is the same defect as a system with no knobs, one layer
along, and this project shipped four unreachable capabilities in a single day.

## The measurement

Two cameras, the before taken from 1e9f1b74 at its own key angles (azimuth -39.4, elevation 52.8 --
which is what `direction: [0.387, -0.806, -0.472]` works out to), the after at its own (-50, 35).
`tools/light_probe.py` partitions subject pixels by the sign of n.L against the key, out of the
normal AOV, so the partition is made of the thing under test and moves when the key moves.

|                           | before | after  | no-key control |
|---------------------------|--------|--------|----------------|
| wide  `shadowed_frac`     | 0.0000 | 0.5099 | n/a            |
| wide  `key_to_shadow`     | 2.394  | 12.760 | **0.586**      |
| wide  `shadow_floor`      | 0.1798 | 0.0534 | 0.0550         |
| wide  `rms_contrast`      | 0.0846 | 0.1134 |                |
| wide  `p99`               | 0.4393 | 0.6749 |                |
| under `shadowed_frac`     | 0.0000 | 0.5838 |                |
| under `key_to_shadow`     | 2.535  | 25.112 |                |
| under `shadow_floor`      | 0.2588 | 0.0267 |                |

The no-key arm does not merely fall short of the band: the ratio **inverts**, because with the key
off the surfaces facing where it was are the ones facing away from the fill. Key-facing mean
luminance goes 0.27113 to 0.01047, a factor of 25.9.

Two more controls, on the after:

* `_ck-ctl-noshadow` — the key lit, `castsShadow` off. Key-facing mean 0.31702 against 0.26894 with
  shadows on: **14.5% of the light on key-facing surfaces is occluded by the scene's own geometry**,
  which is the self-shadowing and branch layering §14 asks for, as a number.
* `_ck-ctl-nofill` — `shadow_floor` 0.0534 to 0.1544. The fill is measurably what keeps the shadow
  side readable, which is §6.

### Cost

1920x1080, tier high, engine GPU timestamps, minima over samples (ADR-170), n=10 and n=4:

| | shadow phase | shadow draws | GPU frame |
|---|---|---|---|
| shipping (`shadowRange` 420) | 23.20 ms | 74 | 49.74 ms |
| control (`shadowRange` 0)    | 22.09 ms | 72 | — |

**+1.11 ms, 5%.** The caster list does not depend on where the cascades are fitted, so before this
branch the renderer was already spending twenty-two milliseconds a frame — 44% of its GPU time —
rasterising 3,203,880 triangles into shadow maps that every fragment in the picture looked up
outside of. The change did not add the cost; it made the cost buy something.

Both numbers are **contended** and are an attribution rather than a benchmark: another agent's test
binary held 99.5% of a core through the window and `CrashPlanService` held 123.9%. The phase split
survives that in a way the wall clock does not, which is why the shadow-against-scene ratio is
quoted and the absolute frame time is not.

Unrelated, and flagged rather than investigated: the same log line reports `cpu(scene)=1818.14 ms`.
49.74 ms of GPU work is 20 fps; the owner reports 5. If that CPU number is per frame it is the whole
of the gap and it is not in this renderer.

## Consequences

* A scene can make the shadowed range a compositional decision. Nothing that does not ask, moves.
* `--aov shadow` is worth running on any scene where a shadow looks absent: the difference between
  "soft" and "the constant 1.0" is one `min` away and invisible in the beauty pass.
* A control has to be authored in the layer that wins. `_ck-ctl-nokey` first disabled the key in
  the scene, was overruled by the project parameter this ADR added, and returned statistics
  identical to its arm to five decimal places — which is what a control that did not fire looks
  like, and it looks exactly like an arm that does nothing.
