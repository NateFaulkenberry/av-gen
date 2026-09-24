# Catalog: Temporal

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags are
defined in [shared-infrastructure.md](shared-infrastructure.md). The simulation half of this family
is in [time-dilation.md](time-dilation.md).

*Afterimage appears under both Motion and Temporal. It has one entry:
[catalog-motion.md#afterimage](catalog-motion.md#afterimage--afterimage).*

## The family's two mechanisms

Every temporal effect uses one of these two mechanisms.

1. **Image history (TEMPORAL).** This is ADR-410's ring of clean pre-post HDR frames
   (`src/rendering/temporal_history.*`).
   - The ring holds at most 32 frames, stored as one texture array per channel at
     `resolutionScale`, which defaults to 0.5.
   - Colour is RG11B10Ufloat, falling back to RGBA16F.
   - It is captured **before** the post chain (`scene_renderer.cpp:3877-3899`).
   - ADR-410's rules:
     - An effect is an **FIR filter** over history.
     - An effect may never read its own output. IIR feedback is forbidden, because a feedback loop
       cannot be rebuilt by re-rendering, so it cannot scrub.
     - A seek resets the ring (`resetTemporalHistory`, called from `application.cpp`).
   - Today only one effect exists, **FrameEcho** (`src/scene/temporal_settings.hpp:35-72`,
     `shaders/temporal.wgsl`). Everything is whole-frame. The header says per-object selection is
     meant to happen "at read time from the id target" (`temporal_history.hpp:28-32`), but no
     temporal shader reads ids today.
   - The Motion channel is allocated but never captured (`temporal_history.cpp:378`).
   - ADR-410's *warm-up* was never built. It would re-render the K frames before a seek target, and
     it is what would make image history exact after a scrub. Its status is still "proposed".
2. **Motion history and local time.** These are HIST (per-entity transform history, checkpointed)
   and LOCALTIME (per-entity time τ). Both are exact under seek by construction. See
   shared-infrastructure and [time-dilation.md](time-dilation.md).

**The family's rule.** Prefer mechanism 2 whenever the effect concerns an *entity*. It is exact,
cheap, and independent of resolution. Use image history only for looks that are genuinely about the
picture: an echo of the whole frame, or smear. When image history is used, the **temporal warm-up**
from ADR-410 is the missing piece that makes it scrub-exact. The roadmap schedules it. Until it
lands, every image-history effect reports its "settling" state after a seek, which the ring already
exposes (`framesValid < framesNeeded`).

### One Temporal Filter type, three looks

**Echo**, **Temporal Smear** and **Ghosting** are all FIR kernels over the same ring. They differ
only in how their taps are spaced and weighted. They are therefore **one registry type,
`temporalFilter`**, with a `kernel` choice, and each spec name is a style:

| Style | Taps | Weights | Look |
|---|---|---|---|
| Echo | sparse: every `spacing` frames, N ≤ 8 | geometric `decay^i` | distinct repeated copies |
| Temporal Smear | dense: every frame over `length` | box or triangle | long-exposure streak |
| Ghosting | dense | exponential, plus a per-tap drift offset and tint | lingering, drifting ghosts |

The existing FrameEcho (`scene.temporal.echo`) is exactly the Echo kernel. It **migrates** to a
Camera-owned `temporalFilter` instance with no compatibility alias, under ADR-441. This keeps one
authority for the look instead of two.

**Masking.** A `mask` choice selects between whole frame, entity (by id), and depth range.

- Entity masking needs an **id channel in the history ring**: R32Uint at history resolution, or
  R16Uint for the object-id half.
- Without it, a masked tap can only test the *current* frame's id. That selects the region where the
  owner is now, and misses where it was, which is the wrong half for a trail.
- History ids let a tap keep pixels where the owner *was*.
- Cost: one more array channel, 2–4 bytes per pixel per frame at history resolution.

---

## Echo  (`temporalFilter:echo`)

- **2.1 Definition.** Discrete delayed copies of the picture, or of one entity, superimposed and
  decaying. It is the visual echo of a sound delay.
- **2.2 References.** ADR-410. A video-feedback look, approximated by an FIR (not a feedback loop).
  The existing FrameEcho has frames 6, strength 0.45, decay 0.72.
- **2.3 Visual anatomy.** Anything that moves repeats as 2–8 fainter copies spaced behind it. Static
  areas are unchanged.
- **2.4 Implementation.** TEMPORAL FIR:
  `out = cur·(1−s) + s·Σ_i w_i·hist[i·spacing]`, with weights normalised, and in Entity mode masked
  by `histId[i·spacing] == owner`. Taps run up to the ring length (32). Longer delays use capture
  stride: capture every Nth frame, which the ring would need to support.
- **2.5 Targets.** Camera (whole frame) and Entity (masked, needs history ids).
- **2.6 Parameters.** `copies`, `spacing` (frames or seconds), `decay`, `strength`, `mask`, `tint`,
  `blend` (Add / Max / Mix).
- **2.7 Modulation.**
  - `beat → spacing` makes one echo per beat subdivision. At 120 BPM an eighth note is 15 frames at
    60 fps.
  - `audio.rms → strength`.
- **2.8 Animation model.** Stateless over the ring. The ring settles after a seek, and warm-up makes
  it exact.
- **2.9 Compositing.** Pre-post, so the echoes are bloomed and graded like the frame. The ring holds
  clean radiance only, so echoes of echoes cannot compound.
- **2.10 Stack behaviour.** `ScreenSpace`, in the temporal slot.
- **2.11 Performance class.** Medium. Bandwidth grows with the number of taps.
- **Presets.** Beat Echo, Dub Delay, Entity Echo.

## Freeze-Frame  (`freezeFrame`)

- **2.1 Definition.** Motion stops while something else continues. There are two modes:
  - **Pose Freeze (World or Entity).** The world's or an entity's local time holds while the camera
    keeps moving. This is bullet time.
  - **Image Freeze (Camera).** The *picture* holds, as a stutter or a still.
- **2.2 References.** Bullet time: a camera moving through a held instant. Stutter edits come from
  music video.
- **2.3 Visual anatomy.** Pose: bodies, particles and effects stop mid-motion while the camera
  orbits. Image: the frame locks, optionally with a flash or grain, then releases.
- **2.4 Implementation.**
  - **Pose Freeze.** LOCALTIME rate 0 for the owner, or for every entity when World-owned. See
    [time-dilation.md](time-dilation.md). This is exact: the owner is evaluated at `τ_hold`, and the
    camera is not an entity with local time, so it keeps moving. Particles belonging to a frozen
    owner hold if their emitter runs on the owner's τ. This needs particle `dt = 0` support, which
    is a small addition.
  - **Image Freeze.** Show `hist[age_frames]` until the release. It is exact only if the ring holds
    the hold frame. After a seek into the hold window, **warm-up renders the single hold frame**,
    because a freeze's tap set is one frame. This is the cheapest warm-up of any temporal effect.
    Until warm-up exists, the effect holds the first valid frame, which is documented.
- **2.5 Targets.** World (freeze everything but the camera), Entity (freeze one), Camera (Image).
- **2.6 Parameters.** `mode`, `hold` (seconds), `trigger*`, `flash`, `desaturate`, `stutterRate`
  (Image mode repeating holds).
- **2.7 Modulation.** `music.drop` triggers it, `beat` drives stutter.
- **2.8 Animation model.** idle → hold (from t0 for hold seconds) → release. It is a function of the
  trigger time.
- **2.9 Compositing.** Pose Freeze is ordinary rendering. Image Freeze replaces the pre-post
  radiance, so grading and bloom still run. If a separate grade is wanted, Freeze has its own
  desaturate.
- **2.10 Stack behaviour.** Pose Freeze is a simulation-level LOCALTIME effect, not a render stage.
  It is declared `Geometry` with a `requiresLocalTime` flag. Image Freeze is `ScreenSpace`.
- **2.11 Performance class.** Very Low (Pose) or Low (Image).
- **Presets.** Bullet Time, Drop Freeze, Stutter.

## Time Dilation  (`timeDilation`)

- **2.1 Definition.** An entity, or a region, experiences time at a different rate:
  - slow motion (rate < 1)
  - fast-forward (rate > 1)
  - hold (rate 0)
  - rewind-like (rate < 0, bounded, see Reverse)

  The **global clock is never changed.**
- **2.2 to 2.10.** See [time-dilation.md](time-dilation.md). In summary:
  - Each owner has a local time `τ(t) = ∫ rate(s) ds`, integrated on the fixed 1/60 grid.
  - For simulated bodies, τ lives in the entity and is checkpointed (ADR-700).
  - For track-driven nodes, τ is computed from a prefix-sum cache over the rate curve.
  - Everything that belongs to the owner reads τ instead of t: its tracks, behaviours, animation,
    effect envelopes and emitters.
  - The visual partner is Time-Warp Distortion.
  - Targets: Entity, World (a region: rate is a function of position).
- **2.11 Performance class.** Low: CPU only. The cost is the re-evaluation of the owner's tracks at τ.
- **Presets.** Slow-Mo Hero, Hyper Fast, Time Bubble (region), Beat Breath (rate on `beat.phase`).

## Temporal Smear  (`temporalFilter:smear`)

- **2.1 Definition.** A long-exposure smear. Motion leaves continuous streaks, like a shutter held
  open for many frames. It is distinct from Motion Smear, which deforms geometry, and from motion
  blur, which covers one shutter interval.
- **2.2 References.** Long-exposure photography as a box filter over time. ADR-410's FIR.
- **2.3 Visual anatomy.** Moving lights draw streaks, moving objects blur into translucent sweeps, and
  static content stays sharp.
- **2.4 Implementation.** TEMPORAL dense FIR with a box or triangle kernel over `length` frames, up to
  32. There is an optional `max`-blend "light painting" mode that keeps only the brightest samples,
  which suits emissive trails. Entity mode is masked with history ids.
- **2.5 Targets.** Camera, Entity.
- **2.6 Parameters.** `length`, `kernel`, `blend` (Mean / Max), `strength`, `mask`.
- **2.7 Modulation.** `audio.rms → length`, `music.break → strength`.
- **2.8 to 2.11.** Stateless over the ring. Pre-post. `ScreenSpace`. **High**: up to 32 taps of
  bandwidth per pixel. It must be limited by quality tier (`temporalHistoryScale`).
- **Presets.** Light Painting (Max), Dream Smear, Shutter Drag.

## Ghosting  (`temporalFilter:ghost`)

- **2.1 Definition.** Lingering translucent after-copies that fade out slowly and may drift, for
  example rising upward like spirits. Echo produces crisp repeats. Ghosting produces soft, fading,
  drifting ones.
- **2.2 References.** [R9] Karis 2014. Unclamped history reprojection produces "ghosting", and here
  that artefact is the intent. The engine keeps it FIR per ADR-410, so it has no feedback loop.
- **2.3 Visual anatomy.** Soft, desaturated or tinted fading copies that trail motion. They are
  optionally displaced by a drift, so ghosts float away.
- **2.4 Implementation.** TEMPORAL dense FIR with exponential weights. Each tap `i` samples at
  `uv − drift·i`, so the ghost drifts in screen space. Tint shifts per tap. It works with an Entity
  mask. The length is capped by the ring, at 32 frames, or more with stride.
- **2.5 Targets.** Entity (primary), Camera.
- **2.6 Parameters.** `length`, `decay`, `drift` (vector), `tint`, `desaturate`, `strength`, `mask`.
- **2.7 Modulation.** `owner.speed → strength`, `beat`.
- **2.8 to 2.11.** Stateless over the ring. `ScreenSpace`. Medium to High (taps).
- **Presets.** Spirit Rise, Motion Ghosts, Haunt.

## Delayed Motion  (`delayedMotion`)

- **2.1 Definition.** The owner's *rendered* motion is a delayed or resampled version of real motion.
  It has three modes:
  - **Follow.** The owner replays a *leader* entity's path, `delay` seconds behind it, like a
    trailing twin or a snake.
  - **Lag.** The owner's visual lags its own simulated position.
  - **Stepped.** The owner animates "on twos": its transform and pose are sampled at
    `floor(t·fps)/fps`. This is the limited-animation look.
- **2.2 References.**
  - A delay line applied to motion.
  - Limited animation "on twos", used in stylised 3D such as *Spider-Verse* and GGXrd [R20]. Motomura
    describes stepped keyframes as a stylistic choice rather than a performance one.
- **2.3 Visual anatomy.** Follow: a companion traces the leader's exact path. Stepped: choppy, crisp
  poses at the chosen rate while the camera stays smooth.
- **2.4 Implementation.**
  - Follow and Lag use **HIST lookups**. The owner's render transform is `HIST_leader(t − delay)`,
    interpolated between grid samples. This requires HIST capacity to be at least `delay`.
  - Stepped is an XFORM that replaces the transform with the one at the stepped time. That comes from
    HIST for simulated bodies, or is evaluated analytically for track-driven nodes. A skinned pose
    also steps, through the animation sampling time. That is LOCALTIME with a step function.
  - All three are exact, because they are functions of t over checkpointed history.
- **2.5 Targets.** Entity.
- **2.6 Parameters.** `mode`, `leader` (endpoint), `delay` (s), `stepRate` (fps), `offset` (local).
- **2.7 Modulation.** `beat → stepRate`, which gives stepped motion synced to the tempo.
- **2.8 Animation model.** Stateless given HIST.
- **2.9 Compositing.** Ordinary rendering. Velocity uses `prevModel`, which in Stepped mode is zero
  between steps and jumps at a step. For a crisp look, motion blur should be *off* for the owner; an
  FXL flag makes the prevClip equal the current clip.
- **2.10 Stack behaviour.** `Geometry` (XFORM, HIST).
- **2.11 Performance class.** Very Low.
- **Presets.** Shadow Twin (Follow), On Twos (Stepped 12 fps), Drunk Lag.

## Reverse Playback-Like  (`reverse`)

- **2.1 Definition.** The appearance of time running backwards over a bounded window. There are two
  modes:
  - **Motion Rewind (Entity).** The owner retraces its last `span` seconds, then resumes.
  - **Image Rewind (Camera).** The last ring frames play back in reverse.

  The global clock keeps running forward; this is only an appearance.
- **2.2 References.** The VHS-rewind look: reversed motion plus scan distortion. Game rewind features
  such as *Braid* and *Prince of Persia* keep a state buffer, which is what HIST is in miniature.
- **2.3 Visual anatomy.** After the trigger, the owner moves backward along its recent path, often
  with Scanlines or Chromatic Aberration layered on. At the end, it either snaps back to the present
  or blends into it.
- **2.4 Implementation.**
  - **Motion Rewind.** At trigger time `t_r`, for `a = t − t_r` in `[0, span]`, the render transform
    is `HIST(t_r − a·speed)`. It then *blends* back to the live transform over `returnSeconds`.
    Simulated bodies keep simulating forward underneath, so the rewind is **visual only**; this is
    the correct choice, see time-dilation §4. For track-driven owners the span is unbounded, because
    tracks can be evaluated at any τ. Skinned pose uses LOCALTIME with a negative rate on the
    animation clock, for clips, which are functions of time.
  - **Image Rewind.** `hist[a·speed]` reversed. The span is at most the ring length × stride.
- **2.5 Targets.** Entity, Camera.
- **2.6 Parameters.** `mode`, `span`, `speed`, `returnSeconds`, `trigger*`, `scanlines` (a companion
  preset).
- **2.7 Modulation.** Trigger on `music.break` or `music.drop`, or a scratch control through MIDI.
  In the live tier only, the transport's scratch mode could drive it.
- **2.8 Animation model.** idle → rewinding (a function of `t − t_r`) → returning → idle.
- **2.9 Compositing.** Ordinary rendering (Motion) or pre-post (Image).
- **2.10 Stack behaviour.** `Geometry` (Motion) or `ScreenSpace` (Image).
- **2.11 Performance class.** Very Low (Motion) or Low (Image).
- **Presets.** Rewind, Tape Scratch, Undo Hit.
