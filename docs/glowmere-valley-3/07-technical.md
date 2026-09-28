# 7. Technical improvements made during production

Each change is small and reusable: engine fixes apply to every scene, tools to every production.
None was built before something in this production needed it.

## 7.1 Terrain: a feature's level is continuous across its path's bends

**Found** by scouting the valley from above for this production. A top-down map showed three straight
dark lines across the valley and a step in the river. The owner asked for them fixed.

**Measured** with the new seam scanner (§7.4): **1,455 cells of cliff in 42 runs, up to 2.35 m high.**
There were four straight walls across the valley, not three.

**Cause.** A path feature's level is the path's height at the nearest point
(`closestOnPath`, `src/world/world_map.cpp`).

- On the inside of a bend, past its centre of curvature, the nearest point jumps from one arm of the
  path to the other across the medial axis, and the level jumps with it.
- Glowmere's 300 m valley corridor descends about 2 m per 60 m of course, and flattens the ground
  toward that level. So each jump became a cliff running straight across the valley from a bend.

**Fix: `blendedLevel`.** The level is now an integral of the path's level over every part of the path
that is nearly as near, weighted by a kernel of how much further it is. Every term is continuous in
the query point, so the level is too.

- The integral is taken by 5-point Gauss-Legendre per segment.
- Where the average agrees with the old nearest-point level to 5 cm, the old level is returned
  exactly, fading to the average over the next 5 cm. So terrain away from bends does not move.
- `AVGEN_LEVEL_BLEND=0` switches the fix off for comparison.

**Two cheaper versions were built first and both failed,** for one reason:

1. Blending the local minima of distance along the path, faded by path length, left 637 half-metre
   steps.
2. Weighting those minima by their persistence left steps up to 2.53 m.

Far inside a bend the distance along the path is nearly flat over a long stretch. Any rule that
*picks points* jumps as the stretch's deepest wiggle moves. Only an average over the whole stretch
doesn't.

**Result.** The scanner finds **0 seams** on the ground. Five tests cover it
(`tests/unit/test_path_level_continuity.cpp`), and each has a control:

- the synthetic corridor's old level jumps 2.65 m across the axis the test crosses;
- each Glowmere transect crosses a place where the old level jumps more than a metre.

With the fix switched off, the Glowmere crossings fail at 1.08–2.19 m and the synthetic one at 2.65 m.

**What else moved** (measured against the multicam scene's placements):

- **Objects:** 12 of 28 placed objects sit on ground that changed by at least 1 mm. That's the
  animals, which re-ground every frame, plus four heroes by 0.3–3.9 cm. Scree's ground fell 1.9 cm,
  inside the float it already had from centre-point seating (§7.6).
- **Grid:** of 2,304 grid points, 950 moved at least 1 mm, 281 at least 5 cm, and 105 at least
  20 cm. The 20 cm ones are all in the bands around the old cliffs.
- **Cost:** the whole-map seam scan runs in about 1.7× its old time. Height queries on the insides of
  bends now integrate.

## 7.2 Water: two waters that overlap meet without a wall

**Found** as the "step in the river". The scanner measured **a 2.63 m wall of water across the
channel at z ≈ 82**.

**Cause.** `WorldMap::waterSurface` took the *highest* surface of any water feature reaching a point.

- Glowmere's elder-pool sits on the river with its surface 1.2 m above the river's.
- The highest-surface rule carried the pool's surface across the channel to the edge of the pool's
  reach, and dropped it 2.6 m there, in mid-river.
- The water mesh places each vertex at that surface, so the drop rendered as a wall of water.

**Fix.** Where waters overlap, each surface counts in proportion to its own weight. A surface fades
out where its feature does, the same principle ADR-830 applied to the terrain's cuts. One water on
its own keeps exactly its own surface.

**Result:** 0 water seams. Two tests: the wall-free transect, with a control proving the pool really
raises the water, and the single-water identity.

**Rejected:** blending the *water table* the same way. The ecology uses it to place vegetation. No
defect showed there, and the blend re-routed the aliens (§7.3) for nothing in return.

**A test re-baselined, not loosened.** The full CPU suite found one casualty of the re-routing.
- **The test:** the directed-orders test proves "face Vane" works against a control where Rook, on
  his own, does not face Vane.
- **What moved:** re-routed, he now does, to 0.31 rad.
- **The proof:** a throwaway build with the old water rule passes.
- **The fix:** the test now orders Rook to face Sage. On his own, Rook's closest to Sage is 2.01
  rad, the most margin of the four aliens; ordered, it is 0.03 rad.
- **Unchanged:** the thresholds (ADR-894, consequences).

## 7.3 Motion: a walking body's bob eases off instead of dropping

**Found** as a side effect. ADR-830's benchmark, "no alien's foot jumps in the first 40 s",
failed after the water fix. That fix changed where the water is, the aliens are drawn to water, and
Ember took a different route.

**Cause.** `Liveliness` scaled the stride bob by the body's *intended* speed.

- A decider that wants a sharp turn drops its speed to almost nothing in one step, and the body's
  measured travel stops dead with it.
- The bob fell from its crest to the ground in that one frame: **0.31 m in 1/60 s on Ember**.
- The foot IK absorbed it as a 0.19 m jump in both feet.
- The failure was not on the terrain. A 1 cm probe along Ember's path found no step larger than 2 mm.

**Fix.** The bob's amplitude eases toward its target with a 250 ms settle
(`src/entity/behaviors.cpp`). The body stopping dead is still the locomotion's to answer, and is
recorded as an open item.

**Result.** The benchmark passes on every route tried, including main's. Worst foot steps are now at
or below main's on every alien: Ember 0.048–0.054 m (main 0.059), Rook 0.057 m (0.061).

**A pinned trace re-pinned.** ADR-623's default-off test hashes Glowmere's first three seconds,
rig poses included, against a digest pinned before motion matching was wired. The eased bob changes
every walking pose, so the digest moved. It was re-pinned, as ADR-830 did before, because:
- no matcher code changed;
- the scene carries no key;
- the control arm still shows the key changing the trace.

## 7.4 Tools

| Tool | What it answers |
|---|---|
| `avgen_world_preview --seams` | Every step in the ground or the water surface that no slope explains. It paints them on the map, prints them as runs, and exits 3 if it found any. Probes now print the water level, to the millimetre. |
| `avgen_cast_trace` | Where every character and the saucer are at every moment of the film, from the real engine at the render's frame rate. It is the call sheet for placing cameras on a cast nobody scripts. |
| `tools/contact_sheet.py` | Labelled frames on one sheet: from PNGs, from video instants, or one row per shot. |
| `tools/gv3/review.py` | A cut reviewed shot by shot: contact sheets plus per-shot exposure, contrast, colour and motion, with blunt flags. |
| `tools/gv3/ground.py` | Ground and water heights asked of the engine, cached, so a camera can be placed "1.8 m above the ground". |
| `tools/gv3/framing.py` | Where each shot's subject sits in frame and how tall, from the rigs and the trace; with `--video`, whether it is actually seen (§7.9). |
| `tools/gv3/cuts.py` | The frames either side of every cut, for eye-trace and geography. |
| `tools/gv3/shimmer.py` | Frame-to-frame change inside a band of a locked-off shot: shimmer and crawl on water, measured. |

The CMake engine source list that `avgen_character_quality` used is now one named list,
`AVGEN_OFFLINE_ENGINE_SOURCES`, shared with `avgen_cast_trace`.

## 7.5 A 30 fps preview is a different film

This was measured before any preview was rendered, with the cast trace. An entity steps at 1/fps,
so a 30 fps simulation of the multicam film put Ember **23–29 m** from where the 60 fps one did
within 30 s; the others stayed within a few metres. Every preview of this production is therefore
rendered at the final 60 fps, cheapened by resolution instead.

## 7.6 Hero seating on slopes

Heroes were seated by one height probe at their centre, so the downhill side of a stem floated on a
slope: scree by 12.6 cm, cairn by 4.1 cm. The GV3 generator seats every hero on the lowest ground
under its stem's base ring, sunk 10 cm. It writes that to both the scene and the project's
`nodes/*/position`, because the project's value is the one that renders (ADR-264).

## 7.7 A node's `emissiveBoost` does not reach a procedural node (recorded, not fixed)

Every scene node registers `nodes/<name>/emissiveBoost`, and the parameter panel, the route editor
and the timeline all accept it. `Composition` multiplies it into the material emission of an
imported mesh's entities. For a procedural node it is computed and then never used: the procedural
branch of the node update applies the node's own procedural and per-part parameters, and nothing
else. A route onto it binds, reports no warning, and changes nothing. That is how GV3's first
heartbeat failed (F23).

A procedural node whose emission comes from a material program would not answer even if it were
fixed, because a program that writes emission owns it (ADR-179). What does reach such a surface
is the FXL gain of an effect on the node (ADR-703), which multiplies after the program. That is
what the heartbeat now uses.

**The same dead path elsewhere, found while measuring the blast radius of a fix:**

- **Glowmere Valley 2** and its derivatives route `music.downbeat` and `music.drop` onto the elder's
  gills and `music.section` onto its cap. All three nodes are procedural and program-lit, so none
  of those routes has ever done anything, and the fix would not change that.
- **`glowmere-stylized` and `glowmere-lyrics`** route the same three signals onto `elder-filaments`
  and `elder-crown`. The filaments are procedural with **no** program, so the fix would bring their
  downbeat and drop routes to life for the first time. The crown has a program and would stay
  unaffected.
- **ADR-343's day/night cycle** dims the scene's named star nodes by multiplying `emissiveBoost`.
  The tree-of-life ocean world's stars and motes are procedural, so the cycle's dimming never
  reaches them. All it can do is hide them at zero brightness. (I did not check whether anything
  else dims them on screen.)
- **`tree-of-life-floating-island-night`** authors those stars at 2.1–2.2 and the motes at 1.9.
  Eleven `_ck-*` check projects beside it author them at 1.15–1.25. None of those values has ever
  reached the picture.

**Why it is not fixed here.** The fix itself is small. After the per-part parameters, multiply each
part's material `emissiveIntensity` by the boost. That value is re-applied from its parameter every
frame, so the multiply cannot compound. It is also not in the procedural structural hash, so it
costs no regeneration. But it would change other productions' approved pictures: the night
film's stars would become twice as bright, and the stylized elder's filaments would start to
pulse. Whether they should, or whether those values should be
reset to 1 first (ADR-441), is the owner's decision, not this production's.

## 7.8 Where the elder's gold can be reached

The heartbeat's first two targets were both correct according to their documentation, and both
did nothing visible:

1. **`nodes/elder-2-gills/emissiveBoost`:** dead on a procedural node (§7.7).
2. **A Glow on `elder-2-gills`:** its FXL gain does act after the material program
   (`shaders/pbr_shade.wgsl` multiplies the program's finished emission). But the gills are the thin
   radial lamellae. The gold that fills a frame is `elder-2-under`, so a 1.9× gain on the gills moved
   the gold 1.4%. A difference image between a kick frame and the frame before it showed exactly which
   pixels moved.

What reaches the gold is the program's own `emissionIntensity` on `glowmere2TissueWarm`. The elder's
gills and underside are the only surfaces that use that program.

**The lesson:** confirm a modulation target on a difference image of the render, not from its name.

## 7.9 Seeing occlusion from the trace: `framing.py --video`

The projection check (§5.1 layer 3) cannot see a fern in front of an alien. With `--video`, every
half second of every shot of a white body (the aliens, the horse), the rendered frame is sampled
where the subject projects. The subject counts as seen when the brightest tenth of that patch stands
well above the frame's median.

- **Why brightness and not saturation:** under the valley's cyan light, a white body measures about
  0.6 saturation, so "pale and unsaturated" missed every alien.
- **Validation on v1:** it agrees with the contact sheets. Ember, Tide, Rook and the horse are seen
  100%; Sage in s18 84%, which is the fern occlusion the sheets caught by luck.
- **The flag:** a shot is flagged when its subject is seen in under 90% of samples.

**Limits:** it cannot tell a subject from a bright thing behind it, and it does not check the saucer,
which is dark with bright lights.

## 7.10 A preview at a lower resolution hides fine detail the final will show

**Where it bit:** the water's ripple layers fade by their size in pixels. The previews (960×540 at 2×
supersampling) and the final (1920×1080 at 2×) differ by a factor of two in the pixel footprint,
so a ripple scale tuned on previews showed its texture across the whole far river in the final.
Caught on 1080p stills before the final render, and fixed by doubling the frequency and halving the
amplitude (F37).

**The same shape elsewhere:** anything that fades, band-passes or levels-of-detail in screen pixels
changes between the previews and the final. That includes the water's sparkle band-pass, particles'
minimum pixel size, the ribbon's minimum width and bloom radii.

**The check that catches it:** before a final render, a handful of stills at the final resolution
of the shots where fine detail carries the look.
