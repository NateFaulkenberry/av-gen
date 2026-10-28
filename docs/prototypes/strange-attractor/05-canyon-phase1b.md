# Canyon Phase 1b: the flicker traced, and the look brought back toward gate2e

This phase works from the coordinator's Phase 1b brief:
- (A) find and fix every flicker spike in the Phase 1 flythrough;
- (B) match the gate2e ray-march look as closely as the mesh allows.

The ray-march path is still there behind `"mesh": false`. The decision records are ADR-1858 (cascade caster reach)
and ADR-1859 (baked occlusion and ledge normals). Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-p1b/`.

## A. Flicker

**Metric.** This is the coordinator's metric. For each frame, take the mean |Δ| of the greyscale frame at 480x270
(`d_i`). Irregularity is `|d_i - mean(d_{i-1}, d_{i+1})| / mean`. A spike is a frame whose change is unlike its
neighbours'.

| clip | median | p90 | p99 | max |
|---|---|---|---|---|
| gate2e ray-march (reference) | 0.003 | 0.009 | 0.021 | 0.14 |
| Phase 1 mesh | 0.031 | 0.391 | 0.904 | **6.23** |
| Phase 1b mesh (final 1080p clip) | 0.010 | 0.050 | 0.140 | **0.25** |

The largest spike fell 25-fold, and nothing in the flight now stands above ordinary camera acceleration. Phase 1b is
still about 3x the ray-march's frame-to-frame irregularity (median 0.010 against 0.003). It has no single events:
it is spread evenly through the flight.

### What each spike was

**24.17 s (6.2x), 25.17, 25.67 and 26.03 s: cascade pull-back (ADR-1858).** With shadows off, every one of them
vanishes. The cause is in `fitDirectionalCascade`:
- Each cascade's light-space eye stands back `clamp(casterDistance, r/2, 3r)` from the cascade, where `r` is its
  radius. A near cascade is tens of metres across.
- Under the 9° sun, the shadow on the slabs comes from a rim more than a kilometre up-sun. Seen through the far
  cascade the slab is in shadow. When it moves into a nearer cascade, the rim leaves that cascade's depth range and
  the whole slab turns lit for a frame or a few.
- This is the "slab fully in shadow in one frame at 24.17" and the "patch shape changing" the coordinator saw.
- Things that changed nothing: cascade count (1, 2, 4), bias, and caster screen-size culling (`shadowCasterMinPixels`
  is 0 by default, so it was not culling).
- Fix: `environment.shadowCasterReach`, a floor on every cascade's pull-back. The scene uses 4000 m. The default 0 is
  bit-identical to the old fit, and the unit test pins that.

**20.1, 22.6 and 16.93 s: the same mechanism, at a smaller scale.** These are smaller lit patches crossing a cascade
boundary. They are gone with the reach.

**7.0 s and the dotted crawl at 26.0 s: screen-space contact shadows.**
- Contact shadows march a short screen-space ray per pixel. On the near-vertical walls at a grazing sun, they drew a
  dotted checker that crawled with the camera.
- With `environment.contactShadows: false`, the 6.97→7.0 s frame difference fell from 4.38 to 1.87.
- They add nothing the cascades (now reaching) don't already give at this sun angle, so they are off in the scene.

**The river's SSR.** I first suspected it of the dotted grid on the water at 7.0 s. I tested reach 450, 800 and
1200 m and 160 or 256 steps: the 6.97→7.0 s difference was 2.95–2.97 in every case, so SSR is not a source. The
short reach did lose the lit walls' reflections, so it is back at 1200 m with 160 steps.

**Geometry, LOD and culling pops: none, by construction and by measurement.**
- The mesh is static. There is one RTIN, no LOD switching, and no streaming.
- Chunk frustum culling drops whole chunks only when they are off-screen.
- After the fixes, the largest remaining irregularity anywhere in the flight is 0.24–0.27. That is ordinary camera
  acceleration (the gate2e clip itself reaches 0.14).

**Proof on the output video:** `07-flicker-strips-p1-vs-p1b.png`. It shows four consecutive frames at each former
spike, Phase 1 on the left and Phase 1b on the right. In Phase 1, the slabs at 24–26 s pop to lit and back. In
Phase 1b they are steady.

## B. Look

Each step below was set side by side against gate2e at the six checkpoints (1, 9, 18.5, 22.5, 33 and 40 s). The
pairs are `01-06-*-gate2e-vs-p1b.png`.

1. **Sky.**
   - Phase 1's uniform lilac came from a wide haze, 0.22. At that width the warm horizon colour reached the zenith.
   - Haze is now 0.12, with a deeper blue zenith and a pink horizon, so the twilight band sits low as in gate2e.
2. **Cloud streaks.** I used the existing engine cloud system (ADR-1289 lobes) rather than writing a new one:
   - 18 long, flat ellipsoid lobes at 2.8–4.5 km, about 6:0.12:0.5 in proportion, running across the flight;
   - lit warm from the sun's side, with pink-violet ambient underneath.

   **No engine change.**
3. **Shadowed rock.** gate2e's shadowed rock is red-brown; Phase 1's was violet. The ray-march put inter-reflection
   in its ambient, and the engine's ambient is the sky alone, so the dimmer sky ambient (0.085) took the blue out of
   the shadows. Two unshadowed warm directional lights stand in for the bounce:
   - one from the sunlit walls across the canyon;
   - one from the floor, upward.
4. **Strata.**
   - Four band colours (cream, orange, deep red and mauve) blended in OKLab, where Phase 1 had a three-stop ramp.
   - A wider spread of band noise, and per-band brightness from 0.74 to 1.12.
   - A darker floor colour.
5. **Ledges, laminations and relief as detail normals (ADR-1859).**
   - The band coordinate is uv.x, so the shader's cotangent frame tangent runs up the strata. The program writes
     `(s, 0, 1)`, where `s` is a downward under-lip at each band's foot, an upward lip at its head, five laminations
     per band, and a low-frequency triplanar relief.
   - The laminations and relief fade out between 280 and 700 m so they cannot alias.
   - Walls only.
6. **Crevice darkening (ADR-1859).**
   - A horizon-search occlusion is baked per mesh vertex from the height grid. It runs in 8 directions out to 256 m
     and counts only what rises above the horizontal.
   - It is packed into uv.y beside a quantised playa level, and feeds the program's occlusion output and, slightly,
     its albedo.
   - GTAO stays off, because it drew fins on the long triangles.
7. **Water.** Sky mirror 0.2 and reflection 1.3 (were 0.6 and 2.0). The river is darker, so the canyon's reflections
   carry it.
8. **Fog.** A darker violet (0.16, 0.115, 0.26). The haze no longer lifts the floors to lilac.

### Where it still falls short of gate2e (honest)

- **Warm river reflections are weaker.** gate2e reflected the lit walls with a true ray-marched mirror. SSR can only
  reflect what is on screen, so the lit cliff tops at the frame's top edge reflect only partly.
- **Some slabs are now correctly in shadow where gate2e had them lit.** For example, the right-hand slab at
  18.5–26 s. gate2e's own shadow march was short. Now that the cascades reach the far rim, those slabs are shadowed
  for the whole pass. This is more correct, but it is less warm light in frame.
- **The shading is flatter overall.** The near rock's relief is a normal perturbation along the strata only, and it
  cannot tilt across them, because the frame's bitangent comes from uv.y and is ~0. gate2e's per-pixel geometry still
  reads crisper close up.
- **The opening frame's near steep face shows vertical streaks.** This is the height grid's normals on a
  near-vertical face; it is not new in 1b.

## Performance

1920x1080, M2 Max, `--live-profile --mode headless --quality ultra --warmup-frames 60 --measure 40` (the same command
as Phase 1).

| | GPU median | scene | volumetrics | clouds | shadows |
|---|---|---|---|---|---|
| Phase 1 (mesh, final defaults) | 40.2 ms | n/a | n/a | — | n/a |
| Phase 1b | 69.2 ms | 35.2 ms | 10.4 ms | 18.2 ms | 3.0 ms |

- **Clouds** are the largest new cost. At 24 view steps and 1 light step they cost 5.4 ms, but the thin streaks
  dissolve into dither speckle. The scene keeps 96 and 3 (the art-before-optimisation rule), and a live tier would
  lower them.
- **Scene pass.** The 62-op program with detail normals, plus two extra lights, raised it by about 10 ms.

The offline 1080p render at 2x supersample took 508 s for 1380 frames (`08-perf-and-flicker.txt`).
