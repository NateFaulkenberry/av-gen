# The Astral Forge: Iteration 2

The owner approved the "recommended next iteration" of `05-tests-and-assessment.md`. The rules are unchanged:

- no architecture;
- a black void;
- the entity emerges from the field;
- metal plus light;
- the greyscale check.

The track is *Trench* (`assets/audio/trench.wav`). Media is in `~/Desktop/av-gen-review/37-astral-forge/iter2/`:

- `clips/`: T01, T03, T05, the new T07, and T06 with Trench;
- `stills/`, each with a `-grey` copy;
- `sheets/`: one frame per second, colour and grey;
- `dev/`: the before/after comparisons quoted below;
- `production/`: step 7's example renders.

Every change can be switched off from the command line for before/after comparisons:

| Flag | Effect |
|---|---|
| `--no-advect` | step 1 off |
| `--no-cache` | step 4 off |
| `--no-shards` | step 3 off |
| `--iter1` | all three off, plus the old march step |
| `--arch N` | substitutes an archetype into any scripted test |
| `--cache-frame`, `--min-step`, `--shard-px` | tuning |

## Step 1: matter is carried through the warp (TEST 03 folds instead of tearing)

**What was wrong.** In iteration 1, the folds moved the latent anatomy and the matter was only re-attracted
to it. When a fold moved faster than the springs could follow, the surface turned to dust in transit. The
sphere inversion was worse: for strengths above about 0.3, the blended inversion is not injective, so
space folds over itself and *no* motion of matter can follow it.

**What was built.**

- **Advection.** Each step, a bound particle solves `warp_t(p') = warp_{t−dt}(p)` with two Newton
  iterations on a numerical Jacobian (`sim.wgsl`) and moves there, scaled by its binding. A fold now
  *carries* the matter. It runs only while the warp is changing.
- **An injective "fold inward".** A spiral implosion replaces the inversion: the latent domain expands by
  `1 + 3·inv·e^{−r²/6.25}` and twists by `2.6·inv·e^{−r²/6.25}` about a point behind the eyes. It is
  monotone in r for inv < 0.75, so matter can follow it.

**Result** (`dev/t3-adv-sheet.png` against `dev/t3-noadv-sheet.png`, and `clips/t03.mp4`).

- The twist, the receding eye and the tunnel mouth now stay whole.
- At the climax the Seraph **spirals into itself like a galaxy of engraved metal** (`stills/t03-b-spiral-implosion`).
- It then unwinds back into the face (`t03-c-unwinding`, `t03-d-restored`).
- What was a tear in iteration 1 now reads as a fold.

**Verdict: PASS.** The brief's "a wing passes through itself" is still not literally achieved: the
implosion folds the whole form inward, not one limb through another.

## Step 2: meso-scale tendons replace the whiskers and the mid-coherence smoke

**What was built.** A new particle role, about 16% of the matter (`sim.wgsl`, `latent.wgsl` "TENDONS"), on
**18 skeleton curves**:

- the brows, cheek lines, nasolabial lines, jaw arc, forehead midline and temples, lying on the mask's front
  surface;
- eight outer tendons that leave the rim *backward* and curl into the field.

Each tendon particle is attracted to one curve, chosen by its index and a *generation* counter. It
**streams along the curve** at a speed set by the mid band. At the curve's end it sprays off into the field,
and its generation advances, so it re-joins another curve. The curve projection (four 12-segment walks) is
refreshed every third step, staggered by particle block. The whisker appendages were deleted.

**Result** (`dev/t1-tendon-sheet.png`, `stills/t01-a/b`).

- In the forming phase, the face is *drawn by streams of flakes* along its anatomical lines before any
  surface exists.
- The mid-coherence fog now has direction and structure.
- From the front, the outer tendons read as hair-fine strands receding behind the mask, not as insect legs.

**Verdict: PASS.**

**Unexpected finding.** The tendons first cost 19 ms of simulation, because roles were per particle, so
every SIMD group paid for both the tendon path and the latent path. Assigning roles **per block of 64
particles** cut the simulation to **4-8 ms** with the tendons included, below iteration 1's 8.8 ms without
them.

## Step 3: near flakes become real lit shards

**What was built** (`flakes.wgsl` → `shards.wgsl`, an indirect draw). A flake that is *bound*, more than
12 px wide on screen, and *resting on the surface* is drawn as geometry. "On the surface" means within a
thin shell around the ray-marched depth. Only a quarter of such flakes qualify, thinning further as they
grow.

Each becomes an irregular sliver plate:

- four uneven corners, stretched;
- fine wavy grooves across it;
- a bevelled rim that turns outward and catches the strips;
- lit by the strips only, with no soft fill, so it is dark unless it catches a light.

Two first versions failed and are recorded here:

- **Lit by the soft fill**, the shards read as grey paper confetti (`dev/s6-review.png`).
- **Every near flake as a shard** turned a MICRO shot inside the field into a blizzard
  (`stills/t05-c-inner`).

**Result** (`dev/s7-review.png` right, `dev/s8-review.png`). MICRO frames now show dark engraved debris
resting above the engraved surface, which gives parallax and proves the surface is made of matter.

**Verdict: PASS, modest.** They are sparse by design.

## Step 4: the cached latent volume (measured)

**What was built.**

- Once per frame, the full latent is evaluated into **two 128³ clipmap levels**, but only in blocks that
  hold matter:
  - level 0 is **framed on the shot**: centred on the camera target, sized to the view distance;
  - level 1 covers the whole density box.
- The surface march steps on the cache.
- The exact latent is used for:
  - the last two bisection steps;
  - the normal (three exact evaluations, a forward difference at a zero-set point);
  - any point within 0.75 texels of the cached surface.

**A correctness lesson.** The first version marched on a single 128³ cache over the whole box, with
0.16-unit texels. It *silently deleted* every feature smaller than about 0.3 units: the mask's inner face
and the teeth (`dev/t5-inner-cmp.png`, top right). The march stepped over them before the exact bisection
could ever see them. **A cache must be a bound, not the field.** That is why the framed level and the
exact band exist.

**Other performance work** in the same step:

| Change | Effect |
|---|---|
| Five of seven bisection steps on the cache | |
| One warp Jacobian shared by the three engraving families | 12 → 4 warp evaluations |
| The density blur through a 6³ workgroup tile | 2.1 → 1.6 ms |
| Flake streaks at 2 px spacing, at most 8 samples | |
| Block-coherent roles | step 2 |

**Measured:** see "Performance, before and after" below.

## Step 5: conductor vocabulary

- **Withhold.** A section labelled break, other, bridge, intro or outro, or one that is sparse and quiet,
  becomes a *withhold*.
  - Chaos is held at C ≈ 0.2, where only eye matter binds.
  - Snare flashes are the only time the face appears, at 1.6× the usual flash.
  - In the last four beats the god **surges** to C ≈ 0.96, so the next downbeat has something to destroy.
  - On *Trench* the break (86.25-94.61 s) is a held pair of eyes in dust (`stills/t06-b-withhold-eyes`). It
    surges at 94 s (`t06-c-surge`), and the chorus downbeat collapses it.
- **Camera vocabulary.** There are now seven behaviours, and each belongs to its phrase, so none switches
  mid-phrase:
  - build phrases use OBSERVER, PROFILE or LOW;
  - held phrases use DESCENT, ORBIT or MICRO;
  - withheld sections use HOVER on the eyes;
  - REVEAL at a section change, and COLLISION on a kick-opened collapse;
  - the side alternates by phrase.
- **Strobe and sparks.** The collapse strobe decays with a 45 ms constant (it is gone in about 0.15 s), and
  sparks have a 0.23 s half-life (was 0.43 s).
- **No global flicker.** Iteration 2's first T06 cut brightened every light band on the hats. The Critic
  measured it ("the whole frame pulses with the audio": 71% of regions brightening together about 300 ms
  after onsets). The hats now only shimmer the flakes. The clip was re-rendered after this fix (see below).

## Step 6: the Machine God and the Chimera

- **Machine God.** It is faceted *everywhere*: a true 24-plane faceted mask (the max of plane distances over
  a slowly turning Fibonacci set, with crisp flat faces) replaces iteration 1's 6-norm, which only looked
  chamfered. It sits inside counter-rotating toothed gyro orbits and a radial blade fan. It has the coldest
  temper (silver) and the tightest grating, so it is the most holographic.
- **Chimera.** Three faces at 120° around a shared mass, with no front, and horns that curl through the
  faces.
- **TEST 07 (new), "The Two Gods"** (`clips/t07.mp4`): one continuous 400° orbit. The Machine God assembles
  inside its orbits, collapses at 8.25 s, and the Chimera forms from its dust while the camera keeps
  circling and discovers each face in turn. Critic (preview): **1.00**, two low findings.
- **In TEST 06** each kind of section now owns a *pair* of gods that alternate by occurrence. On *Trench*
  the second verse brings the **Machine God** (66.5-86 s) and the second chorus the **Chimera** (104.8 s
  on).

## Step 7: the production path (ADR-1140 to ADR-1143)

This was gated on steps 1-6, which landed. `origin/main` had merged productionization and dropped
PHONOTAXIS, so it was merged into this branch first (`ede16714`). Two engineering agents built the four ADRs:
ADR-1140-1142 on this branch, and ADR-1143 on `proto/astral-forge-material` in a separate worktree, merged
here in `2611fc76`. Each ADR is in `docs/decisions/`, and the range row `1140-1159 | proto/astral-forge` is in
the ADR index.

| ADR | What it adds to the engine | Scene JSON | Parameters |
|---|---|---|---|
| **1140** latent SDF force on GPU particles | a `cs_latent` step: binding by coherence (per-particle threshold), spring to the SDF projection, tangential flow, release impulse. It packs its own copy of the named SDF tree, so it does not bind SdfRenderer's buffer, and it is refused at load when the name is missing, the wrong kind, or uninterpretable. This answers ADR-704 | `particles[].latent {sdf, coherence, width, strength, flow, release}` | `particles/<n>/latent/{coherence,strength,flow,release,width}` |
| **1141** render-transient particle density volume | u32 fixed-point splat, then a tiled 3³ blur, into an rgba16float 3-D texture. It is **the engine's first 3-D texture**. It is owned by the particle renderer and is never a `Simulation` grid, so ADR-360's relaxed seek stays contained. Resolutions above 256³ are refused | `particles[].density {boundsMin, boundsMax, resolution, weight}` | `particles/<n>/density/weight` |
| **1142** SDF "density iso ⊕ SDF" mode | an SDF object draws a particle system's density iso-surface, sharpened toward its own tree (the prototype's `fieldAt`). It is a **pipeline variant** spliced between new markers. The first version, a uniform branch, moved two shipped compiled trees by 5,192 channels; the variant leaves them byte-identical | `sdf.density {particles, iso, sharpness}` | `sdf/<n>/density/{iso,sharpness}` |
| **1143** thin film and anisotropy in `pbr_shade` | Airy thin-film tint on f0 (16 wavelengths, CIE fit, 0 nm exactly neutral) for every light and the IBL; Kulla-Conty anisotropic GGX with a tangent from the normal map or the object's +Y, turned by `rotation`. `kObjectStride` grows 512 → 768 (one new `optics` lane). The path tracer reports both as Degraded | `material.thinFilm {thickness, ior}`, `material.anisotropy {strength, rotation}` on procedural, SDF, terrain and orb materials | `procedural/<n>/material/thinFilm/*`, `.../anisotropy/*`, and the same under `sdf/<n>/` |

**Physics correction from ADR-1143.** With a film ior of 2.4, the temper sequence straw → bronze → purple → blue
occurs at about **20-90 nm**, not the 150-350 nm that `01-research.md` implied. 150-350 nm is the second and third
repeat of the colour cycle. The prototype's `thickness = temper × (330 + …)` therefore sweeps several cycles,
which is why its colours change faster than real temper. A production scene should drive `thickness` over
about 20-90 nm.

**Reach.**

- **Scene JSON:** read and write round-trips, and unknown keys fail the load and name the key.
- **CLI:** an offline render of `examples/astral-forge/latent-entity.json` uses all four features in one frame:
  `avgen --project … --render <dir> --range 4:4 --particle-warmup 240`, avgen's own exit code 0
  (`iter2/production/all-four-adrs-cli.png`).
- **Editor:** the parameters are in the registry the inspector reads, and `--save-project` writes them. Nobody
  has *seen* the ImGui panels; that is inferred, not observed.
- `examples/astral-forge/tempered-metal.scene.json` shows ADR-1143 alone (`iter2/production/adr1143-tempered-metal.png`).

**Tests.**

- ADR-1140-1142: 16 unit cases and 7 GPU cases (`[gpu][particles][latent]`, `[density]`, `[sdf][density]`):
  - convergence onto a sphere at coherence 1, and none at 0;
  - GPU = CPU reference within 1e-3;
  - deterministic buffers;
  - density = CPU splat within one fixed-point quantum;
  - surface only where matter is;
  - byte-identical when absent.
- ADR-1143: 7 unit cases and 4 GPU cases:
  - byte-identical defaults (0 differing bytes for absent vs explicit zero);
  - a 300 nm film matches the CPU tint within 4%;
  - anisotropy aspect ratio > 1.5 at 0.8, < 1.15 at 0.

**Measured cost** (the agents' measurements, M2 Max):

| Measurement | Cost |
|---|---|
| latent force, 1M particles | +6 ms. The prototype's every-third-step projection was not ported: the 64-byte particle record has no room for the stored target |
| density pass, 128³ / 192³ / 256³ | 0.59 / 0.92 / 1.57 ms |
| density-mode raymarch, 1080p | 7.9 ms. There is no empty-space skipping yet; the prototype's coarse occupancy grid is not ported |
| thin film + anisotropy | under ~0.6 ms on a quarter-frame of SDF spheres |

**Honest state of the production path.**

- **It is functionally complete.** The engine can now make an entity from matter: a particle system bound to an
  SDF, its density drawn as a surface sharpened toward the SDF, in tempered anisotropic metal, all from scene
  JSON.
- **It is not art-complete.** The production still shows density banding and voxel blockiness on the surface.
- **It lacks the prototype's** occupancy skipping, latent cache, staggered projection, guilloché engraving,
  reflection-only light bands, flakes-as-glints and shards.
- **The next production steps**, in order:
  1. port the coarse occupancy grid into ADR-1142's march;
  2. a stored-target lane for staggered projection (it needs a second particle buffer, and the storage-slot
     budget is now spent: ADR-1140 used the last one);
  3. a line-field material op for engraving;
  4. reflection-only bands as an environment term.

**Suites** on the merged branch (both features together): see the end of this file.

## Performance, before and after

M2 Max, 1920×1080, under the lock, p50 / p90 of 240 frames, all sharing the GPU with other agents' work.
Raw data: `bench-iter2.txt`.

- "Iteration 1" is the shipped iteration-1 number from `05-tests-and-assessment.md`.
- "Steps off" is iteration-2 code run with `--iter1`: no cache, no advection, no shards, the old march step.
  It isolates step 4, but it keeps the block-coherent roles and the tendons.

| Moment | Iteration 1 | Steps off | **Iteration 2, 2M** | **Iteration 2, 1.75M** |
|---|---|---|---|---|
| T01 formed face (mid shot) | 15.8 / 19.5 | | **13.4 / 15.9** | **11.9 / 13.8** |
| T02 organism, close | 23.8 / 26.8 | 22.2 / 24.8 | 20.3 / 22.2 | 19.1 / 21.0 |
| T03 fold, close | 30.1 / 45.4 | 30.6 / 32.1 | 23.3 / 28.3 | 21.8 / 26.2 |
| T04 choir lattice | 24.5 / 30.9 | 24.5 / 27.1 | 21.6 / 24.2 | 19.5 / 22.5 |
| T05 MICRO engraving | | 26.6 / 48.8 | 23.9 / 42.9 | 22.5 / 40.4 |
| T05 giant reveal | 21.0 / 31.3 | | **14.7** / 22.5 | |
| T05 inside the mouth (extreme MICRO) | | | 38.9 / 42.4 | 35.3 / 35.6 |
| T06 Trench, formed god | 24.1 / 25.0 | | **17.2** / 23.2 | **15.5** / 22.5 |
| T07 Machine God, orbit | (new) | | **11.6** / 22.5 | |
| T07 Chimera, close | (new) | | 22.9 / 23.5 | 22.0 / 22.5 |

**What the numbers say.**

- **The target, 1080p60 everywhere, is not met.**
- It is met for every mid and wide shot, which is where the entity is seen as a whole: T01, T05 reveal, T06,
  T07 orbit, at 11.6-17.2 ms. They are under 16.7 ms at p50 with 1.75M particles.
- Close-ups run at **19-24 ms (42-52 fps)**.
- The extreme MICRO inside the mouth runs at **35-39 ms**.
- The simulation is now 3.2-7.7 ms, against iteration 1's 8.8 ms, despite the tendons; that is the
  block-coherent roles.
- **The remaining cost is the surface pass in close-ups (9-14 ms, 26-29 ms inside the mouth).** It is the
  exact latent evaluated in the band the cache cannot resolve, plus the 3-evaluation normal. The cache halved
  it, and making the cache correct gave part of that back.
- The next lever is a **half-resolution march with full-resolution shading on the reconstructed depth** (the
  shading itself measured only ~2 ms), or a third clipmap level. Both are recorded for iteration 3.

**Shard cost** is under 0.1 ms: they are sparse. **Latent cache** cost is 0.13-1.64 ms (both levels).

## Iteration-2 clips and the Critic

| Clip | Contents | Critic (preview) |
|---|---|---|
| `clips/t01.mp4` | chaos → eyes → tendons drawing the face → precise mask → collapse | (iteration 1: 0.99) |
| `clips/t03.mp4` | fold with advection: twist, receding eye, tunnel, spiral implosion, unwind | |
| `clips/t05.mp4` | engraving → face → giant ghost mask (the entity is its eye) → mouth portal → inner face with debris | |
| `clips/t06-audio-trench.mp4` | *Trench* 66.5-114.5 s: Machine God (verse 2) → withheld break (eyes in dust) → surge → chorus collapse → Choir → Chimera | **0.96**, musical sync **1.0**, audio RMS vs visual change r = 0.48 |
| `clips/t07.mp4` | The Two Gods: one 400° orbit | **1.00** |

- On T06 the iteration-2 Critic run first measured "the whole frame pulses with the audio" (the hat
  flicker). After the fix, that finding is gone, and musical sync went from 0.98 back to 1.0.
- What remains is **"repeated composition"** (6 medium findings): the entity is always centred on black, so
  shots look alike to a layout metric. That is fair. A void with one entity needs off-centre framing and scale
  changes to keep compositions distinct.

**Greyscale.** Every iteration-2 still holds in greyscale (`stills/_contact-grey.png`).

**Architecture check.** Nothing reads as a building. The Machine God's gyro rings and blade fan are the
nearest thing to a "constructed object", and they read as an instrument or orrery, not a structure.

## What still looks generic

1. **The Machine God's gyro rings** are the most familiar image in the set: "a sci-fi gyroscope around a
   head". They work, but they are the least original element.
2. **Centred framing.** It is the Critic's repeated-composition finding: every god sits in the middle of the
   frame.
3. **Mid-coherence dust in very wide shots** still drifts toward glitter-fog at times (T06 at 96-99 s), though
   the tendons fixed most of it.
4. **The production path's look** (`iter2/production/`) shows density banding and blockiness. It has none of
   the prototype's engraving, light bands, glints or shards yet, so it reads as "a blobby chrome mask". It is
   functionally correct and visually behind the prototype.
5. **The collapse spark colour** (molten orange over the whole cloud for a frame, `stills/t07-c-collapse`) is
   close to the banned "fire ball" in the frame where it peaks.

## Suites on the merged branch (both features together)

Run one after the other under `tools/gpu-lock.sh` (`AVGEN_GPU_LOCK_TIMEOUT=14400`), with the binaries' own
exit codes captured in the same shell, after a CMake reconfigure, so the merged test files are compiled:

| Suite | Exit code | Cases | Assertions |
|---|---|---|---|
| `avgen_tests` | **0** | 4152: 4132 passed, 19 skipped, **1 failed as expected** (the one `[!shouldfail]`) | 10,333,697 |
| `avgen_render_tests` | **0** | 613: 612 passed, 1 skipped | 642,819 |

4152 = the ADR-1140-1142 branch's 4145 + ADR-1143's 7 unit cases. 613 = 609 + ADR-1143's 4 GPU cases. So the
merged tests ran.

## Recommended iteration 3

1. **Close-up performance:**
   - a half-resolution march with full-resolution shading;
   - or a third, finer clipmap level;
   - and the staggered projection plus occupancy skipping ported into the production path.
2. **Port the look into production:**
   - engraving as a line-field material op;
   - reflection-only bands as an environment term;
   - glint flakes in the particle renderer.

   Production then becomes able to render the prototype's frames, and the prototype can retire.
3. **Composition:** off-centre framing, scale cuts, and a second, distant entity in the meta field for
   counterpoint.
4. **Replace the Machine God's rings** with something less familiar (for example, engraved bands that are
   part of its face and unwind on the beat).
5. **Collapse colour:** confine heat to the release front and cool it within 100 ms.
