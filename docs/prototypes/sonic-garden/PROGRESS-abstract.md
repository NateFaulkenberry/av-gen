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

- **State (2026-10-02 18:45):** the plan is written. Two prototypes are blocked out: 1 Sacred Geometry (v5 is strong:
  a gold mandala on an ultramarine halo, with a flower-of-life field and a spiral well) and 2 Neon Vector (v3
  rendering). Next: the other six blockouts, then routes and iteration on the weakest.
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
  steps through the Sonic VFX set only; the engineer is extending it to the open project's set.
- **The pinned engine:** `96bc0214` at `$S/vfx/bin-96bc0214`, through `$S/vfx/avgen.sh` (sets `AVGEN_SHADER_DIR` to
  the pin's shaders). `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  Never render art from `build/release` (the engineer's). To re-pin when the engineer posts a ready note:
  1. `git archive <sha> | tar -x -C $S/vfx/src-<sha>`;
  2. `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`;
  3. build `avgen avgen_sonic_probe`;
  4. copy the binaries and `shaders/` into `$S/vfx/bin-<sha>`;
  5. point `avgen.sh` at it.
- **GPU:** every job goes through `tools/gpu-lock.sh`, one job per hold (shared with the Liminal art agent and the
  engineer). No `timeout` command.
- **Real music:** 30 s excerpts of the owner's tracks (Desktop: `All You Got.wav` from 34 s, `Rebuild.mp3` from 156 s),
  cut by `abstract.py music` into `assets/audio/sonic-abstract-{allyougot,rebuild}.wav`. Classes `allyougot` and
  `rebuild` in `review.REAL_MUSIC`.

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

## Log

- 18:00 read the brief, the engine docs (procedural geometry, materials, SDF, splines, particles, post, temporal), the
  rejected passes' stills; wrote `ABSTRACT-PLAN.md`.
- 18:10-18:30 Sacred Geometry blockouts v1-v5; Neon Vector v1-v2 (the field deformer's `alongNormal`).
- 18:40 the owner's decision: Sonic Abstract is its own project. Moved the prototypes to `examples/sonic-abstract/`,
  restored the Sonic VFX tooling defaults, added `scenes.ABSTRACT`, `kit.ABSTRACT_DIR`, `abstract.py build` and
  `live.py --abstract`.
