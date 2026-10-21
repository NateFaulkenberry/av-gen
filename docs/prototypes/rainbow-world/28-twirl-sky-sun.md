# Rainbow World 2, gate 7: fireworks fade, the twirl, the sun, the sky

The owner's gate-6 verdict (verbatim in 22-owner-fixes.md) asked for four things:
- fireworks that fade about a second sooner;
- trees that twirl "like a dancing object in a disney movie rather than just a mechanical rotation";
- realistic sun rays, or cut them;
- audio reactivity in the sky ("pulsating clouds maybe, splashes of color").

Every motion below is a function of `a`, the metres the scan's front has run past a tree. The front runs at about
139 m/s (10 m to 980 m in SCAN_RUN = 7 s), so 14 m is about 0.1 s. Fields centred on the camera ride the front
(ADR-025), and effectors act on the instances (ADR-1500). There are no events and no state, so seek equals play.

## 1. Fireworks: a second sooner

| | Gate 6 | Gate 7 |
|---|---|---|
| Burst (flight) | 280 m (2.0 s) | 200 m (1.4 s) |
| Sparks shrink away | 140 to 280 m (gone at 2.0 s) | 70 to 160 m (gone at 1.15 s) |
| White heat cools | 60 m | 40 m |

The crackle lives while the spark does, so it now ends about 0.85 s earlier too.

The GPU has 16 field slots. Gate 6 used 13 of them, because each window was two ramps plus a compound. Gate 7 makes
each window ONE field: an inverted sphere is the fall, and its falloff is the rise (a falloff weights every field
kind). That frees six slots, so the fireworks now use 6 fields, the twirl 6 and the sky 1, 13 in all.

## 2. The twirl (Thomas and Johnston's principles, as fields)

Curves: ~/Desktop/av-gen-review/76-rainbow-world-2/gate7-twirl-curves.png. They were proved in Python against the
engine's field formulas before the build.

| Principle | How | Field |
|---|---|---|
| Anticipation | a counter-twist of -0.42 to -0.45 rad and a crouch the moment the front arrives | the twist and squash sines' first lobes |
| Slow in / slow out | the whole turn eases (smootherstep) out of the wind-up and into the landing | `fwTwSpin_*` (constant, Smooth falloff) |
| Squash and stretch | crouch, stretch tall mid-turn, squash on landing, a smaller rebound; XZ moves against Y (scaleAxis -0.5, 1, -0.5), so volume holds | `fwTwSquash_*`, a spherical sine with origin riding the front, an envelope, and a 12-16 m fade-in |
| Arcs | the same squash field lifts the tree on the stretch and sinks it on the squash (a hop). The tree also leans on its own X axis, and that axis turns with the spin, so the lean sweeps round like a skirt | the squash field: positionOffset local Y, rotation local X |
| Follow-through | an overshoot of +0.23 to +0.27 rad, then a damped wobble that settles within about 1.5 to 2 s | `fwTwTwist_*` (a spherical sine) |
| Overlapping action | the canopy wobbles 40% more than the wood | an extra twist effector on `*_leaves` |
| Variety | two temperaments (below), plus the front's own cascade: nearer trees start first | per-family effectors |

The two temperaments:
- **Heavy** (great tree, weeper): a longer wind-up, a turn over 22 to 150 m, squash 0.07 and a 1.4 m hop.
- **Light** (blossom, pine): quick, a turn over 16 to 105 m, squash 0.12 and a 2.2 m hop.

Each family also has its own direction: the great tree and pine turn one way, the weeper and blossom the other.

There is no per-tree variation within a family. Effectors read fields at the instance position, and the slots left
no room for a noise compound. Every tree ends exactly one turn round, so at rest the world is as built.

**Cost:** 6 or 7 effectors per tree part (the cap is 8), and 6 fields.

## 3. The sun: a glare, not spokes

The gate-5 burst was 22 translucent spokes starting 20 m off the sun, with a visible gap. It looked like a drawing.

True crepuscular rays need occlusion through a medium, and this world has none (ADR-570's march does not reach
it). The realistic look that remains is the sun's glare as the air scatters it:
- a forward-scattering halo: a tight core and a broad aureole, both exponential in the distance from the sun;
- inside it, many faint, soft, uneven streaks that start AT the sun and die with the halo, with no hard edges;
- one quad 4400 m across at 5000 m (`ray_asset.glare_texture`), base glow 1.0, still swelling with the synth.

Before and after: gate7-sun-glare-before-left-after-right.png.

Recommendation: keep the glare. If the owner still dislikes it, cut it: delete the `sun_burst` node and its routes,
and the sky's own sun disc stays.

## 4. The sky

**Breathe.** Every cloud puff swells about its own centre with the bass body, up to 14% at a full swell (`macro.swell`
drives the strength of a constant field, `skyBreath`, which a scale effector on the cloud nodes reads). The clouds
also brighten with it (`cloud_glow` +0.3).

**Splashes of colour.** Patches of saturated colour bloom on the clouds:
- **What drives them:** each clap (the drum stem's 2-6 kHz band: amount 0.7, decay 420 ms) and each phrase start
  (`beat.phrasePulse` 0.6, fading over about 1.1 s).
- **Where they land:** the patches are a ~400 m noise whose sample point travels with `beat.phrase` and `beat.bar`
  (bounded and seekable), so successive splashes land in different places.
- **Their colour:** it turns once round the spectrum per phrase.
- **How much shows:** at most about a third of the cloud area, never the whole sky (gate 2's rule: no world sweep).
- **Cost:** 10 ops in `rw_cloud`.

Clip: gate7-sky-reactivity-40-65s-before-left-after-right.mp4, which includes the drop at 47.85 s.

## 5. Live profile

The machine was on battery at the time (build/rw2/prof/ac_power.txt), so this run does not count against the bar.
Rerun it on AC.

| | Gate 6 (AC) | Gate 7 (battery) |
|---|---|---|
| Frame p50 / p95 | 33.4 / 35.9 ms | 33.3 / 36.9 ms |
| GPU p50 | 24.4 ms | 23.3 ms |
| Misses | 2 | 101 |

The gate-7 run spent 269 frames at Medium, mostly between the scans.
