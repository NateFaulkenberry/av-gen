# Generative R&D: performance

## Setup

All of this was measured on 2026-10-17. Every run went through `tools/gpu-lock.sh`, with the GPU shared with the
coordinator's offline renders between runs.

| | |
|---|---|
| Hardware | Apple M2 Max (Mac14,6), macOS 26.7.1 |
| Software | WebGPU / Dawn / Metal, release build |
| Command | `avgen --live-profile --project <scene>.json --audio assets/audio/feline-footwear.wav --target-fps 30 --size 1920x1080 --start 40` |
| Live | `--mode live --quality auto`: the editor loop with present, Fifo and the 1920×1080 projection window |
| Headless | `--mode headless --quality ultra`: the internal resolution is 1920×1080 |
| Window | 5 s measured after a warm-up of up to 3 s |

Every number below is **measured** unless it is marked as an estimate. The owner's target is 1080p output at 30 fps
under Auto. A live run "achieves" it when it has 0-2 deadline misses in about 150 frames.

## Results

| scene | live: Auto level (internal) | live GPU p50 / p95 | live misses | headless Ultra GPU p50 | CPU work p50 | grid layer cost (A/B) | grid state | memory (Dawn est.) |
|---|---|---|---|---|---|---|---|---|
| **Baselines** | | | | | | | | |
| Cymatic World (resonator) | Medium (1362×766) | 27.6 / 32.2 ms | 0 | 41.0 ms | 0.7-1.4 ms | n/a (bake grids 0.5-0.7 ms, timeline) | 46 MB | 337-403 MB |
| Resonant Silk (resonator) | Low (960×540) | 31.9 / 38.3 ms | 1 | 34.7 ms | 0.5-1.1 ms | n/a | none | 270-344 MB |
| **A, oscillators** | | | | | | | | |
| Phase Membrane | Medium (1362×766) | 26.0 / 29.6 ms | 2 | 36.1 ms | 0.5-1.1 ms | **2.6 ms** (384², r = 4) | 9.4 MB | 284-344 MB |
| Phase Wall (flaps) | **Ultra** (1920×1080) | 17.9 / 22.3 ms | 0 | 15.1 ms | 2.9-4.7 ms | **0.2 ms** (300×128) | 2.5 MB | 330-339 MB |
| **C, reaction-diffusion** | | | | | | | | |
| RD Relief | **High** (1632×918) | 28.3 / 32.1 ms | 0 | 33.2 ms | 0.7-3.6 ms | **7.5 ms** (384², 12 iterations) | 5.0 MB | 336-369 MB |
| RD Pins | Low (960×540) | 26.1 / 28.6 ms | 0 | 42.5 ms | 3.6-4.2 ms | **6.2 ms** (320², 12 iterations) | 3.5 MB | 251-344 MB |
| RD Bloom (hidden) | High (1632×918), fails | 80.3 / 103 ms | 51 | 50.5 ms | 3.5-4.2 ms | **32.7 ms** (100³, 20 iterations) | 32 MB | 435-553 MB |
| **B, flow** | | | | | | | | |
| Current Lines | **High** (1632×918) | 30.4 / 33.1 ms | 0 | 34.9 ms | 2.2-6.4 ms | **9.4 ms** (wake 256² and the fibre strands that read it) | 6.5 MB | 321-349 MB |
| Current Meadow (hidden) | Emergency (960×540), fails | 35.6 / 40.8 ms | 12 | 45.9 ms | 2.6-6.2 ms | 6.0 ms | 6.5 MB | 188-382 MB |

### Reading the table

- **The grid layer cost** is an A/B. It is the headless Ultra GPU median with the experiment's grid enabled, minus
  the same scene with that grid disabled (`enabled: false` in a throwaway copy).
  - It includes everything the grid's presence costs: the simulation kernels and the readers that skip their work
    when the grid is unbound. For Current Lines that is the 1,150 fibre strands' field reads.
  - It is a measured difference of two medians, so ±0.5 ms.
- **CPU work** is under 7 ms everywhere. Packing a field block per sub-step (ADR-1119) is visible but small. No
  experiment is CPU-bound.
- **Live frames** are paced by Fifo at 33.3 ms, so "frame" is not in the table. The GPU median and the misses say
  whether a scene is live-viable.

## What the costs are made of

**Oscillators are cheap.** The coupling kernel reads about 50 neighbours per cell at r = 4.
- The first version computed a Gaussian `exp()` per neighbour. That was 6.3 ms of GPU on the membrane.
- The weights are now separable, `g[|dx|]·g[|dz|]`, with five exponentials a cell. That brought it to **2.6 ms**.
  CPU and GPU were changed together; the parity test still holds at 2e-3.
- The wall's coarser grid costs 0.2 ms.
- The membrane's frame is its mesh: 79k vertices through a deformer, a depth prepass and a shadow pass (scene 31 ms
  at Ultra).

**Reaction-diffusion pays for its iterations.**
- A 512² plane at 16 iterations was 11.5 ms. 384² at 12 is **7.5 ms**, with patterns about a third larger, which the
  relief wears well (see the compare stills).
- The regime map is now baked once a frame into a 128² bake grid (ADR-1407) rather than evaluating a compound with
  fbm per cell per iteration. Its saving could not be separated from the resolution change (see the trap below); the
  stencil reads dominate either way.
- The 100³ volume behind the bloom costs 33 ms. It is **offline-only**.

**Flow.**
- The analytic base current is a static grid; nothing is stepped.
- The cost is the 256² vector wake: inject, self-advection, two diffusion sweeps and dissipation, at 4 sub-steps per
  frame (8 counting both grids' steps). Its readers add to that: 1,150 fibres × 40 segments × an RK2 step reading
  two grids.
- `fiberStrands` was 25 ms at 64 segments. Dropping to 40 segments brought it to about 9 ms with the wake.
- The meadow's 64k strands are the expensive part of that scene.

**Air.** The volumetric fog march cost **7.2 ms** in every scene that set `volumeDensity`, even at 0.0006. The
relief, pins and lines now have none; their framing closes the view instead.

## A measurement trap (for `docs/testing.md`, not fixed here)

**The profile's `particles/simulation` line times only the first grid's first pass.** `Simulation::update` puts its
timestamp writes on the first compute pass and nothing else. The GPU timeline attributes everything until the next
marked pass to whatever comes next.

What happened here:
- Adding a 128² regime bake grid ahead of the RD plane made `sim` fall from 8.91 ms to 0.07 ms.
- In the same run, `shadows` rose from 4.6 ms to 13.7 ms.
- The total did not change.

So a scene with several grids, or with a bake grid first, reports its simulation as nearly free. The A/B above is
the honest number. The fix belongs in `simulation.cpp`: end-of-last-pass timestamps.

## Viability

| experiment | live (1080p30, Auto) | offline |
|---|---|---|
| A, Phase Membrane | **Yes**, at Medium; it was at risk with 2 misses in one run and 1 in another | yes |
| A, Phase Wall | **Yes**, at Ultra with headroom | yes |
| C, RD Relief | **Yes**, at High, 0 misses | yes |
| C, RD Pins | Yes, at Low (the 40k pins' effector pass and shadows) | yes |
| C, RD Bloom | **No** (the 100³ volume) | yes; 12 fps at 640×360 headless in review renders |
| B, Current Lines | Yes, at High, 0 misses | yes |
| B, Meadow and Storm | No (64k strands), and weak as images | yes |

The resonator baselines on the same day: Cymatic World at Medium with 0 misses, and Resonant Silk at Low with 1.
The experiments sit in the same budget class, and the leads (Relief, Wall, Lines) sit a level or two higher.
