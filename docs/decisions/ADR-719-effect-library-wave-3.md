# ADR-719: Effect Library Wave 3 — fractal paths, analytic shells, and two lenses

**Status:** Accepted
**Date:** 2026-09-25
**Builds on:** ADR-702 (instances attached to owners), ADR-703 (Wave 1), ADR-716 (Wave 2).
**Design:** `docs/design/effect-library/` — shared-infrastructure "BOLT" and "SHELL", rendering-architecture
§4–5, and the catalog entries for each type.
**Implemented by:**
- BOLT: `world/effects/bolt_path.*`, drawn through RIBBON.
- SHELL: `world/effects/shell_frame.*`, `rendering/shell_renderer.*`, `shaders/shell.wgsl`, `shaders/shell_fx.wgsl`.
- The DF lens modes: `distortion_frame.*`, `distortion_renderer.*`, `shaders/distortion.wgsl` (`fs_offset_ext`).
- Fifteen types, kinds 33–47, in `world/effects/kinds/`.

**Tests:**
- CPU: `[bolt]`, `[shell]`, `[shell2]`, `[lens]`, `[shimmer]`, and the registry and conformance suites.
- GPU: `[bolt]`, `[shell]`, `[shell2]`, `[lens]`, `[shimmer]`, plus hidden review sets that write the sheets.

**Review sheets:** `~/Desktop/av-gen-review/16-effect-library-wave3/{bolt,shell,lens,shell2}/`.

---

## Context

Wave 3 is the roadmap's BOLT + SHELL wave. About fifteen effects need both primitives on top of Waves 1–2's
DF, RIBBON, LIGHTMOD, EMIT and TRIGGER, which is why they could not come earlier. It ran as four
slices with no files in common beyond the registration lines. Kind numbers were fixed per slice in
advance, so a type's serialised value never depended on merge order.

| Slice | Kinds | What it built |
|---|---|---|
| bolt | 33–36 | BOLT, then Lightning, Arc, Electric Field, Discharge |
| shell | 37–39 | SHELL, then Plasma, Energy Shield, Force Field |
| lens | 41–42 | DF's Facing and Cylinder shapes and Shimmer and Lens fields, then Heat Shimmer, Gravitational Lens |
| shell 2 | 40, 43–47 | After shell and bolt merged: Charge-Up, Light Beam, Halo, Bubble, Portal (no Remote View), Reality Tear |

## Decision

### BOLT

A CPU generator using midpoint displacement with branches.
- **Hashing:** pcg3d, never a `std::` distribution, so the output is the same on every platform.
- **Levels of detail:** a lower depth is an exact prefix of a higher one.
- **Limits:** 512 vertices and 24 branches, with an LRU cache and LOD by projected size.
- **Drawing:** each bolt is two ribbon strips, a core and a glow.

**The shape rule came from looking.** The first generator drew random-walk scribbles that folded back on
themselves. Now:
- every vertex advances along its start-to-end chord and is offset across it only;
- offsets shrink ×0.55 per level;
- no segment leans more than 35° from the chord, so no turn exceeds 70°;
- branches leave at 15–45° towards the target.

A test over 1,181 bolts pins this rule. It failed 1,082 of them on the first generator.

**Shared hooks added:**
- `EffectResolve::light`: any bucket can take LIGHTMOD pool lights.
- FXL now folds a lane term that comes from another bucket.

### SHELL

- **One storage buffer** of 192-byte records, holding up to 128 shells; the rest are `Dropped` with a reason.
- **Producers** register from their own type files, as RIBBON's do.
- **Batches** are sorted by shading kind and mesh, keeping stack order.
- **One pipeline per shading kind** (ADR-118): the three phase-1 kinds plus six from phase 2 (Beam, Glare, Ring, Bubble, Portal, Tear).
- **Draws** go in pass 1's blended section with all five targets declared.
- **Depth:** shells read the separate linear-depth target, with a flag saying whether this frame's prepass ran. A shell that wants depth on a frame without it is `Partial` with a reason, one frame late in status but never in pixels.
- **Portal and Reality Tear write depth.** Bubble, Portal and Tear blend over the frame; the others add.
- **More hooks for phase 2:** `EffectResolve::distortion` and `::particles` let a shell type add DF proxies and EMIT systems, and `EffectSceneQuery::lightView` lets Light Beam match a spot light.

**Plasma's look came from looking.** The first version summed emission along the whole ray, so every orb
was a white bloom blob. Now the strands both emit and hide what is behind them, and only a small hot core
passes the bloom threshold. A GPU guard bounds the orb's white-pixel fraction and requires colour and
luminance spread; it failed on the first version.

### The DF lens modes (Heat Shimmer, Gravitational Lens)

- **New shapes and fields:** Facing and Cylinder shapes, Shimmer and Lens fields, and an R16F cover target (the event horizon).
- **Existing DF is untouched.** Inside `fs_offset`, one pixel of an existing Space Warp frame moved by one level, so the new modes run in their own entry point, `fs_offset_ext`. `fs_offset` is textually unchanged, and all 52 PNGs the existing DF and Wave 2 tests write are pixel-identical.
- **No IBL tap for the lens.** Every lens tap stays inside the lens's own disc, where the frame copy already holds the bent ray's colour, whatever the sky is. Only a lens crossing the frame edge clamps, as ADR-703 does for sky lensing.

## Evidence

- **Gates.** With no instance, every primitive is byte-identical to the pre-change binary:
  - SHELL: Glowmere t=60 `fc5047c7c187d0f8`, UFO stack t=120 `577be54d2b814393`;
  - BOLT: zero ribbon draws and zero pool lights;
  - lens: the 52 existing DF/Wave 2 dumps are pixel-identical;
  - shell 2: six disabled instances render the same bytes as no effect.
- **Exactness.** Play = scrub on a keyed owner holds for a triggered Lightning (ribbon bytes and flash lights), the Energy Shield's hits, and the phase-2 shells, side data and DF proxies.
- **Shown able to fail:**
  - bolt determinism, endpoints, prefix, caps, the backtracking rule, budgets and Partial;
  - shell capacity, the ground-line location, the depth-prepass fallback, plasma occlusion and the plasma look guard;
  - lens foreground cube not bent, the mirrored inner image, the horizon over a foreground cube, shimmer coherence (0.26 frame-to-frame against 0.80 for white noise);
  - shell 2 gate, portal and glare occlusion, and Charge-Up's release burst.
- **Looked at** (the sheets):
  - lightning forks down behind a Glowmere hill, and an arc hangs from the saucer;
  - the shield has hexes, a rim and hit rings;
  - plasma orbs are coloured, with strands and a hot core;
  - the laser-grid wall;
  - the black hole has a photon ring against the Tree of Life sky, and the hillside lens swirls the trees behind it;
  - a violet portal on the valley floor, an iridescent bubble, a void tear against the aurora, corona halos, the Charge-Up gather and release, and the tractor beam.

## Findings not fixed here (for the owner's eye, or later waves)

- **Light Beam** reads as soft haze rather than a crisp beam. Its pool light is a point, not a spot.
- **Plasma**'s blue close-up preset shows subtle strands; the orange and violet presets show them clearly.
- **Heat Shimmer**'s Campfire preset is subtle at 960p.
- **Gravitational Lens:**
  - its Einstein ring is faint against a smooth sky gradient;
  - the horizon also darkens fog in front of it.
- **Reality Tear:** the Crystal Crack's shear barely shows against a smooth aurora.
- **Force Field:** the review shot stands mostly on the bank, not across the river.
- **Bolts** pick up the film's lens chromatic aberration (0.109) as orange fringes on thin cores; the bolt colour is correct.
- **Structural limits:**
  - Discharge sparks are ribbon streaks, not EMIT (exact under scrub).
  - Electric Field's arcs sit on the owner's bounds ellipsoid, not its mesh.
  - Bolt LOD depends on the camera, so play = scrub is exact when the camera is a function of time.
  - The arc can flip where its chord points exactly along −Z.
  - Particles are not exact after a seek (EMIT's existing behaviour).
  - A camera inside a Beam or Ring box can lose pixels to the depth test.
  - The camera inside a Plasma orb sees nothing.
  - Charge-Up's `fx.<id>.charge` signal is not published: SIGNALS has no path for an effect to publish one. `chargeUpCharge()` is the pure function a publisher would call.
- **Not built:**
  - Energy Shield's Contour mode (REDRAW) and refraction;
  - aimed shield hits (they land at hashed directions);
  - Portal Remote View (Wave 6);
  - a Camera form of Reality Tear;
  - Heat Shimmer's Ground Haze (FXPOST, Wave 4);
  - lightning ground-snapping, and a shadowed or sky-exposure flash.
