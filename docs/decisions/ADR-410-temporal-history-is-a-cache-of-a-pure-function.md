# ADR-410: temporal history is a cache of a pure function

**Status:** proposed
**Date:** 2026-09-20
**Context:** the Reality / Temporal / Digital Deformation brief, §8–§17, §26–§27, §45–§47
**Extends:** ADR-091 (two-tier simulation authority), ADR-360 (the particle relaxation).
**Builds on:** ADR-035 (the five scene targets), ADR-034 (the AO temporal pass),
ADR-387/ADR-392 (the world-effect family's conformance)

## The short version

Datamosh gets a declared I-frame cadence anchored to `audio.beatCount` through an ordinary
`ModRoute`. **The thing that makes it land on the music is the same thing that makes it
scrubbable** — the constraint improves the effect instead of taxing it. The rest of this document
is the generalisation of that one observation: an effect that declares *when its history starts*
as a function of time is a cache, not an accumulator, and a cache can always be rebuilt.

The second result is that **time rift, time freeze and temporal rewind need no history at all**.
They are `render(t + offset)`, `render(T_freeze)` and `render(w(t))` — re-evaluations of a
renderer that is already re-entrant on time. The naive reading of the spec would have built a
32-frame ring in order to freeze a picture.

## A correction, because the misquote did real work

This ADR was briefed against the rule "**ADR-091: scrub must equal play**". That sentence does not
appear in ADR-091, and the rule it states is stricter than the one this project actually holds.

ADR-091 is titled **Two-tier simulation authority**, and it documents an entire tier of entities
as "stateful, reset on seek, and **explicitly not frame-accurate under scrub**". The contract that
really governs is ADR-360's:

> **A render must still be reproducible.** Scrub may differ from play; two renders of the same
> range may not differ from each other.

The compression is an easy one to make, and it had been repeated to several agents, which is why
it is corrected here in the open rather than quietly worked around. A design built against "scrub
must equal play" fails in one of two directions: it refuses a feature the project permits, or it
smuggles in an accumulator to get around a rule that was never there. **A rule everyone
half-remembers wrongly is worse than one nobody remembers**, because it never gets looked up.

What ADR-091 does supply — and what this ADR leans on entirely — is the *method* rather than the
prohibition. When a tier cannot give a guarantee, declare it: "a guarantee that is not visible is
a guarantee nobody can rely on."

## The problem the brief does not name

The brief asks for a reusable temporal media system: a history of previous colour, depth, motion,
object id and normal, 1–32 frames deep, feeding frame echo, temporal smear, datamosh, temporal
displacement, time rift, time freeze, rewind and an afterimage field.

Every one of those makes frame *N* depend on frames *N−1 … N−K*, and §47 asks for offline
determinism on top. A K-frame history and a reproducible render cannot both hold if history is an
accumulator.

ADR-360's relaxation is "particles only" by its own words, so it does not extend to this half. But
its *shape* is the right shape, and the mechanism it promised — "a bounded, opt-in **warm-up** (a
parameter, in frames, capped) [that] fills the pools from the seeded state on seek and at the head
of a render range" — is the same mechanism temporal history needs. That warm-up was never built;
it is being built now for particles. **This ADR does not invent a second one.** It states the
contract both must satisfy, and holds itself to a stronger version than ADR-360 required: here the
warm-up is not optional, and the scrub divergence *converges* rather than persisting.

The naive failure is the one this project keeps paying for: scrub to frame N cold, get a different
picture than playing into N, and see no sign that anything is different. A black frame and a
nearly-black frame are identical in a hash, and a temporal effect's failure mode is subtle drift.

## The decision

**No temporal effect in this half may be an accumulator. Every one declares a bounded history
depth K, and the *anchor* of that history is a pure function of time. History then stops being
state and becomes a cache of a pure function — so it can always be rebuilt, and ADR-091 survives
intact.**

Three consequences follow, and they are the whole design.

### 1. Every effect is classified, and the classification is compile-time

```
Tier T0  Re-evaluation   output(t) = render(g(t))        K = 0, no history at all
Tier T1  Bounded history output(t) = f(frames N-K … N)   K declared, 1..32
```

There is no third tier. Every effect in §9–§17 lands in T0 or T1:

| § | Effect | Tier | Why |
|---|--------|------|-----|
| 9 | Frame echo | T1 | K = echo length |
| 10 | Temporal smear | T1 | K = smear length |
| 11 | Datamosh | T1 | K = GOP length (below) |
| 12 | Object datamosh | T1 | as §11, masked by the id target |
| 13 | Temporal displacement | T1 | K = max displacement |
| 14 | Time rift | **T0** | a region showing `render(t + offset)` — a second evaluation, not a stored frame |
| 15 | Time freeze | **T0** | `render(T_freeze)`; the freeze *time* is the parameter, not "whatever was on screen" |
| 16 | Temporal rewind | **T0** | `render(w(t))` for a declared time-warp `w` |
| 17 | Afterimage field | T1 | K = decay length |
| 26 | Digital corruption | **T0** | image-space, this frame only |
| 27 | Chromatic / signal corruption | **T0** | image-space, this frame only |

The four that look least deterministic — rift, freeze, rewind — are the *easiest*, because the
renderer is already re-entrant on time. They need no history at all. This is only available
because ADR-091 made the scene a pure function of time in the first place; the temporal effects
are cashing a cheque ADR-091 wrote.

### 2. Datamosh's GOP is what makes it deterministic

Classic datamosh is the one genuine accumulator in the brief: hold a frame, keep applying motion
vectors, never refresh. K = ∞.

Real codecs do not work that way, and neither will this. A datamosh has a **declared I-frame
cadence**, and the cadence is a pure function of time:

```
iFrameTime(t) = the most recent anchor at or before t
K             = ceil((t - iFrameTime(t)) * fps), clamped to the declared maximum
```

The anchor is either a fixed period, or — through the existing signal bus (§39, `ModRoute`,
`audio.beatCount`) — **the beat**. The analysis is precomputed and deterministic (ADR-091 says so
explicitly), so a beat-anchored anchor is as pure as a periodic one.

So the effect that looks like unbounded corruption is periodic, bounded, scrubbable, *and* lands
its resets on the music. The thing that makes it musical is the same thing that makes it
deterministic. That is the best argument in this document that the constraint was worth honouring
rather than working around.

### 3. A cold history is never silently shown

A history that must be rebuilt takes time to rebuild, and the rebuild cannot always be instant.
Three contexts, three answers, none of them silent:

**Offline render (§47).** `RenderJob::step` → `renderOne` walks frames strictly forward from
`settings_.startSeconds` on a fixed-step clock, never repeating and never reversing
(`src/app/render_job.cpp:1003-1042`, `655-741`). It never calls `resetTemporalHistory()` and never
reads `discontinuityRevision()`; its determinism rests entirely on `renderer_` being freshly
constructed at `render_job.cpp:184`. A history-bearing render must therefore add a **K-frame
pre-roll** immediately after that construction — on the *real* renderer, because history is what
is being filled. The existing block at `render_job.cpp:156-183` is a *pipeline* warm-up on a
throwaway `SceneRenderer` for a 1-LSB Metal effect; it is not this and must not be confused with
it. Cost: K frames once per render. Frame `start` then has exactly the history a play-through
would have given it.

**Playback.** History is filled as the frames go past. Exact by construction.

**Interactive seek.** This is the only hard case, and it gets a two-stage answer:

- *During a drag* the history is **cold**. The effect renders its K = 0 fallback — an echo with no
  echo, a mosh with no mosh. Scrubbing stays responsive.
- *On seek settle* (the playhead stops moving for one frame) a **bounded warm-up** runs: K scene
  renders at history resolution, filling the ring, then the true frame. The artist lands on the
  picture the render will produce.
- *Throughout*, `TemporalHistoryState{ framesValid, framesNeeded }` is surfaced in four places: a
  viewport badge, the World Effects panel row for each temporal effect, a debug view, and
  `RenderStats`.

**The badge says what to do, not only what is wrong.** "Not what will be rendered" is accurate and
leaves the artist stuck; **"Temporal: settling — 3 of 8 frames"** tells them the picture is on its
way and roughly how far. A disclosure that does not imply an action is a disclosure people learn
to ignore, and an ignored badge is the silent failure this section exists to prevent. The
stuck-state wording is reserved for the case that *is* stuck: a history that cannot fill (no
warm-up budget, or an effect whose K exceeds the cap) reads **"Temporal: cold — this is not the
rendered picture"**, which is the one time the artist genuinely must act.

This is ADR-091's two-tier move applied to frames instead of entities: the divergence is bounded,
it is temporary, and it is *declared*. The difference is that ADR-091's live tier never converges
and this one always does — a warm-up is a guarantee that the cold picture is a transient.

### 4. Re-rendering the same frame must reproduce it

`AoRenderer::update` already solves this and the ring copies it exactly
(`src/rendering/ao_renderer.cpp:271-296`): a **repeat** of the same frame index restores the
history index and validity snapshotted at frame start, so the second render of a frame reads what
the first read. A **discontinuity** drops history. The two need opposite treatment and were once
treated the same; that bug has a name in this repo (`SYM-TERRAIN-1`) and is not going to be
reintroduced by a second implementation of the same idea.

For the same reason the ring resets through the **existing** hook,
`SceneRenderer::resetTemporalHistory()` (`src/rendering/scene_renderer.cpp:1619-1637`), which
already resets the AO history, the particle pools and the previous-frame matrices, and which a
viewport resize already calls (`scene_renderer.cpp:1424-1428`). No second invalidation path is
created. `Transport::discontinuityRevision()` (bumped only by a seek that moves the playhead,
`transport.cpp:216-224`) remains the upstream signal, and its two identical consumers
(`application.cpp:4083-4087` and `:5301-5306`) are where the warm-up is triggered on settle.

## Memory: per-channel depth, reduced resolution, one array per channel

§8 says "do not blindly retain expensive full-resolution buffers". Measured, the naive reading is
absurd: 32 frames of all six channels at 1080p is **1.86 GB**.

Three rules bring it to a budget:

**Depth is per-channel, not global.** "32 frames of history" does not mean 32 frames of *every*
channel. Reprojection needs one frame of depth. An advection chain composes into a single
accumulator rather than storing K motion fields. Only *colour* actually wants depth.

**Colour history is half-resolution RG11B10Ufloat**, not full-resolution RGBA16F: 4 B/px instead
of 8, at a quarter of the pixels — 2.07 MB/frame instead of 16.6, an **8× saving**, on a signal
that is about to be blurred, smeared or advected anyway. Normal history is opt-in and off by
default, because nothing in this half reads it.

**One texture array per channel**, layer `(frame − i) mod K`, following the shadow atlas
(`src/rendering/shadow_renderer.cpp:70-86`): one allocation, one binding, per-layer render views.

Resulting default (Realtime tier, 1080p):

| Channel | Format | Res | K | MB |
|---|---|---|---|---|
| Colour | RG11B10Ufloat | 1/2 | 8 | 16.6 |
| Motion (accumulator) | RG16Float | 1/2 | 2 | 4.1 |
| Depth | R32Float | 1/2 | 1 | 2.1 |
| Identifier | R16Uint | 1/2 | 2 | 2.1 |
| | | | | **≈ 25 MB** |

Against a scene-target baseline of about 91 MB, that is a quarter added, not a quadrupling.

**Measured, at 1080p**: eight frames of half-resolution colour is **15.82 MiB** (16.6 MB decimal),
at 960x540 in RG11B10Ufloat. `tests/rendering/test_temporal_gpu.cpp` asks the ring rather than
recomputing the estimate, and the same test pins the two ends that matter: 32 frames at full
resolution is sixteen times that, and a project with no temporal effect allocates **zero** -- which
is the line in §8 that matters most, because "pays a little" and "pays nothing" are different
promises. The numbers above are decimal MB and the test reports MiB; the two differ by 5% and that
is the whole of the discrepancy.
K = 32 at full-resolution colour remains reachable, gated behind an explicit setting that shows
its own cost, and driven by the quality tier (§48) exactly as `aoHistoryFrames` already is —
`QualitySettings`' own header comment has said since it was written that a tier scales "sample
counts, resolutions and **history lengths**".

**Selective / object-specific history is a read-time mask, not a storage partition.** §12's
object-level datamosh compares the identifier target at read time and moshes only the selected
ids. Storing per-object history would cost memory to do something the id target already does for
free. This is the single largest saving in the design and it falls straight out of ADR-035 having
made identifiers a target.

## §45: the motion vectors are adequate, measured

The audit found them present and well covered, not absent:

- Written by `pbr`, `pbr_skinned`, `procedural`, `grid`, `skybox`, `sdf_raymarch`, `water` and
  `particles`. Camera motion, per-object motion (`prevModel`, keyed by name so reordering cannot
  fake a velocity), skinning and wind deformation are all represented.
- **RG16Float is sufficient.** In UV units at 1920 px, the half-float ulp is 0.015 px at 20 px of
  motion and 0.117 px at 150 px; an 8-frame advection chain at 150 px/frame drifts **0.94 px**
  worst case. No format change is needed, and this replaces an assumption with a number.
- On a discontinuity `prevViewProj` falls back to the current `viewProj`
  (`scene_renderer.cpp:2443`), so velocity is exactly **zero** after a seek rather than garbage.
  An advection on the first frame after a seek is a no-op, which is the right failure.

**One real limitation, documented rather than discovered.** The velocity target describes *opaque*
surfaces. Transparency deliberately leaves it to the geometry behind (`water_renderer.cpp:187`),
and `atmosphere.wgsl:66` writes zero on purpose; the volumetric march composites after the scene
pass and never touches it. So a datamosh or displacement over fog, aurora or vortex advects with
whatever opaque surface is behind it. That is a correct consequence of ADR-035 and it must be
stated where an author chooses the effect, not found in a render.

## The warm-up is ADR-397's, not this ADR's

While this was being written the defects agent built `src/core/pre_roll.hpp` (ADR-397) for
ADR-360's particle warm-up, and deliberately built it as a **shared schedule** rather than
something private to particles. It is the mechanism this ADR asked for and it is not duplicated
here:

- `classifyStep(havePrevious, previous, current) -> First | Repeat | Continuous | Jump`
- `planPreRoll(roll, time) -> PreRollPlan` — which timeline seconds the roll covers and what frame
  indices they carry. **The schedule is shared; the work is not.** Each subsystem re-runs its own
  simulation over the plan's steps.

**And its analysis finds a real bug in this implementation.** `TemporalHistory::beginFrame`
currently distinguishes a repeat from a jump on the **frame index alone**, copying `AoRenderer`.
ADR-397 points out that this is not sufficient in the live application: `RealtimeClock::seek` moves
`renderTime` and leaves the frame counter climbing, so the frame after a scrub arrives as
`index + 1` carrying a second from somewhere else — and an index-only test calls that
`Continuous`. The ring would keep history across a seek and smear the pre-seek trail into the
post-seek frame, which is precisely the silent divergence this whole document exists to prevent.

Today that is masked rather than fixed: `application.cpp:4083` resets the history explicitly on a
transport discontinuity, so the ring is dropped before the classification is ever consulted. A
correctness argument that depends on a *different* subsystem calling a reset first is not one to
keep — it is ADR-385's shape, a stated reason standing in for a mechanism. `beginFrame` adopts
`classifyStep` when ADR-397 reaches main.

Two ADRs reaching the same conclusion from opposite ends — that history after a discontinuity is a
schedule of re-run frames rather than state to be repaired — is the reason it is worth one
implementation.

## What this obliges

- **The declaration is checked, not trusted.** ADR-392's conformance mechanism
  (`src/world/world_effects/effect_conformance.hpp`) is extended to this family. It already asks
  the engine the same questions the engine asks itself — registered paths obtained by registering
  a probe rather than by reading the field table, a save→load→save round trip run through
  registration, every panel row proved to be a parameter that kind has, and a kind proved to
  resolve as *itself* rather than falling through a dispatch chain's `else`. It gains one check:
  **every temporal effect returns a bounded K**, and the one that cannot fails by name.
- **The category goes in the World Effects panel, not a new panel** (§51). That panel is two
  hand-written sections today (`src/ui/world_effects_panel.cpp:146`, `:537`); this is a third,
  added the way the second was — one `drawTemporalSection(engine)` call — with its rows as
  `constexpr EffectRow` tables in `src/ui/ui_logic.hpp` so a test can walk them.
- **`temporal/` goes in `kBeginnerPrefixes`, not merely `kIntermediatePrefixes`**
  (`ui_logic.hpp:43-47`). Those lists gate which groups appear below the Advanced authoring layer,
  so a prefix absent from them is reachable-in-principle and findable by nobody — ADR-375's exact
  defect, reported twice. The authoring layer does not get to decide whether a flagship effect
  exists.
- **The opaque-only velocity limitation is stated in the effect's tooltip, not only here.** A mosh
  or displacement over fog, aurora or the vortex advects with whatever opaque surface is behind it
  (see §45 below). That is a surprising result that looks exactly like a bug, so it is one
  sentence on the row where an author turns the effect on. An ADR nobody reads at the moment of
  choosing is not a disclosure.
- **Audio reactivity is a `ModRoute`, not a mechanism** (§39). The datamosh anchor is a route from
  `audio.beatCount`; nothing new is built.
- **§53's debug views need a surface, because the existing family has none.** `AuxDebugView` is
  reachable only from the `--debug-target` CLI flag (`application.cpp:553-557`, `:1137-1155`);
  `grep` over `src/ui/` finds zero hits. Adding an enumerator means appending to the enum
  (inserting renumbers every shader mode), a `case` in `auxDebugViewName`, an entry in the
  hand-written `kViews[]` array *and* the help string, and an `if (mode == N)` arm in
  `shaders/aux_debug.wgsl`. The history-state view is the one debug view an artist must be able to
  reach from the UI, since it is the thing that says "what you are looking at is not the render".

## Two defects found while auditing, neither in this half's code

Recorded because they are adjacent, real, and would otherwise be rediscovered:

1. **A timeline seek does not reset auto-exposure metering.** `PostSettings::exposureReset` says
   it is for "(scene change, timeline seek)", but `cameraStateReset_` is set only by
   `Engine::resetCameraState()` (`engine.cpp:423-426`), whose sole caller is the composition
   install at `engine.cpp:218`. `Engine::seekSeconds` does not set it. So an interactive scrub
   resets temporal history and leaves the meter carrying its pre-seek reading.
2. **In an offline render the one exposure reset is consumed by the throwaway warm-up frame.**
   `render_job.cpp:174` calls `engine_->update(t)`, which clears the one-shot flag at
   `engine.cpp:3812`, so `renderOne()`'s first real frame runs with `exposureReset == false`.
   Harmless today only because the job's `PostProcessor` is default-constructed.

## Consequences

**Good.** ADR-091 is not weakened — it is extended by the same move it already made, and the
extension converges where the original does not. The offline render is exact. Nothing needs a
second invalidation hook, a second reprojection, a second audio-reactivity system or a second
determinism story. Datamosh gets its musicality and its determinism from one mechanism.

**Bad.** A seek is no longer instant when a T1 effect is live: it costs a K-frame warm-up. That is
a real cost, paid in a visible transient rather than in a wrong picture. Authors must also learn
that "time freeze" freezes a *time*, not a screenful — which is the honest model, but it is not
the one a compositing tool would teach them.

**Watch for.** The temptation to let one effect quietly become an accumulator "just for this one
look". That is the whole of what this ADR forbids, and it will not fail to compile, will not
throw and will not log — so the conformance check (ADR-392's mechanism, extended to this family)
asks every temporal effect for its declared K and fails the one that cannot give a number.
