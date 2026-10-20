# Rainbow World 12: Stage 8 and final report

This report covers Stage 8 (final review and optimisation) and summarises the scene as handed back.

Branch `art/rainbow-world`. The reports for each stage are 05-11 in this folder.

Review images are in `~/Desktop/av-gen-review/74-rainbow-world/stage8/`:

| File | What it shows |
|---|---|
| `clip-final-24-44s.mp4` | 20 s at 1280x720: run, crossing, arch |
| `01-final-ride-stills.png` | 20 stills, one every 30 s round the ride |
| `02-dusk-prism-meadows.png` | The last two look fixes |
| `03-gpu-hot-frames.png` | The frames that set the GPU tail |

## 1. What the owner needs to know first

- **On a fresh clone the example will not open until it is built.**
  - The scene (`examples/rainbow-world/scene/`, 121 MB of GLBs and the composition) is generated and gitignored. Only
    the live project `examples/rainbow-world/rainbow-world.json` is committed.
  - Until the build has run, the Examples > Live menu lists **"Rainbow World  (needs generating)"** greyed out. Its
    tooltip names the missing scene file and the command to make it.
- **To build it:**
  - It needs Blender 5.x and the six CC0 Poly Haven rocks already in `assets/terrain/rocks/`. They are fetched by
    `tools/fetch_polyhaven.py` and listed in `assets/rainbow-world.manifest.json`.
  - The command is `python3 examples/rainbow-world/build.py`. It takes about 90 s on this machine.
  - `--scene-only` rebuilds just the world from the built assets.
- **No audio file.** It is a live scene: play any music into the live input.
- **MIDI:**

  | CC | Control |
  |---|---|
  | 1 | Rainbow glow |
  | 2 | Lens |
  | 5 | Haze |
  | 6 | Light |
  | 7 | Colour |
  | 9 | Look |
  | 10 | Look lock |

  With no controller, every control rests at the scene as built.

## 2. Stage 8: optimisation

All figures are from the live profile at Ultra 1080p, headless, in 60 s windows (1,800 frames each).

| | p50 | p95 | Notes |
|---|---|---|---|
| Stage 7 | 24.9-28.6 | 32.0-35.9 | |
| **Final** | **22.6-24.4** | **29.5-35.3** | The 120-180 s window is at the 29.3 ms budget. The windows with a rim crossing and an arch hold are over at p95. |

Each change below was measured by A/B on the same frames:

| Change | Effect |
|---|---|
| Leaf cards thin from 22 m (were 40 m; ADR-1548) | The near cards were the p95 in the arch windows: removing all leaves took p95 from 36.2 to 30.7 there |
| Two shadow cascades (were the tier's default) | -1.5 to -2 ms |
| Band LOD at 160/420/950 m (were 240/600/1300) | about -1.5 ms p50; no seam (ADR-1546 holds the borders) |
| Hanging garden: 24,680 tufts (were 54,000) | The crossings' tail |
| Cloud-head LOD from 700 m (was 1,200 m) | The crossings, which look down at the sea |
| Arch rise 12 m (was 16 m) | The rise showed the most world at once |
| Rainbows out of the shadow maps (ADR-1547) | Removes their solid shadow arcs |

Measured and rejected:
- The ground program's micro-detail and rim ops: under 0.3 ms.
- Shadow range 170 m: under 1 ms.
- Quality High: p95 unchanged.
- Render scale (the Auto ladder settled at 0.71): p95 34.5. The tail is not resolution-bound, so render scale, the
  last resort, would not fix it.

**Not met: p95 over budget in the crossing and arch windows** (frames at 29-51 s and 284-296 s). Those windows also
vary by up to 20 ms between runs, and a fresh start at the same moment measures 24-29 ms. A continuous run carries
some history cost I have not pinned down. Section 5 has the next levers.

## 3. Final review

The 20 stills, the 20 s clip, and the five looks were inspected against brief section 9.

- **Regressions:** none found against Stage 7.
- **Missing assets:** none. The from-scratch build produces the same scene.
- **Materials:**
  - The GTAO ring stripes are fixed (AO off, Stage 5).
  - The rainbow shadows are fixed (ADR-1547).
- **Lighting:** all five looks keep a green meadow. Measured meadow hue (Bright Noon 0.22-0.25):

  | Look | Hue | Saturation |
  |---|---|---|
  | Golden Hour | 0.27-0.28 | |
  | Lavender Dusk | 0.29-0.35 | 0.35-0.43, lower than Noon's 0.6: the dusk light |
  | Prism | 0.27-0.33 | |

- **Camera clipping:** none. Trees, streakers and the lip gardens are all placed against the ride's clearance.
- **Repeated geometry:** the ride is the same path on both laps. The laps differ in where it holds.
- **Visible world edge:** none. Below is the cloud sea, out to 9 km, over the floor.

## 4. Engine changes (ADR 1540-1548; each one commit, with a test)

| ADR | Change |
|---|---|
| 1540 | The fisheye lens remaps the rectilinear frame |
| 1541 | A spline camera's up can follow the spline's frame |
| 1542 | A closed spline's roll can wind whole turns |
| 1545 | A procedural node can stay out of the shadow maps |
| 1546 | A chunked surface's mesh LOD can hold its border |
| 1547 | A gltf node can stay out of the shadow maps |
| 1548 | A procedural node's leaf cards thin instead of collapsing |

ADR-1543 and ADR-1544 were reserved and not needed. Mesh clouds replaced the volumetric plan.

## 5. Limitations

1. **The GPU p95 is over the 29.3 ms budget in the crossing and arch windows** (section 2). The median meets it. Next
   levers:
   - a lighter far set of leaf cards authored per tree;
   - fewer trees in the lip gardens;
   - an investigation of the history-dependent spikes in continuous runs.
2. **The look is performance state.** The CC 9 look selector is ADR-1420's shared palette system: unlocked, it
   advances on phrase boundaries, so a seek lands on the selected look rather than replaying advances. Everything
   else is seek-equals-play, bit-exact.
3. **The ride repeats its path every lap.** There are 506 s before the whole two-lap loop repeats.
4. **Lavender Dusk's meadow is less saturated than Noon's.** This is the dusk light; the meadow stays green.
5. **Rainbow audio response is moderate.** The arch emission is already near the tonemapper's shoulder; kicks also
   flash the bloom.
