# The Astral Forge: Iteration 4

> **SHELVED 2026-10-06: how to resume.**
>
> The owner: "shelve it for now and come back to it later... leave the engine version where it is for now;
> maybe we'll try to make that version cooler in the future."
>
> **The prototype** (`prototypes/astral-forge/`, behind `-DAVGEN_ASTRAL_FORGE_PROTOTYPE=ON`, off by default)
> is the reference. It is the full v2 look and conductor:
>
> - `--test 8` renders the whole of *Trench*, which goes in `assets/audio/trench.wav` (gitignored);
> - the shaders are read from the source tree;
> - it runs at about 60 fps on half the frames (section 3).
>
> **Production** (ADR-1140..1156) has the prototype's face anatomy as a compiled 90-node SDF, with tendons,
> shards, heat, per-region temper and warped engraving. It runs in `examples/astral-forge/compare-*.scene.json`.
> It reads like the prototype on a formed T01 face, but it is **about 9× slower** there (110.5 ms against
> 12 ms), and it has none of v2's look (legibility, palettes, atmosphere). It stays as it is for now.
>
> **The gap list** is section 7: cost, latent remainders, look differences, v2's look, the conductor and the art
> weaknesses. Section 5 holds the conductor against production's scene states.
>
> **Suggested resume path (coordinator):**
>
> 1. In the engine, first build **a custom-SDF-code node plus a hero fast path**: a latent cache (clipmaps) and a
>    half-resolution march with full-resolution refine and shade, as in prototype iteration 3. Then **measure**
>    against the prototype's 12 ms at the formed face.
> 2. The fallback, if that cannot close the gap: a native **"hero entity" node** that wraps the prototype's
>    pipeline whole.
> 3. Flake drawing (66 of the 110.5 ms) needs a compute splat or per-particle shading in either path.

The owner approved iteration 4 in three steps:

1. rebalance the camera;
2. give production the prototype's sculpted face;
3. port the rest of the look, then say plainly whether the prototype can retire.

The deliverable is the **full length of *Trench*** (207.6 s) at 1080p with sound. After watching that piece
(v1), the owner asked for a second pass (v2):

- the face too abstract;
- the face too far away;
- the colour gone.

Media is in `~/Desktop/av-gen-review/37-astral-forge/iter4/` (v1) and `iter4/v2/`.

## 1. The camera (v1, `01c00890`)

- Close-ups are now accents only:
  - the last 2.5 beats of a build phrase before a kick-opened collapse;
  - one held phrase in six.
- Everything else is mid or wide, off-centre on a third. Camera smoothing never crosses a shot.
- Long sections (over 40 s) alternate their god every four phrases.
- **TEST 08** is the whole song: a 3 s fade-in, the conductor over every section, a 7 s pull-back and a 2.2 s fade.
- `--perf-csv` writes per-frame GPU times for the full-song bench.
- The containment shell read as a glowing sphere in wide shots, so it is gone. A wider, softer meta field
  and a noisy soft containment took its place (`54edf049`).
- Heat now lives only on the front of the **most recent** collapse. Before, a soft phrase with no collapse
  time heated the whole cloud orange.

## 2. v2: the owner's five points (`22756a29`, `bf3d4e56`)

| Point | What changed |
|---|---|
| Shot sizes | The conductor frames by **projected fill**, the face's 6.8-unit anatomical height over the frame height: `d = 12.69 / fill` at 30°. **Build phrases** push from 0.27 to 0.52. **Held phrases** are portraits at 0.5 ± 0.06, with one in eight wide at 0.15, and cut at the midpoint to another angle. **Accents** are 0.75-0.78. **Section starts** reveal toward 0.12, and a withhold hovers at 0.45. `--debug 8` prints the fill for every frame. |
| Legible faces | **Coherence:** build phrases now peak by 60% of the phrase, so it holds ≥ 0.8 for several seconds before a collapse. **The choir** merges into one face at every peak. **At a formed peak** (`legible`, which rises from coherence 0.82 to 0.95): the engraving is 70% quieter; a broad frontal-high key lights the macro normal; cavity occlusion from the latent anatomy darkens the sockets, under the nose and the mouth; the sockets darken around small, restrained pupils; bound flakes over the face dim by 80%; the Machine God's lamellae withdraw **behind** the face as a stepped halo. **Dust:** 80% of loose dust is drawn into the face as coherence rises, and what is left dims. |
| Colour at distance | **Per-god palettes**, each with a key, rim, eye glow, two broad temper zones (features and elsewhere) and a fog tint, all normalised to unit luminance so the grey read is unchanged. **Seraph:** silver and cyan, a violet rim, cyan eyes. **Abyss:** violet and deep red. **Chimera:** emerald and bronze. **Machine God:** gold and gunmetal. **Choir:** rose-copper and teal. The zones also tint the form key, so colour comes from broad areas and not from glints. |
| Silhouettes | **Seraph:** narrow and long, with wings. **Abyss:** sheared and lopsided, its mouth falling inward. **Chimera:** three faces, set further apart. **Machine God:** faceted, with a stepped halo. **Choir:** a skin of small faces that merges at peaks. |
| Background | A palette-tinted glow around an off-screen source above and behind the god, a faint vertical falloff, and screen-space **god rays** (48 → 20 jittered taps) occluded by the surface and by dense matter. The rays cut through the gaps in forming matter. There is no ground, horizon or architecture, and the void stays near-black. |

## 3. Measurements over the full v2 render

All in `iter4/v2/measurements-v2.txt`. The v1 baselines use the same rules on the v1 mp4.

| Measure | v1 | v2 | Target |
|---|---|---|---|
| Shot sizes by projected fill (every frame) | mid/wide-led, close-ups ~1% (not measured by fill) | **close 5.3%, portrait 49.1%, medium-wide 29.1%, wide 16.4%** | 5 / 50 / 30 / 15 |
| Face detected on phrase-peak frames (YuNet, multi-scale grey; C ≥ 0.9, not wide, every 0.5 s; 186 frames) | 4.3% at ≥ 0.3; 0.5% at ≥ 0.5 | **41.9% at ≥ 0.3; 26.3% at ≥ 0.5** | most |
| Mid-range colourfulness (Hasler-Süsstrunk on the entity mask, fill 0.2-0.5, every 2 s) | mean 7.1 | **mean 12.1, median 11.0** (10% of frames ≥ 16.9) | ≥ 16.9 |
| Greyscale read (entity p95/p5 luminance, same frames) | – | 11.8 | holds |
| Frames under 16.7 ms (12,449 frames, M2 Max, 1080p) | 64.7% (p50 15.4) | **50.4% (p50 16.6, p90 23.3)** | – |

**Face detection by god:**

| God | Detected at ≥ 0.3 | Detected at ≥ 0.5 |
|---|---|---|
| Machine God | 69% | 54% |
| Seraph | 47% | 33% |
| Choir | 31% | 18% |
| Chimera | 21% | 0% |

Chimera shows one eye per sector, which a photographic detector does not accept.

**Notes:**

- **Detection is a floor.** YuNet is trained on photographs, so it undercounts stylised masks. The tenfold rise
  is the meaningful number.
- **Colour.**
  - The colour peaks reach 17-21 (the Seraph and Machine God peaks). The Choir and Chimera sit at 11-13.
  - The white flakes and the pale collapse dust pull the mean down.
  - The next lever is tinting the bound flakes and the release dust by the zone palette.
- **Frame time.**
  - The atmosphere pass costs about +0.7 ms after the cut to 20 taps.
  - The absorbing dust adds about +0.7 ms in the sim.
  - The Machine God and Chimera sections are the slow ones (32.6% and 39.0% of frames under 16.7 ms).
  - The render used 48 taps (`perf-v2.csv`: 44.4%). The 20-tap version differs by a mean of 1.2-1.5/255.
- **Critic on the full v2 piece** (`iter4/v2/critic/`):
  - Overall 0.94. Musical synchronisation, composition, colour, lighting and creative intent are all 1.0.
  - Motion is 0.69 and cinematography 0.78. There are 14 camera-shake findings (v1: 6), with the largest
    regression in cut02's jitter.
  - As in iteration 3, the estimator reads dust streaming across the frame as camera motion. The camera path is
    smooth within a shot.

## 4. Production: the face and the rest (ADR-1144 to 1149, 1154 to 1156)

These were built by an engineering agent, in commits `84571ff8`, `c329ac82`, `3930c87d`, `28447c18` and
`783ca527`.

**The face.**

- Seven new SDF kinds, for compiled trees only: `ellipsoid`, `taperedCapsule`, `octahedron`, `facet`,
  `blend`, `fray` and `farField` (ADR-1144).
- A latent on a compiled object runs a compiled copy of `cs_latent` (ADR-1145).
- `tools/astral_face.py` writes the prototype's mask as a 90-node tree. At the T01 camera it covers the same
  pixels as the prototype's latent with **IoU 0.98**.

**The rest:**

| ADR | Feature |
|---|---|
| 1146 | Tendons |
| 1147 | Shards |
| 1148 | The heat front |
| 1149 | Per-region temper and polish, plus local sharpness |
| 1154 | Engraving in the tree's warped domain |
| 1155 | Staggered projection |
| 1156 | Alpha flake systems cover |

- Every feature is reachable from scene JSON, has parameters and is tested.
- Each GPU test has a control that must come out the other way.
- Nine reference scenes render **byte-identical**.

**Production against the prototype** (matched luminance; `iter4/production-vs-prototype/`):

| Pair | Hellinger | SSIM | IoU | Colour, prototype / production |
|---|---|---|---|---|
| T01 face | 0.111 | 0.166 | 0.84 | 16.9 / **17.8** (iteration 3: 11.3) |
| T02 metal | 0.090 | 0.150 | 0.65 | 18.4 / 13.2 |
| T01 collapse | 0.099 | 0.052 | 0.77 | 7.7 / 14.1 |

**Cost at the formed face:**

- Production went from 447 ms (iteration 3) to **110.5 ms**. The prototype takes about 12 ms.
- Of production's 110.5 ms:
  - 66 ms is drawing about 3M fully shaded flake billboards, which is vertex-bound;
  - 24 ms is particle compute;
  - 16 ms is marching the exact compiled tree, with no cached latent.

## 5. The conductor against production's state machinery

`origin/main` now carries DIGITAL MOSH's control ADRs:

- 1161: bounded integrals;
- 1164: hold triggers and elapsed-time states;
- 1168: a seek replays the control layer.

About 70% of the conductor then maps onto scene states, routes and timeline tracks. The rest does not.

| Conductor feature | Production today | Gap, and an estimate |
|---|---|---|
| Section-anchored 16-beat phrase clock (the phrase restarts at each section boundary) | The beat grid and sections exist; there is no phrase index or beats-remaining within a section | `section.phrase`, `section.beatsRemaining`, `section.type/occurrence` as sources (~100-150 lines) |
| Build / hold / collapse phrases as **parallel** layers (coherence, camera, palette and god change on their own clocks) | One committed scene state at a time | Parallel state layers, or per-parameter transition overrides (~300-500 lines). **This is the largest gap.** |
| Transitions in beats; "the third phrase of this section" | Transitions in seconds; triggers do not count from state entry | Transitions in beats plus entry-relative trigger counters (~80-120) |
| A collapse opened only by a downbeat kick | Triggers fire on any kick | A window gate on triggers (~50) |
| Route state reset at a cut (smoothing never crosses a shot) | Routes integrate across state changes | An event reset of route state (~60) |
| The look-ahead accent (a close-up in the last beats **before** a collapse) | Not possible causally in live play | A sequencer-level bake, or drop it live |
| Phrase- and bar-quantised camera (DIGITAL MOSH-style shots) | The camera params and spline banking (ADR-1166) exist; rig shots are time-only | Beat-quantised shot selection rides on the phrase clock above |

## 6. Verdict and the owner's decision

**The prototype cannot retire.** For offline T01-style face shots, production now reads like the prototype: the
same anatomy, colour that matches, and every item on iteration 3's retirement list. But it is about 9× slower at
the formed face, and none of v2's work is in it.

**The owner's decision (2026-10-06):** "it still needs work but merge it. I guess there's no point in refining
this one if the engine can't recreate it."

- **Astral Forge art is closed.** v2's conductor and look merge as they are.
- **The prototype stays in the repo** as the reference, behind `-DAVGEN_ASTRAL_FORGE_PROTOTYPE=ON` (off by
  default).
- **Engine work** to make production recreate it is recorded in section 7, to be picked up later.
- **Later the same day the owner shelved Astral Forge** (see the header). What is useful merges; the engine version
  stays as it is.

## 7. What production would need to recreate the prototype

This is the record for later engine work, in rough order of what blocks most.

### 7.1 Cost: about 9× at the formed face

At the T01 formed face, production takes 110.5 ms a frame and the prototype about 12 ms (both on an M2 Max).

| Where production spends it | ms | What the prototype does instead |
|---|---|---|
| Flake drawing: about 3M billboards, 6 fully shaded vertices each (vertex-bound) | 66 | Splats flakes in compute (coverage-weighted, shaded once per particle), with near flakes promoted to a few shards |
| Particle compute | 24 | Same order, after the staggered projection (ADR-1155) |
| Density surface on the exact compiled tree (`sdf` pass) | 16.3 | Marches at half resolution against a **cached latent clipmap** (128³ fine box on the shot plus a coarse box), with the exact latent only within two texels of the surface, then refines and shades at full resolution |

**Needed:**

1. A compute splat for flakes, or per-particle shading.
2. A cached latent clipmap for the density surface.
3. A half-resolution march with full-resolution refine and shade.

### 7.2 The face latent and primitives: done, with these remainders

- **Done:**
  - ADR-1144: `ellipsoid`, `taperedCapsule`, `octahedron`, `facet`, `blend`, `fray` and `farField`, in
    compiled trees only.
  - ADR-1145: a compiled latent.
  - `tools/astral_face.py` writes the mask as a 90-node tree, with IoU 0.98 against the prototype's latent.
- **Not ported:**
  - the small face inside the mouth;
  - the eye rings' wobble;
  - the octahedral eye's 0.9 squash;
  - the fold terms (twist, eye depth, tunnel, inversion, bend);
  - the other gods' latents: the Seraph's wings, the Abyss's shear and tendrils, Chimera's three sectors, the
    Machine God's peeling lamellae and halo, and the Choir's shell of small faces that syncs and merges.
- **No per-part roles in production.** The prototype binds the eyes and mouth first, then the plate, then the
  appendages. Production has an eyes-and-mouth `features` system, but T02's matter distribution differs without
  general per-part roles.

### 7.3 Tendons, shards, heat and per-region temper: ported, with look differences

**Ported**, all reachable from JSON, tested, and byte-identical elsewhere:

| ADR | Feature |
|---|---|
| 1146 | Tendons |
| 1147 | Shards |
| 1148 | The heat front |
| 1149 | Per-region temper and polish, plus `density.spread` |
| 1154 | Engraving in the warped domain |
| 1156 | Alpha flake systems |

**Still different:**

- Production's flakes do not stretch with motion, so its collapse is a grainier, brighter spray with no streaks.
- Production's surface is smoother and brighter, where the prototype's is denser and darker.
- The tendon system adds 7.6 ms and the features system 8.8 ms, nearly all of it flake drawing (see 7.1).
- Heat lives in the `trailWrites` lane, so a system cannot have both heat and trails.

### 7.4 v2's look, which exists only in the prototype

- **Legibility at a formed peak:**
  - quieter engraving;
  - a broad frontal-high key on the macro normal;
  - cavity occlusion sampled from the latent;
  - dark sockets with small pupils, and a dark mouth;
  - bound flakes dimmed over the face.
- **Per-god palettes:** key, rim, eye glow, two temper zones and a fog tint, normalised to unit luminance.
  Production's ADR-1149 regions are the nearest equivalent, but they are point-based, not per god.
- **Atmosphere:** a tinted source glow and screen-space god rays occluded by the surface and by dense matter.
  Production has no screen-space light scattering.
- **Dust absorbed into a forming face.**

### 7.5 The conductor

The conductor needs the scene-state gaps in section 5. In order:

1. a section-anchored phrase clock;
2. parallel state layers;
3. transitions in beats, with entry-relative trigger counters;
4. a downbeat window gate on triggers;
5. route reset at a cut;
6. the look-ahead accent, which needs a sequencer bake.

### 7.6 The art weaknesses left in v2

These are recorded as-is; the art was closed with them.

- **Faces are still often small.** The portrait band starts at fill 0.38, and the visible plate reads smaller
  than the 6.8-unit anatomical height assumes. Portraits should sit nearer fill 0.6.
- **The haze flattens contrast.** The entity-to-background luminance ratio fell from 10-90 (v1) to about 4-7.
- **The god rays read as a soft blob** around the source rather than as shafts. The occluders are too sparse at
  mid range to cut distinct shafts.
- **Camera-shake findings rose from 6 to 14** (Critic motion 0.69). Dust streaming across close and portrait
  frames reads as camera motion.
- **50.4% of frames are under 16.7 ms** (v1: 64.7%). The Machine God and Chimera sections are the slowest, at
  32.6% and 39.0%.
- **Face detection reaches 41.9% of phrase peaks, not "most".** Chimera is lowest at 21%, with one eye per
  sector.
- **Mid-range colourfulness is 12.1, against a target of 16.9.** The white flakes and the pale collapse dust are
  not tinted by the palette.

## 8. Engineering

- **The full render** writes PNG frames that are converted to quality-95 JPEGs in parallel while it runs. That
  keeps the frame folder near 11 GB instead of 22 GB, and the folder is deleted once the mp4 is verified.
- **Render time:** the whole song renders in 18 minutes.
- **`origin/main`** (DIGITAL MOSH) was merged into the branch at `bea38707`.
- **Suites at `bea38707`** (the merge with `origin/main` f56eaab0), run one after the other under
  `tools/gpu-lock.sh` after a reconfigure and a full build:

  | Suite | Exit code | Cases | Assertions |
  |---|---|---|---|
  | `avgen_tests` | **0** | 4186: 4166 passed, 19 skipped, **1 failed as expected** (the one `[!shouldfail]`, character-lab slopes) | 10,341,720 |
  | `avgen_render_tests` | **0** | 632: 631 passed, 1 skipped | 644,028 |

  Only this doc and the README index changed after that commit.
- **Large-file check** (`origin/main..HEAD`): nothing under `assets/`, no media, and no file over 1 MB. The
  largest is `examples/astral-forge/compare-t01-face.json` at 201 KB.

