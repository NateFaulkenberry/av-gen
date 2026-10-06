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

**Measured** (M2 Max, 1080p, under the lock, p50 / p90, 2M particles unless stated):

| Moment | Iteration 1 | Iteration 2 (correct cache) | Iteration 2 at 1.75M |
|---|---|---|---|
| T01 formed face | 15.8 / 19.5 | **12.9 / 15.1** | |
| T02 close-up | 23.8 / 26.8 | see the final table | |
| T03 fold, close | 30.1 / 45.4 | see the final table | |
| T04 choir lattice | 24.5 / 30.9 | see the final table | |
| T05 MICRO | (not measured) | see the final table | |

The final table is the last section of this file.

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

## Step 7: the production path

ADR-1140 to ADR-1143. See the section at the end of this file. It was gated on steps 1-6 landing, and they
did.
