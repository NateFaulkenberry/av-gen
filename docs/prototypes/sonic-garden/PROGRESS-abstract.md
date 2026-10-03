# Sonic Abstract (the abstract direction): progress (sonic-art)

Brief: `04-brief-abstract-direction.md` (the owner's words govern). Plan: `ABSTRACT-PLAN.md`. Worktree
`/Users/natefaulkenberry/Documents/GitHub/av-gen-sonic`, branch `proto/sonic-garden`, shared with the `sonic-engineer`
(who owns `src/`, `shaders/`, `tests/` and posts ready notes in `PROGRESS-stylized-eng.md`). I own `tools/sonic_vfx/`,
`examples/sonic-abstract/`, the "Sonic Abstract" entries of `examples/index.json` and my docs. Commit only my own paths
(`git commit -- <paths>`), never `assets/`. Push after milestones (`git push origin proto/sonic-garden`). Review media:
`~/Desktop/av-gen-review/28-sonic-abstract/`.

**Its own project** (the owner's decision, 2026-10-02 18:40): the eight prototypes are "Sonic Abstract", separate from
Sonic Garden. Do NOT modify `examples/sonic-garden/*` or the "Sonic VFX" entries of the index; the old tooling defaults
(`kit.OUT_DIR` = `examples/sonic-vfx`, `scenes.SCENES` = the old 16) stay as they were.

## Resume here (cold)

- **State (2026-10-03 00:30): the set of NINE is built and committed** (briefs 05-08 amended 04; see `ABSTRACT-PLAN.md`
  for the set, the designs and every modulation map):
  1 Sacred Geometry Flight `sacred_flight.py`, 2 Neon Vector `neon_vector.py`, 3 Cel Dream `cel_dream.py`, 4 Color
  Geometry `color_geometry.py`, 5 Organic Garden `organic_garden.py` (now with three radiolarians), 6 Digital Alpine
  `digital_alpine.py`, 7 Chromatic Topography `chromatic_topography.py`, 8 Glitch Signal `glitch_signal.py`,
  9 8-Bit Ocean `bit_ocean.py`. `scenes.ABSTRACT` lists them in that order; the index's "Sonic Abstract" category lists
  the nine. Removed (modules, projects, index entries, review media moved to `28-sonic-abstract/work/removed/`):
  Impossible Architecture, Abstract Cinematic Void, Particle/VFX World and the flat Sacred Geometry Garden.
- **Generated meshes:** 6, 7 and 9 instance GLB meshes written by `tools/sonic_vfx/meshes.py` into
  `examples/sonic-abstract/meshes/` at build time (deterministic; committed, about 12 MB; no `assets/`).
- **Next:** (a) a last look round on 8-Bit Ocean (its verdict is "charming, not yet stunning": denser set dressing near
  the lane, stronger colour contrast); (b) the FINAL PRODUCTION for all nine on the pin: `abstract.py stills` (1080p
  silent stills, one queued job), `clips --class allyougot --seconds 30`, `clips --class full`, optionally `clips
  --class rebuild --seconds 30`, perf (`perf.py <the nine ids> --projects examples/sonic-abstract --class allyougot
  --size 1920x1080 --tier realtime --frames 150 --work $OUT/work/perf --md $OUT/perf-realtime-1080p.md`), `tour --class
  allyougot --seconds 3.6`, `notes`, `maps`, `sheet`; (c) the live tour (`live.py sacred-flight --abstract --scenario
  tour --no-capture`, 60 fps check); (d) `[sonic]` and `[sdf]` on both test binaries under the lock; (e) the report and
  the hand-back. `$S/abs/batch3.sh` is the old production script (update its id list).
- **The scratchpad is WIPED when a session restarts**: the pin and `avgen.sh` must be rebuilt (steps below).
  `review.py` refuses to render without the pin.
- **The pin is `8982404a`** (the engineer's last hand-back: cel lighting ADR-1071, outline ADR-1072, wire lines ADR-1073,
  the per-set switcher ADR-1074, letterbox, camera roll and the tiny-scale fix ADR-1075), at `$S/vfx/bin-8982404a`;
  `$S/vfx/avgen.sh` points at it.
- **Build:** `python3 tools/sonic_vfx/abstract.py build [module ...]` writes `examples/sonic-abstract/<id>.json` and
  `.scene.json` (and the module's meshes). With no module it builds all of `scenes.ABSTRACT` and rewrites the index's
  "Sonic Abstract" entries.
- **Check without the GPU:** `$S/vfx/avgen.sh --project examples/sonic-abstract/<id>.json --audit-routes out.json 2>&1 |
  grep -iE "warn|error|skipped" | grep -v "live-only-source\|silent-source"`. It must print nothing (the two excluded
  rules are expected for live projects: engine need 8).
- **Review** (every render goes through `tools/gpu-lock.sh` with the pin):
  - `abstract.py stills <id ...> --size 960x540 --tag vN [--at T]` gives look-development stills in
    `28-sonic-abstract/work/blockout/` (one queued job; each still is the last frame of a 3 s pre-roll);
  - `stills` with no size gives the 1080p silent stills `NN-<id>-still.png`;
  - `clip <id> --class allyougot|rebuild|full [--size] [--seconds] [--out]` gives one reactive clip; `clips` a set;
  - `frame`, `sheet [--blockouts]`, and `music` (cuts the real-music excerpts into the gitignored `assets/audio`).
- **Signal meter** (look development only, `$S/abs/meter/`): a scene of eight bars whose heights are signals, rendered
  under the review music, read back from the pixels. It is how the Glitch collapse threshold was measured. Rebuild with
  the snippet in the log entry of 00:00.
- **Live:** `python3 tools/sonic_vfx/live.py <id> --abstract --scenario demo` runs one prototype live. The switcher
  steps through the set the open project belongs to (ADR-1074): the nine, program n = scene n mod 9.
- **The pinned engine:** a three-line wrapper `$S/vfx/avgen.sh` sets `AVGEN_SHADER_DIR` to the pin's shaders and runs
  its `avgen`. `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  Never render art from `build/release` (the engineer's). To re-pin:
  1. `git archive <sha> | tar -x -C $S/vfx/src-<sha>`;
  2. `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`;
  3. build `avgen avgen_sonic_probe`;
  4. copy the binaries and `shaders/` into `$S/vfx/bin-<sha>`;
  5. point `avgen.sh` at it.
- **GPU:** every job goes through `tools/gpu-lock.sh`, one job per hold. No `timeout` command. The engineer is done
  (the GPU is ours).
- **Real music:** 30 s excerpts of the owner's tracks (Desktop: `All You Got.wav` from 34 s, `Rebuild.mp3` from 156 s),
  cut by `abstract.py music` into `assets/audio/sonic-abstract-{allyougot,rebuild}.wav`. Classes `allyougot` and
  `rebuild` in `review.REAL_MUSIC`. The 12-second intensity follower starts at zero in a clip that starts mid-song, so
  `response.intensity` climbs through the whole excerpt (do not trigger anything on "level above intensity").

## Engine needs (for the coordinator)

1. ~~An output letterbox~~: landed (ADR-1075, `post/display/letterbox`); the Cinematic Void uses it.
2. ~~A camera roll parameter~~: landed (ADR-1075, `camera/roll`); the Neon flight banks with it. The Relativity Court
   keeps rolling the WORLD on purpose (its shadows sweep as the court turns against the sun).
3. **Translucent procedurals** (Blend draws opaque): lines cannot fade out against what is behind them, only into a
   matching colour (the Sacred Geometry lattice takes the halo's colour to fade).
4. **A per-instance phase for deformers** (a wave travelling across a distribution's instances): a field of slabs
   flipping in a stadium wave needs one node per row today.
5. ~~A BUG: a small uniform scale pops an object back to full size~~ (fixed, ADR-1075; the 0.01 parking stays, harmless). `Transform::fromMatrix` (`src/scene/scene.cpp`)
   falls back to the identity (keeping only the position) when `glm::decompose` fails, and it fails when the composed
   matrix's determinant is under float epsilon: a uniform scale below about 0.005 (0.001 cubed is 1e-9). Measured
   2026-10-02 with four tori turned 90 degrees at scales 1, 0.1, 0.01 and 0.001 (a node scale or a distribution
   transform): at 0.001 the torus drew at full size and unrotated. Any route or track that shrinks something toward
   zero makes it jump to full size at the bottom; 0.001 is also the parameters' hard minimum, so a clamp lands on it.
   Workaround here: park at 0.01, or hide under geometry.
6. **A far surface leaks the cleared background.** A second opaque surface 25 m in front of another at 1700 m (a
   cylinder's triangle fan) showed the environment's background colour through slivers of its triangles: its depth
   prepass and colour pass disagree at that range (a red background turned the streaks red). The Cinematic Void now
   draws its sun's reflection inside the mirror's own program.
7. **The headless sky's sun disc is blocky**: offline renders draw the procedural sky from the prefiltered lighting
   cube (`sky_background.wgsl`, the non-live branch), so its sun disc is a few texels wide. The Cinematic Void draws its
   sun as geometry.
8. **The liveness audit for live projects**: a `sonic.live` project hears its input, but `--audit-routes` calls every
   `audio.*` and `beat.*` route dead because the project has no audio file.

9. **A refractory period for a trigger** (a route chain `cooldownMs`, or the effect `Trigger`'s). Glitch Signal's
   collapse is one trigger staged by delays; a strong passage can fire it on several kicks in a row, which chains
   collapses into continuous corruption. Today the only control is the threshold (measured: kick envelope x flux above
   0.62 fires three to five times in thirty seconds of the review music).
10. **A hidden procedural template composes nothing** (ADR-029 references): `visible: false` on a template makes every
    object that references it draw nothing, and the template's own distribution transform reaches the composed copies
    (parking it far away moves them too). Sacred Geometry Flight keeps its templates visible in the camera's own plane
    so the near plane clips them. Either is worth a line in the docs; the first looks like a bug.
11. **Planar reflection for a flat water plane** (optional): the engine's water reflects only the environment (sky), so
    Digital Alpine reflects its mountains with mirrored twin meshes and paints the sky's mirror image in a dome's
    program. That works and is cheap; a real planar reflection would let ripples distort the mountains' reflection too.

## Engine facts learned this pass
- **A material op's constants and `value` are clamped to ±1000** (they are registered as parameters with that hard
  range): a smoothstep over 200..4500 m silently became 200..1000. Work in decametres (Digital Alpine, Chromatic
  Topography and Glitch Signal multiply the camera distance by 0.1 first).
- **A delayed route reads its input interpolated between frames**: a one-frame event delayed by a non-integer number of
  frames can arrive below its threshold. Trigger staged routes from envelopes (`kickEnv`, `noteEnv`), not events.
- **The xz radial distribution puts angle e at (cos e, 0, -sin e)** (its frame is a = +x, b = -z); `tangent` orientation
  looks along increasing e.
- **The sky's sun glows only above its own horizon** (the ground hemisphere is flat), so a sun straight down a
  horizontal flight draws a horizon line across the frame.
- **Material programs run on mesh sources and on toon materials**: a program's `baseColor` output is what toon light
  shades (8-Bit Ocean's floor), and an unlit program's `emission` is the whole look (Digital Alpine).
- **`integrate` works on any parameter** (`camera/roll`, `post/grade/hueShift`, a noise's offset): a 0.2 s pulse
  integrated is a persistent step, which is how Glitch Signal rebuilds into a new configuration.

- A field deformer (`"kind": "field"`) defaults to `alongNormal: true`: a scalar field then inflates a mesh along its
  normals (a tube becomes a fat white mass). Set `"alongNormal": false` to displace along `axis`.
- A distribution transform's rotation turns the arrangement too: a `linear` line of rings along -Z under
  `rotation [90, 0, 0]` becomes a vertical stack. Turn each instance with `sourceTransform.rotation` instead.
- A single giant additive particle is MUCH brighter than its colour times `emissive` suggests (0.07 still flooded the
  frame). Use an emissive disc for a soft glow.
- An opaque procedural cannot fade out: a line faded to black prints as a dark line over whatever is behind. Fade it
  into the colour of what is behind it instead (the lattice takes the halo's colour, through the same radial function).
- The twist deformer is `angle = amount * y + speed * t + phase`: `phase` is a routable rotation (radians), so a twist
  with amount 0 is a spin. Two local twists give a ring its spin and its opening about a diameter.
- The route chain has `delayMs` (up to 4 s), which staggers one hit across nodes into a travelling wave.
- `volumeMaxDistance: 0` with `volumeDensity` > 0 is analytic surface fog only (no march).
- Parking an object at scale 0.001 does NOT hide it (engine need 5): it draws at full size, unrotated. Use 0.01.
- A particle system and a procedural node with the same name collide: the particle parameters do not register
  (`particles/beads/burst` was unknown while a procedural `beads` existed).
- Lights are steered by `lights/<id>/azimuth` and `elevation` (there is no direction parameter); the sky's colours are
  `env/sky/{zenithColor,horizonColor,groundColor,sunColor}`.
- A procedural's scale cannot be negative, but a radially symmetric ring mirrors to itself: its twin in a mirror plane
  is the same ring at the mirrored height.
- A radial distribution of ONE instance with a routed `distribution/startAngle` places a single object at any angle on
  a circle (the void's beacon); a short spiral spline with a routed `startAngle` does the same for an emitter.
- `notes.polyphony` is the number of sounding notes / 8.
- Beyond `volumeMaxDistance` the SAME density is analytic surface fog on every surface (not on the sky): a far object
  takes the fog colour while the sky behind it keeps its gradient. For a distant silhouette, keep the density tiny.
- The sky mixes zenith and horizon LINEARLY: 25% of a bright amber (red 1.0) swamps a dim teal (0.02) into warm grey.
  A narrow `haze` confines the horizon colour.
- A procedural box's size is at most 1000 m: widen it with the transform's scale.
- In the corridor, the SDF's `localPosition` is the object's frame, before its internal `translate` nodes: a colour
  computed from it stays put while the geometry travels through it.
- The route liveness audit calls `audio.*` and `beat.*` routes dead in a project with no audio file; live input
  publishes them (`Engine::publishFrame`). Those verdicts are expected for live projects.

## Log

- 18:00 read the brief, the engine docs (procedural geometry, materials, SDF, splines, particles, post, temporal), the
  rejected passes' stills; wrote `ABSTRACT-PLAN.md`.
- 18:10-18:30 Sacred Geometry blockouts v1-v5; Neon Vector v1-v2 (the field deformer's `alongNormal`).
- 18:40 the owner's decision: Sonic Abstract is its own project. Moved the prototypes to `examples/sonic-abstract/`,
  restored the Sonic VFX tooling defaults, added `scenes.ABSTRACT`, `kit.ABSTRACT_DIR`, `abstract.py build` and
  `live.py --abstract`.

- 2026-10-02 22:00-22:45 the owner's corrections (05-08): removed Impossible Architecture, Cinematic Void, Particle World
  and the flat Sacred Geometry (`fc6f8030`); read the four briefs; designed the five new prototypes.
- 22:45-23:10 Sacred Geometry Flight v1-v8: off-axis tunnel (busy), then on the axis with star and rose gates (the
  templates must stay visible), the end light as geometry; committed `d29aad8d`.
- 23:10-23:40 Digital Alpine: boxes (a pile of slabs), then generated faceted meshes (`meshes.py`), twins, the sky dome's
  mirrored sky; found the ±1000 clamp on op constants; committed `624904a0`.
- 23:40-23:50 Chromatic Topography: periodic heightfield, seven bands, contour lines, a glide that rises; `25e28b68`.
- 23:50-00:05 Glitch Signal: pixel canyon, the staged collapse; the trigger measured with a signal meter (level never
  falls below the cold intensity follower; kick envelope x flux above 0.62 is the strong event); `5f6c986a`.
- 00:05-00:25 8-Bit Ocean: voxel level on a ring, three areas; the radial angle convention; outlines; `f043d4c6`.
- 00:30 `ABSTRACT-PLAN.md` rewritten for the nine (designs and modulation maps), notes for the nine, the index rebuilt.
