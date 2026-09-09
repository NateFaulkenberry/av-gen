# Fungi, and the radius profile

`examples/organic/fungi.json` is five glowing fungi. Every part of them -- stem, cap, glowing
underside -- is the same `tube` primitive.

## A cap is a radius profile, not a new primitive

A tube's `tubeTaper` interpolates the radius **linearly** from start to end, so a flare is a cone.
A mushroom cap is not a cone: it is widest at the rim and curves over to the apex.

The spline already solves this. Each control point carries a `scale`, and the sweep multiplies the
radius by it, so an explicit `points` curve is a radius profile of any shape:

```json
"curve": {
  "kind": "catmullRom", "generator": "points", "samplesPerSegment": 10,
  "points": [
    {"position": [0, 0.000, 0], "scale": 1.000},
    {"position": [0, 0.015, 0], "scale": 0.987},
    ...
    {"position": [0, 0.150, 0], "scale": 0.020}
  ]
}
```

with `scale = cos(u * pi/2) ^ 0.55` -- widest at the base, narrowing to the apex. Reverse it and
you get a bowl; raise the exponent and the cap flattens; lower it and it becomes conical.

This is worth knowing generally: **any varying radius along a tube is a points curve with scales**,
which covers gourds, pods, bulbs, seed heads, tentacle suckers and the swelling at a branch fork.
No engine feature is needed.

## The species

| Part | How it is made |
|---|---|
| stem | a tube on a noise curve, taper 0.72, slight twist |
| cap | a tube on a dome profile, its own rotation per individual |
| underside | the same profile, slightly smaller, rotated 180 degrees so it faces down |

The glow lives on the underside rather than on the whole body, which is what makes them read as
lit from within rather than as painted. The rim of light under the cap is the entire effect.

## What is missing

- **Gills.** The underside is a smooth surface. Real gills are radial blades, which is a radial
  distribution of thin tubes -- available, not yet done.
- **Material.** The cap is a plain dielectric and reads as white plastic. Fungal flesh is
  translucent: light passes through the cap edge. That is subsurface scattering, and the engine
  has none.
- **Shape variation between individuals.** All five share one cap profile. A species should vary
  its profile with the seed, the way the plant grammar varies its branching.
- **Everything around them.** They stand on a bright grey plate. Fungi belong on rotting wood, in
  shade, near moisture -- which is ecosystem scattering, a later phase.
