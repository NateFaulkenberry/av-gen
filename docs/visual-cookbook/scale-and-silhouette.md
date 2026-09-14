# Scale and silhouette: two things a hero cannot supply for itself

Written while building the Tree of Life (`src/scene/tree_scene.cpp`). Four findings, three of which
have nothing to do with trees.

## An empty ground plane has no size

A thirty-metre hero read as an eight-metre one in every render for a week, and no change to the tree
fixed it: not the height, not the bole fraction, not the lens. Nothing in the frame disagreed with
"small", because **an empty ground plane has no size**, and a lone object in an empty frame is
whatever size the viewer assumes.

The fix was fourteen ordinary trees at 46 to 135 m, five to eleven metres tall, **forty triangles
each**, seen through mist. Thirty metres became legible in one render. This is the cue every
concept-art reference uses and it is the cheapest one there is.

The corollary matters as much: **the environment earns its place by giving the hero a referent, not
by being furnished.** Once the referents were in, adding more would have started competing with the
hero rather than sizing it.

## Distant casters coarsen the shadows on your hero

The referents are `castsShadow = false`, and that is not an optimisation.

This renderer fits its cascades to the bounding sphere of each frustum slice over
`[near, min(3 x scene radius, far)]`. A ring of distant trees is inside that fit, so it pushes the
far plane out, which stretches every cascade, which **coarsens the shadow texels on the hero
itself**. The hero owns the one cascaded directional light this renderer allows (a second is not
rendered at all), so anything that dilutes that light's resolution is spending the hero's budget on
scenery.

Rule: **background geometry placed for scale should not cast.** It is far away, its shadows are not
legible, and the cost is paid where the shadows *are* legible.

## The ground can be the thing fighting your silhouette

`docs/visual-cookbook/bioluminescence.md` records that a `fogColor` brighter than the background
makes distant surfaces *brighter* with distance, which is the opposite of a silhouette. The same
mistake arrives from the albedo side and looks different enough to miss: **a ground albedo brighter
than the horizon draws a pale band across the frame**, behind the subject, at exactly the height the
subject's base sits.

The check is the same one either way: whatever is behind the hero must be darker than the hero, and
"behind" includes the ground between the hero and the horizon.

A related one: the mist read warm-brown under a cyan canopy, because the ground's albedo came
through it. **The fog's own colour is what decides that**, and a haze whose hue fights the palette
fights it everywhere the two meet — which, with a low camera, is the whole lower third.

## Surface detail cannot exist below about a hundred pixels

At the showcase camera the hero's trunk is about sixty pixels wide. Bark was authored as a fine
radius perturbation on the swept tube and was completely invisible, and the instinct -- make it
stronger -- produces a rough cylinder rather than a gnarled one.

What reads at sixty pixels is **the outline being irregular**, not the surface being textured. The
answer was two octaves doing different jobs: a short wavelength for grain, and a much longer one for
the swelling and irregular taper that says *old*. That is a scale-aware answer rather than a louder
version of the wrong one, which is the more common outcome.

The same argument is why the trunk has no normal map: a tangent channel would cost sixteen bytes on
every vertex in the engine to carry detail that is below the pixel at the only camera that matters.
