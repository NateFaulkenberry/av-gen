# ADR-347: The sea joins the cycle, and the fog stops arguing with the sky

Status: accepted
Date: 2026-09-18

## Water

Reflection and deep colour are curves on `dayPhase` like everything else, so the sea is part of the
world changing state rather than a sheet the sky falls on (the water spec's §28). Near water runs
25/38/60 sRGB at midnight against 152/168/187 at noon — a 6× swing where it had none — and
`rippleScale` 0.030 → 0.055 gives detail that survives at this camera distance.

**The warm ends are a workaround and it is worth knowing why.** The water shader samples the
environment *cube*, which is the HDRI: a fixed blue-sky map. Since ADR-345 made the visible sky
procedural, the two disagree exactly where the brief cares — its §28 names "sky says sunset, water
still looks like noon" as a failure mode. Turning reflection down at dawn and sunset and carrying
the colour in the water's own body closes most of it: water R−B goes −33 → **−6.3** at dawn and
−33 → **−2.6** at sunset, against a sky at +47 and +49. Closing the rest means feeding the analytic
sky into reflections, which is a renderer change and is not this one.

## The second horizon, and two wrong answers before the right one

The grazing view — eye 35 units above the water at noon, the hardest case — showed a flat band
between the textured near water and the sky, reading as a second horizon.

**Not the plane's edge.** At 20,000 units that subtends 0.1° below the horizon; the band was ~1.4°.

**Not the water's reflection either**, and the control settles it rather than an argument: the band
reads 24.1/31.5/55.0 with `reflection` at 0 and 24.1/31.6/55.0 with it at 3 — identical — while the
same two arms move the mid water 23 → 70 and the near water 5 → 159. Whatever paints the band, the
environment sample is not it.

**It is the fog.** Beyond a certain distance the water is 100% fog colour. The scene's fog was
(0.0125, 0.0165, 0.0335) linear = 29.3/34.6/51.3 sRGB against a measured band of 24.1/31.5/55.0.
Distant water fully faded to one colour, meeting a sky of another.

So the fog takes its colour from the sky's own horizon now, scaled the way the visible sky is
scaled — which is what atmospheric perspective *is*, rather than two curves an author has to keep
in agreement by hand. `fogHorizonBlend`, default 1; 0 restores the authored curve. Verified in the
grazing view: one horizon.

The fog **density** was separately 15× too high, tuned when the ocean plane's edge was at 3,000
units. Transmittance is `exp(-(d·density)²)` and the pair that matters is the island at ~257 u and
the water edge at 20,000 u; 0.000135 leaves the first at 0.999 and the second at 0.0004. At the old
value sunset rendered as flat pink dust with island, water and sky all one colour.

## The dark specks were clouds

Recorded because the hypothesis was wrong and the refutation is the useful part. The ocean world's
grazing view was speckled with dark blobs. They were not the star nodes — removing all four
procedural nodes left *exactly* the same pixel count. I then supposed they were DWAA compression
artifacts in the bright sky around the sun and taught the importer to decode the lossless PIZ
original instead.

The measurement refuted it: the day map has **2,676** dark-outlier texels decoded from the DWAA
tier and **2,727** decoded losslessly, in the same region, and the rendered blob count went *up*,
2,463 → 6,190. Cropping and tone-mapping that region — x 769..1198 of 2048, where every outlier
sits — shows a dark cumulus bank silhouetted against the sun's glare. They are clouds. The grazing
camera magnifies that patch of sky across the frame, which is the only reason they read as specks.

The lossless decode is kept, for the reason that actually holds rather than the one it was made
for: the spec asks for the original data as supplied, PIZ is lossless, and the derived file costs
6.0 MB against 5.7.
