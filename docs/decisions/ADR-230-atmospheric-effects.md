# ADR-230: An atmospheric effect is a view ray, not a surface term

Status: Accepted

## Context / Problem

The brief asks for two large celestial phenomena in Glowmere's sky -- bioluminescent comets crossing
it, and an audio-reactive aurora rising from the horizon -- and asks explicitly that they not be
"a comet shader and an aurora shader" but an extension of the World Effects system into a reusable
atmospheric category.

ADR-207 already answered the general question. A world effect is *a spatial phenomenon with a
source, a propagation geometry, a speed, an appearance, a lifetime and a wish to be modulated*,
resolved on the CPU once per frame into a fixed-size record and evaluated per fragment in the shared
surface shader as an additive term. Two propagation kinds ship because they are the two distance
metrics, and the extension point is the mask.

That model is right for a beam crossing a valley and cannot be stretched to a comet, for one reason
that is not a matter of taste: **the surface shader only runs where there is a surface, and the sky
has none.** A comet, an aurora, a meteor shower and a sheet of lightning are all things you see
*instead of* geometry. None of them can be an additive term on a fragment that was never shaded, and
no mask over `dot(p - origin, axis)` changes that, because there is no `p`.

Three further constraints came from the engine as it is:

* `scene::Camera::farPlane` is 200 m and Glowmere's valley is 640 m across. A comet two kilometres
  away cannot be geometry -- it would be clipped.
* `ObjectUniforms` has no free lane (ADR-135) and the procedural scatter submits 60k instances, so
  anything per-object is unaffordable, exactly as ADR-207 found.
* `Environment::skyBloom` is 0.1 in Glowmere. That is a sensible number for an environment map and a
  catastrophic one for a comet, which *is* a light source.

## Alternatives considered

1. **A new `PropagationKind` on `world::WorldEffect`.** The obvious reading of "extend the existing
   system", and the one the brief's own diagram suggests. Rejected because it does not reach: adding
   `Comet` to an enum evaluated in `pbr_shade.wgsl` produces an effect that is invisible wherever
   there is no geometry, which is the whole of the sky. The shared *model* is worth keeping; the
   shared *evaluation site* is what makes it impossible.
2. **Real geometry: a ribbon mesh for the tail, a billboard for the head.** The engine already has a
   complete, deterministic, camera-facing ribbon in `particles.wgsl` (`vs_ribbon`, ADR-040), and
   transliterating it was seriously considered. Rejected on the far plane: at 200 m the comet would
   have to live inside the valley, at which point it is a prop with parallax wrong by two orders of
   magnitude. Raising the far plane moves every cascade split, the froxel grid and the AO range.
3. **A screen-space pass over the sky.** Cheap to describe and it is what the brief warns against:
   an effect that is "attached to the screen" rather than to the world. It also cannot be occluded by
   terrain without reading depth and reconstructing position, which is strictly more work than
   evaluating the same function where the sky is already being drawn.
4. **Three added lines inside `skybox.wgsl`.** The smallest change, and wrong for four separate
   reasons, each of which alone would justify the extra pipeline -- see the Decision.

## Decision

**An atmospheric effect is authored data, resolved on the CPU once per frame into a fixed-size
GPU-ready record, and evaluated per *view ray* in a direction-space draw at the far plane.**

It shares ADR-207's model and replaces its rendering path.

**What is shared, and shared literally.** `world::Activation`, `world::Timing` and `world::Sparkle`
are reused as-is. The gating rule is now one exported function, `resolveActivationWindow`, that both
families call -- previously it was private to `effects.cpp`, and two copies would have been two
places for "why did my effect not fire" to have different answers. Resolution is a pure function of
the transport second in both families, for the reason ADR-091 gives. Parameters, presets,
serialisation, modulation and the panel all follow ADR-207's shapes.

**What is new is the geometry.** `world::AtmosphericEffect` in `src/world/atmospherics.hpp` carries a
kind, a `Comet` payload, an `Aurora` payload, a `GroundIllumination`, an `Activation` and a `Timing`.
Both payloads are always present rather than living in a variant, because an effect changes kind when
somebody picks a different preset and a variant would silently discard the settings of the kind they
left. It round-trips as an `"atmosphericEffects"` array beside `"worldEffects"`.

**A comet is an analytic ray-vs-curve integral.** Its trajectory is authored in sky coordinates -- an
azimuth and elevation to launch from, one to fly to, and a distance -- and flown as a **great-circle
arc at that distance**, bowed by an optional lift and curvature. The shader marches the tail from the
head backwards and accumulates a Gaussian band whose width grows down the tail. That buys real
parallax (crossing the valley moves it against the stars), immunity from the far plane, and a
trajectory that is a closed-form function of the transport second.

The arc is load-bearing and a straight chord is not a simplification of it but a different, wrong
answer; see Consequences.

**An aurora is K ray-vs-vertical-cylinder solves** at stepped radii. The hit's azimuth and height
index a curtain whose top comes from sixteen spectrum bins, mirrored across the sky so the wrap has
no seam. Stepping the radii gives the layers parallax against each other; putting the base at a world
height below the valley floor is what makes terrain silhouette the curtain feet.

**The transport is `FrameUniforms`**, appended after ADR-207's block so no existing offset moves --
the same argument ADR-207 made for appending after `lights`. The block grows 2320 -> 3776 bytes
against a 64 KB limit.

**The draw is its own pipeline**, recorded immediately after the sky, `depthCompare = LessEqual` with
no depth write at clip z = 1, additive into the HDR target and the emission target. Four reasons, each
sufficient on its own:

1. **It runs when the skybox does not.** `SceneRenderer` only draws the sky when the scene has an IBL
   and has asked for it as a background. A comet should not require a procedural sky to exist.
2. **It blooms at its own weight**, rather than through `Environment::skyBloom`.
3. **It costs nothing when there is nothing.** The draw is skipped entirely when no effect is live,
   which is what makes "effects disabled" a real arm rather than a frame that renders the same pixels
   through one more branch.
4. **It cannot regress the sky.** `skybox.wgsl` keeps the star field, the moon disc and the equirect
   LOD selection ADR-049 had to hand-tune, untouched.

Because the draw is depth-tested against a scene that has already rendered, **terrain silhouettes the
aurora for free**. There is no horizon seam to author because there is no horizon edge: the depth
buffer is the horizon. This is the single largest thing the choice of rendering path bought.

**Ground illumination (§6) is a hemispheric wash plus one moving pool**, summed on the CPU and added
in `atmosphere_ground.wgsl` -- a separate, thirty-line module included by `pbr_shade.wgsl` and by
`water.wgsl`. It is multiplied by albedo, because it is incoming light and not paint, and it is
deliberately absent from the emission target, because illumination is not emission.

## Rationale

The expensive decision in a sky effect is **where the ray comes from**. A ray per fragment of a
fullscreen triangle at the far plane is the only formulation that is simultaneously: free of the far
plane, correctly occluded by geometry that has already written depth, paid for only on visible sky,
and able to carry real world-space parallax. Every other formulation gives up at least one of those.

The per-fragment cost is controlled by rejection rather than by simplification. A comet tests the
view ray against a bounding sphere around its whole trail in three instructions, and most sky pixels
on most frames stop there; only rays that could plausibly hit march the tail. That is why the cap can
be six comets with a 24-sample march rather than one comet with four.

Audio reaches the effects on two established paths, and no third one is invented.

**Scalars go through the modulator.** Every number an aurora or a comet has is an ordinary parameter
under `atmos/<name>/<property>`, and `defaultAtmosphericRoutes` returns the depths and envelopes that
make one answer the music. That is the rule `world/effect_params.hpp` states, and it is what makes
the response editable, curvable and visible in the Modulation panel rather than buried in a shader.

**Vectors come from the frame block, where the engine already publishes them.** The aurora's shader
reads `frame.audio`, `frame.audioBands` and `frame.beat` directly -- which is not a special case but
exactly what ADR-030 put those lanes there for, and the same thing a material program does. What is
authored per effect is the *depth* on each band, and every one of those depths is itself a parameter
a route can drive. The sixteen spectrum bins are the one genuinely new lane, and they are new because
a curtain whose shape is a frequency spectrum needs sixteen numbers across the sky and a scalar route
carries one. They ride in `AtmosphericContext::spectrum`, folded by the engine from the same
`analysis::AnalysisFrame` every other consumer reads -- which is also what keeps them deterministic,
because offline that frame is a precomputed track indexed by the render time.

The distinction that matters is that **no audio is read inside `world::atmospherics`**. Resolution
takes numbers it is handed; it never asks an analyzer anything.

## Consequences

* **A chord is not an arc, and the difference is visible.** The first implementation flew the comet
  along a straight chord between two points on the sky sphere. A chord passes through the interior,
  so the comet dived towards the anchor: authored to launch at 31 degrees and aim at 5, it passed
  overhead at **49** and left the frame it was written for. Slerp at constant distance fixes it, and
  the property that matters is that a camera near the anchor now sees the azimuth and elevation
  interpolate between exactly what was typed. A near-antipodal pair still rises towards the zenith --
  that is what a great circle does, it is asserted in the tests so nobody "fixes" it, and an author
  who wants a low crossing between opposite horizons is asking for two comets.
* **Curtain count is a depth control, not an exposure control.** Four shells summing un-normalised
  made raising the layer count quadruple the radiance, so an artistic choice about depth silently
  became a brightness change and the aurora blew out. The accumulation is normalised by the shell
  count.
* **An aurora needs gaps or it is a fog bank.** The first version's azimuthal noise never reached
  zero, so every bearing was lit and the lower sky was a solid wash. The dark between curtains is the
  silhouette, and the silhouette is the effect.
* **Water takes the ground wash separately**, in one added line, for the reason ADR-207 records for
  the world effects: Glowmere's elder stands in a pool, and light that stopped at the waterline would
  draw a hard edge across the exact shot the effect exists for.
* **The comet does not write the velocity target.** Writing the sky's camera-only velocity for a fast
  comet would smear it under motion blur; writing a correct one needs the previous frame's
  trajectory, which is state this system deliberately does not keep. Recorded as a limitation: a
  comet does not motion-blur, and at the speeds these cross the sky nothing in the renders suggested
  it should.
* **The aurora is the most temporally active thing in Glowmere's frame, by a wide margin.**
  `tools/temporal_stats.py` over a 180-frame static-camera sequence: 1.4% of pixels ever cross the
  flicker threshold with the effects off, 7.4% with six comets, 59% with the aurora. Attributed
  rather than guessed -- it is not the audio (removing every band and the spectrum leaves 61%), not
  FXAA (58.9% with it off) and not the post chain (62.7% with it off). What it is, is a large soft
  element animating: the flicker map shows broad curtain shapes rather than per-pixel noise, a 1:1
  crop is clean, and `tools/chroma_speckle.py` reads 0.55% against a 0.26% baseline, which is no
  fringing at all. The second difference cannot tell fast smooth motion from alternation, and an
  aurora is fast smooth motion over most of the sky. Recorded because the number looks alarming and
  the next person to measure it deserves the attribution rather than the fright.
* **Ground illumination is not lighting.** There is no shadow, no occlusion and no falloff with
  surface orientation beyond a hemispheric weight. §6 asks for cinematic atmospheric illumination
  rather than physical accuracy, and a high comet's pool of light lands kilometres away and is
  usually off screen -- which is why the broad wash exists alongside it and takes the larger share.
* `world/effects.cpp` lost its private `activeWindow` to an exported `resolveActivationWindow`. The
  behaviour is unchanged and ADR-207's tests still pass unmodified, which is the evidence for that.
* The `FrameUniforms` layout guard in `tests/unit/test_renderer_layout_guards.cpp` had to learn the
  new header bundle. It caught the three added types immediately, which is what it is for.

## Rejected alternatives

* **One list for both families.** Folding atmospheric effects into `"worldEffects"` would mean one
  `kind` field deciding which half of a much larger struct is meaningful, and a file in which most of
  every record is ignored.
* **A storage buffer instead of the frame block.** The data is bounded on purpose and is 1.4 KB. A
  buffer is what you reach for when the data is unbounded; see ADR-207's identical argument.
* **Volumetric marching for the aurora.** The engine has a froxel volume (ADR-058) and an aurora is
  the textbook case for it. Rejected on the range: `volumeMaxDistance` is 320 m in Glowmere and the
  curtains are at 5 km, so the aurora would have had to be a second volume with its own bounds and
  its own march -- a full-resolution pass per frame for an effect that is one ray solve per shell.

## Revisit triggers

* A scene wants more than six comets or more than two auroras at once. Both point at the same answer
  as ADR-207's: a storage buffer, and -- for comets specifically -- a coarse screen-space tile list,
  because the bounding-sphere rejection is already doing per-tile work redundantly per pixel.
* Somebody wants a comet to motion-blur, or to be reflected in the water surface. Both need state
  this design does not keep (the previous frame's trajectory; a reflection ray), and both are real
  requests rather than mistakes.
* The aurora's sixteen bins stop being enough for the shape somebody wants, at which point the
  spectrum should become a small texture rather than a wider uniform lane -- `SceneRenderer` already
  builds one for user shader layers (`updateSpectrum`) and it would only need a binding.
* `scene::Entity` gains a semantic category, at which point ground illumination could ask what kind
  of surface it is landing on rather than only which way it faces.
