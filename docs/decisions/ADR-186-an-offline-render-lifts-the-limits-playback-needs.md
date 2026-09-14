# ADR-186: An offline render lifts the distance limits playback needs

**Status:** Accepted
**Date:** 2026-09-14

## The gap

The tier table has said since ADR-035 that the Offline tier takes "no representation shortcut"
(§5.9), and `QualitySettings::forTier` spells that out as data: `materialTiers = false`,
`forcedMaterialTier = Full`, `renderScale = 1.0`, `lodHysteresisAllowed = false`.

Every one of those is about **shading**. Three *geometric and temporal* reductions were never
covered, because each lives in a subsystem that decides for itself:

| Reduction | Where it is authored | What it costs the picture |
|---|---|---|
| Procedural distance cull | `LodSettings::maxDistance`, `minScreenRadius` | distant scatter is not drawn at all |
| Procedural LOD rungs | `LodSettings::lodDistances` | what is drawn is a simplified mesh or a billboard |
| Rig pose rate | `SkinnedRig::nearDistance`, `farHz`, `cullDistance` | distant characters step at 20 Hz, or freeze past 120 m |
| Entity behaviour bands | `EntityDesc::fullDetailDistance`, `cullDistance` | distant characters stop living entirely |

So a finished render at the Offline tier still put billboards in its far field and still had a
motionless herd on the far hillside. The promise was in the table; the far field was not in it.

## The decision

`scene::DetailLimits` -- four booleans, every one defaulting to `true` ("honour what the scene
authored", which is exactly live playback). It rides on `Scene` because that is the one object all
three readers already hold, and it is **not serialised**: a scene file that could switch its own
limits off would be the scene-specific quality decision §49 forbids.

`RenderSettings::limits` is the render's answer, in three words:

* `tier` (the default) -- the tier decides. Offline lifts them all; every other tier keeps them,
  because a `--tier realtime` render exists to be a fast preview of the live picture and a preview
  that quietly drew the far field at full detail would be previewing something else.
* `live` -- keep them whatever the tier.
* `unlimited` -- lift them at any tier.

`RenderSettings::resolvedLimits()` is the only place those words mean anything, so the job, the
panel and a test cannot disagree about what `tier` resolves to.

## Frustum culling is not in this

What is off screen stays off screen, at every setting. Removing geometry outside the frustum is not
a reduction in what the frame shows, and an object behind the camera contributes nothing to an
offline frame either. The same goes for the camera's far plane: how far the camera sees is a
compositional decision the author made (a composition already sets it to `max(radius * 50, 2000)`),
not a playback budget, so it is untouched.

## Told to the engine, not the renderer

Two of the four live in the simulation rather than in a pass. `RenderJob` calls
`engine_->setDetailLimits(...)`, and `Engine::update` writes it into the controller's scene every
frame -- before the warm-up's seek, so the catch-up integration runs under the same policy the
frames will.

This is ADR-147's lesson again: a policy set on the interactive side only is a policy the
deliverable never gets. The same pass found a second instance of it and fixed it -- `--tier` was
read by the interactive renderer and never copied into `RenderSettings::tier`, so
`--render out --tier realtime`, the fast proof before committing an hour to a sequence, still
rendered at the offline tier.

## Cost, and the shape of the implementation

The procedural half needed **no shader change and no second code path**. Zero is already
`shaders/cull.wgsl`'s "no limit" for both distance tests, and a zero first threshold already ends
the LOD ladder at rung 0 -- so lifting the limits is filling the existing uniform with the values
that already mean "everything, at full detail". The CPU-side whole-object early-out
(`objectFullyCulled`) had to hear about it too: it rejects an object before the cull pass is
encoded, so a policy that reached only the shader uniform would have changed nothing at all.

## Shown to change the output

ADR-182: an arm that cannot fail is worse than no arm. Three frames of Glowmere Valley 2 at
t = 6.00 s, same build, same GPU lock:

* `--render-limits live` -> sequence hash `97d4ab810562a108`
* `--render-limits unlimited` -> sequence hash `eb0ab8857e214301`
* 12,304 of 2,073,600 pixels differ (0.59%), max channel delta 137, in a box covering the lower and
  far part of the frame -- the distant scatter.

## Consequence to be aware of

The default for a render is `tier`, and a render's default tier is `offline`, so **existing projects
now render with these lifted**. That is the tier table's stated promise finally being kept rather
than a new policy, but it is slower on a world with heavy scatter, and `--render-limits live` (or
the panel's "draw distance: live") restores exactly the old behaviour.

One of the four moves the simulation and not just the picture: an entity that keeps walking ends up
somewhere an entity that froze does not, so a render with the behaviour bands lifted is not
frame-identical to a preview with them in force. That is the point -- but it is a real difference and
not only a sharper far field.
