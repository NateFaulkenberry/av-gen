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

- **State (2026-10-02 20:15, after a session restart):** all eight modules exist with their instruments (routes
  audited clean). Two blockout rounds reviewed; round 2's verdicts and fixes:
  - **strong:** 1 Sacred Geometry, 6 Particle World and 7 Impossible Architecture (busy);
  - **2 Neon Vector:** the far rows merged into a band, so they open out with distance and the sweep rests beyond the
    plain;
  - **4 Color Geometry:** thin outlines became monumental mitred frames in a luminous cream haze;
  - **5 Organic Garden:** the floor's hard horizon is gone (a 900 m floor), and the voice buds rest closed;
  - **3 Cel Dream:** recomposed (a chain of islands fading into pink haze, a ringed planet, a sea of clouds);
  - **8 Cinematic Void:** the air no longer floods (a 70 m march), and the mirror is built (a backdrop with the sky's
    mirrored gradient, a twin sun).

  Next: round 3 stills with the new pin (one queued job), then the 1080p silent stills, the real-music and MIDI clips,
  perf, the live runs (the switcher now steps through the Sonic Abstract set, ADR-1074), the contact sheet, the notes,
  the maps, the tour and the report.
- **The scratchpad is WIPED when a session restarts** (it happened 2026-10-02 ~20:00): the pin and `avgen.sh` must be
  rebuilt (steps below). `review.py` now refuses to render without the pin. Before 20:04 it silently fell back to
  `build/release` once.
- **The pin is `da96635e`** (the engineer's cel lighting ADR-1071, outline ADR-1072, wire lines ADR-1073, the per-set
  switcher ADR-1074), at `$S/vfx/bin-da96635e`; `$S/vfx/avgen.sh` points at it.
- **Build:** `python3 tools/sonic_vfx/abstract.py build [module ...]` writes `examples/sonic-abstract/<id>.json` and
  `.scene.json`. With no module it builds all of `scenes.ABSTRACT` and rewrites the index's "Sonic Abstract" entries.
  For look development elsewhere, use `--projects DIR`.
- **Check without the GPU:** `$S/vfx/avgen.sh --project examples/sonic-abstract/<id>.json --audit-routes out.json 2>&1 |
  grep -iE "warn|error|skipped"`. It must print nothing.
- **Review** (every render goes through `tools/gpu-lock.sh` with the pin):
  - `abstract.py blockout <id> [--tag v2]` gives the silent still at 960x540 in `28-sonic-abstract/work/blockout/`;
  - `silent <id>` gives the 1080p silent still `NN-<id>-still.png`;
  - `clip <id> --class allyougot|rebuild|full` gives the reactive clip `NN-<id>-<class>.mp4`;
  - `frame`, `sheet [--blockouts]`, and `music` (cuts the real-music excerpts into the gitignored `assets/audio`).
- **Live:** `python3 tools/sonic_vfx/live.py <id> --abstract --scenario demo` runs one prototype live. The switcher
  steps through the set the open project belongs to (ADR-1074): Sonic Abstract's eight, program n = scene n mod 8.
- **The pinned engine:** `da96635e` at `$S/vfx/bin-da96635e`, through `$S/vfx/avgen.sh` (a three-line wrapper that sets
  `AVGEN_SHADER_DIR` to the pin's shaders and runs its `avgen`; recreate it after a wipe). `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  Never render art from `build/release` (the engineer's). To re-pin when the engineer posts a ready note:
  1. `git archive <sha> | tar -x -C $S/vfx/src-<sha>`;
  2. `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`;
  3. build `avgen avgen_sonic_probe`;
  4. copy the binaries and `shaders/` into `$S/vfx/bin-<sha>`;
  5. point `avgen.sh` at it.
- **GPU:** every job goes through `tools/gpu-lock.sh`, one job per hold (shared with the engineer; the Liminal agent
  is done). No `timeout` command. `abstract.py stills` renders several silent stills in ONE queued job.
- **Modulation maps:** `abstract.py maps [id ...]` writes `NN-<id>-modulation.md` from the built project (every route by
  audio dimension, `[S]` for structural targets) plus the scene's plain-words vocabulary.
- **Real music:** 30 s excerpts of the owner's tracks (Desktop: `All You Got.wav` from 34 s, `Rebuild.mp3` from 156 s),
  cut by `abstract.py music` into `assets/audio/sonic-abstract-{allyougot,rebuild}.wav`. Classes `allyougot` and
  `rebuild` in `review.REAL_MUSIC`.

## Engine needs (for the coordinator)

1. **An output letterbox** (`post/output/letterbox`, an aspect such as 2.39): the Cinematic Void draws its bars as
   black geometry riding a straight camera push on the same track, which breaks under any turn, shake or zoom.
2. **A camera roll parameter** (`camera/roll`): the Impossible Architecture rolls the world by turning the SDF object,
   which also turns its light relationship; a true roll would keep the light fixed to the world.
3. **Translucent procedurals** (Blend draws opaque): lines cannot fade out against what is behind them, only into a
   matching colour (the Sacred Geometry lattice takes the halo's colour to fade).
4. **A per-instance phase for deformers** (a wave travelling across a distribution's instances): a field of slabs
   flipping in a stadium wave needs one node per row today.
5. **A BUG: a small uniform scale pops an object back to full size.** `Transform::fromMatrix` (`src/scene/scene.cpp`)
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

## Engine facts learned this pass

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
