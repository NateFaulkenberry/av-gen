# Aurora Borealis pass 5: research on a realistic fireball

The owner's request (pass 5): "make the fireball look more like a realistic fireball - deep research on how to do this".
These notes summarise how real-time and VFX artists build convincing fire, what was tried, and what was chosen.
The engine change is ADR-1665.

## What makes fire read as fire

- **Colour is temperature.**
  - A flame's yellow and orange light is incandescence: soot particles glowing from heat. Blue light comes from
    combustion chemistry, not heat ([Luminous flame, Wikipedia](https://en.wikipedia.org/wiki/Luminous_flame);
    [What determines the colour of flames, ScienceABC](https://www.scienceabc.com/pure-sciences/what-determines-the-color-of-flames.html)).
  - So the colour follows a blackbody curve:
    - white-yellow where it is hottest;
    - through orange;
    - to deep red;
    - then soot that has cooled below glowing, which only absorbs light. This is the dark, smoky edge real
      fireballs have.
  - Engines expose this curve directly:
    - Blender's Blackbody node ([manual](https://docs.blender.org/manual/ko/latest/render/shader_nodes/converter/blackbody.html));
    - Unreal's colour-temperature node ([docs](https://docs.unrealengine.com/4.27/en-US/BlueprintAPI/Math/Color/LinearColorSetTemperature/index.html));
    - Unity's `CorrelatedColorTemperatureToRGB` ([docs](https://docs.unity3d.com/ru/ScriptReference/Mathf.CorrelatedColorTemperatureToRGB.html));
    - pbrt's blackbody, ported in the [`blackbody` crate](https://docs.rs/blackbody).
  - The radiance also rises steeply with temperature (Stefan-Boltzmann's T^4), so the core outshines the rim.
- **Shape is turbulence.** Layered noise alone looks uniform. Domain warping, which samples `fbm(p + fbm(p))`, bends the
  field into swirls, filaments and tongues
  ([GameDev.net shader lab: domain-warped noise](https://gamedev.net/shaderlab/23-domain-warped-noise/);
  [Wayline: a living fire](https://www.wayline.io/learn/shaders/19), which builds its fire from fbm, a two-fbm warp and
  a dark-red, orange and yellow ramp).
  - Scrolling the warp field with time makes it boil.
  - Advecting it back along the flight sweeps the tongues behind the ball.
- **Real-time fire is volumetric noise or flipbooks.**
  - Procedural volumetric fire, with noise evaluated in the pixel shader, has run in real time since 2007
    ([Fuller et al., UC Davis](https://web.cs.ucdavis.edu/~hamann/FullerKrishnanMahrousHamannJoy2006Final03102006.pdf)).
  - Shipped games mostly use flipbooks simulated in EmberGen or Houdini. Artists render emissive and albedo separately,
    so that colour and intensity stay controllable
    ([Justin Cherry, Back 4 Blood](https://realtimevfx.com/t/justin-cherry-realtime-vfx-reels-2021-back-4-blood/18495);
    [Unity's free temperature-pass flipbooks](https://unity.com/blog/engine-platform/free-vfx-image-sequences-flipbooks)).
  - Flipbooks loop visibly ([Vanzine thesis](https://crhc.iusb.edu/math-compsci/_prior-thesis/YVanzine_thesis.pdf)).
  - Hybrid setups add particle smoke, embers and sparks around the flipbook
    ([80.lv flame burst](https://80.lv/articles/creating-a-flame-burst-effect-using-unreal-engine-5)).
  - Fully procedural shader fire is common for stylised looks
    ([80.lv procedural fire](https://80.lv/articles/check-out-this-fully-procedural-stylized-fire-vfx-made-with-shaders/)).
- **Volumetric-lite** means raymarching a short emission-absorption integral through a noise density inside a bounding
  sphere: emission where it is hot, absorption where it is sooty
  ([raymarched volumes, Maxime Heckel](https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/);
  [Godot port of a Quilez volumetric flame](https://godotshaders.com/shader/volumetric-plasma-flame/)).
- **Heat shimmer** is a screen-space refraction: the rendered frame is sampled at UVs offset by scrolling noise,
  masked to the hot region, and drawn after the opaque pass
  ([GameDev.net heat haze](https://gamedev.net/shaderlab/38-heat-haze-distortion/);
  [Bugnet heat haze](https://bugnet.io/blog/how-to-fix-screen-space-heat-haze-distortion-not-visible)).
- **Trailing smoke and embers** sell a projectile fireball
  ([JangaFX realistic fireballs series](https://jangafx.com/?p=200); the
  [SideFX fireball explosion](https://www.sidefx.com/contentlibrary/fireball-explosion/)).

## What fits this scene

- **No textures.** The scene is procedural and asset-free, and flipbooks would need baked sequences that loop. So the
  look is procedural noise in the shader.
- **The pass-4 ball shows on every surface and the sky** through one shared function, `waveBallGlow`. Any look has to
  be a function of the ray, so it hides behind what it flies behind and shows against the sky.
- **Smoke can dim only the sky.** It is an absorption, and only the sky composite (`atmosphereSkyAt`) can take a
  transmittance narrowly. The balls fly above the land, against the sky and aurora, which is where the dark edge
  matters.
- **Heat shimmer is out of scope.** It needs a post-opaque distortion pass, which is not narrow, and against a
  near-black night sky it would hardly read.

## Tried

Every variant was rendered on the 45.6, 47.0 and 47.7 s crash group, from the same build.

| Variant | What | Result |
|---|---|---|
| v0 | pass 4's glow (ADR-1663) | a lamp: a round white-gold core in a soft orange halo |
| v1 | look 1, the flame billboard | ragged tongues, orange round a white heart, a sooty fringe: reads as fire |
| v2 | look 2, volumetric-lite, 10 steps | a hot, billowing body with a dark puff of soot behind it; softer, less defined at this size |
| v3 | look 2, more smoke, embers, a longer trail | v2 with sparks and a darker trail |
| **v4 (chosen)** | look 1, larger (85 m), smoke 0.7, embers 0.8, 220 m trail | the most convincing fire at the sizes it flies at |

Two artifacts were found and fixed on the way. Both had the same cause, and both looks shared one of them:

- **The billboard drew radial rays.** Its noise scrolled in the screen plane, and where the flight heads straight away
  from the camera that scroll direction spins with the view.
- **The trail fanned and drew crescents** when seen end-on. It was keyed to the ray's offset from the trail's axis.

The noise is now keyed in the ball's own 3D frame, and the trail is a string of puffs.

## Chosen: look 1

The flame billboard reads as fire because of three things:

- turbulent tongues from domain-warped fbm, boiling and swept back;
- a blackbody ramp whose white-hot heart sits inside orange, red and dark soot;
- smoke and embers trailing behind.

It stays within the flash gate (0.0485 on the crash group) and keeps lighting the land and water below. That light
code (`waveBallLight`) is unchanged.
