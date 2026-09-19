# ADR-372: AgX was undone with the wrong curve, and three things were built but unreachable

- Status: Accepted (2026-09-19)
- Builds on ADR-039 (image formation, AgX the default operator), ADR-035 (auxiliary targets),
  ADR-040 (motion blur, atmosphere coupling), ADR-352 (the glTF BRDF gains energy at grazing),
  ADR-367 (soft particles), ADR-368 (a disabled path is a property of the encoder).
- Continues the Image/Look work. The four items here are the "deliberately not done" list from
  ADR-368's session, taken in the order the coordinator set.

## 1. AgX's output was converted back with a curve that is not the inverse

`shaders/tonemap.wgsl`'s `agx()` returns an **sRGB-encoded** value, and `fs_main` re-encodes with
`linearToSrgb` — the exact IEC 61966-2-1 piecewise curve. For that pair to be a round trip, `agx()`
has to undo *exactly* that encode. It used `pow(v, 2.2)`, which is not its inverse.

**Measured before the change**, over AgX's whole output range `v ∈ [0, 1]`, comparing
`linearToSrgb(pow(v, 2.2))` against the `v` that should have come out:

| quantity | value |
|---|---|
| worst absolute error | **−0.0335 at v = 0.061** (−8.55 of 255) |
| worst 8-bit code difference | **9 levels, at v = 0.0495** |
| range where the 8-bit code differs at all | v = 0.002 … 0.998 |
| range where it differs by ≥ 2 codes | v = 0.006 … 0.849 — **84% of the range** |

"Worst in the toe" was the right direction and the wrong magnitude. Translated into scene-linear
greys through the full AgX transform:

| scene-linear grey | AgX v | old byte | new byte | delta |
|---|---|---|---|---|
| 0.00562 (−5 stops) | 0.0611 | 7 | 16 | **+9** |
| 0.01125 | 0.1070 | 21 | 27 | +6 |
| 0.02250 | 0.1752 | 40 | 45 | +5 |
| 0.04500 | 0.2657 | 66 | 68 | +2 |
| 0.09000 | 0.3751 | 95 | 96 | +1 |
| 0.18000 (mid grey) | 0.4967 | 128 | 127 | −1 |
| 0.36000 | 0.6211 | 160 | 158 | −2 |

**The deepest shadows were being more than halved** — 7 where 16 was intended — while the midtones
were 1–2 levels bright. AgX is the default operator (ADR-039), so this was in every shipped frame.

**The fix** is an exact piecewise `srgbToLinear` in `tonemap.wgsl`, and the same correction in the
CPU mirror in `tests/rendering/test_image_formation_gpu.cpp`. The mirror mattered: it carried the
*same* wrong inverse, so it confirmed the shader was consistent with itself and never that it was
right. A parity test between two copies of one mistake is not a parity test.

### Why the model can be trusted

The Python model of the transform reproduces **every** previously documented AgX value exactly —
`docs/image-formation.md`'s `(255, 210, 175)` for scene-linear `(8, 1, 0.2)`, and all seven rows of
`docs/hdr-lab/README.md`'s grey ramp (128 / 174 / 202 / 224 / 239 / 254 / 255). It is therefore
faithful to the shader it is predicting, and its predictions for the new values are what the
re-baseline commit is checked against.

## 2. Three things that were built and could not be reached

The failure family that has now bitten five times in this session. All three are fixed, and all
three are **default-neutral by construction** rather than by hope.

**`post/output/sharpenId`** — registered, round-tripping, shader path written and tested, and unable
to affect any frame a user rendered, because nothing ever assigned `PostFrameInputs::identifier`.
One line in `scene_renderer.cpp`. Default-neutral because `fs_sharpen`'s mask is
`identifierAvailable > 0.5 && maskId != 0u`, and `sharpenId` defaults to 0.

**`motionBlurSamples`, `motionBlurMaxRadius`, `motionBlurTileSize`** — read by the shader on every
frame that blurs, registered nowhere, so no scene and no project could set them. `amount` was the
only reachable motion-blur control. Their new parameter ranges are the clamps `PostProcessor::run`
already applies, so a slider cannot now express a value the chain would silently alter — a control
whose top half does nothing is the same defect wearing a different hat.

**`Framebuffer::worstAlbedo`** — allocated, filled per pixel on every `--pt-probe` run, written to
no EXR channel and read by nothing. **Decision: wire it, not delete it.** The data is computed and
the intent is documented — ADR-352's aggregate table says a material gains energy, this says *which
pixels* — so the missing piece was one channel. It is now `worstAlbedo.X` in the AOV EXR, present
only when the probe ran. `.X` and not `.R` because a directional albedo is a ratio, not a colour
(the same rule that gives `normal.X/Y/Z` and `motion.X/Y`).

And the other half of the same gap: `AlbedoProbeReport::format()` — the per-material breakdown
ADR-352 describes — was called from `test_pathtrace_albedo_probe.cpp` and nowhere else, so a person
running `--pt-probe` from the command line got a one-line summary and none of the detail. It is now
printed on the CLI path. **A report only a test can read is not a report.**

## 3. `fogCorrection` compared three different quantities as though they were one

`shaders/particles.wgsl`'s ADR-040 atmosphere coupling:

```wgsl
let dist = length(toParticle);                                       // EUCLIDEAN ray distance
let sceneDist = min(textureLoad(linearDepthTex, pixel, 0).x,         // VIEW-SPACE Z
                    params.fog2.x);                                  // EUCLIDEAN march limit
```

All three were treated as one quantity. They agree only on the optical axis. Measured at the default
42° vertical FOV on 16:9:

| position in frame | angle from axis | euclidean / view-Z |
|---|---|---|
| centre | 0° | 1.000 |
| top edge | 21.0° | 1.071 (+7.1%) |
| side edge | 34.3° | 1.211 (+21.1%) |
| corner | 38.1° | **1.270 (+27.0%)** |

The consequence is not a small error but a **silent switch-off**. The next line takes
`max(sceneDist, dist)`, so understating the surface distance by up to 27% makes the surface read as
nearer than the particle, the two transmittances become equal, and the correction returns 1.0 —
strongest exactly at the frame corners, where a particle in front of distant geometry stayed
over-fogged. That is the defect ADR-040 added this function to remove.

`softParticleFade`, twenty lines below in the same file, already solves the same problem the other
way and its comment names the trap explicitly. This function did not get the memo. The conversion
here goes *to* euclidean rather than to view-Z because `fogTransmittance` marches along a direction
for a distance, so euclidean is the unit it wants. The camera axis is derived the way
`softParticleFade` derives it — `cross(cameraRight, cameraUp)` — so no new uniform is needed.

## 4. The re-baseline, and what actually moved

Rendered both ways at 1920x1080, t = 8.00 s, through `tools/gpu-lock.sh`: the **baseline is a
separate worktree at main's tip (`f3642f97`) built from scratch**, so the only difference between
the two binaries is this change.

| arm | delta range (of 255) | lifted | darkened |
|---|---|---|---|
| `_ck-view-hero` | **−2 … +9** | 91.2% | 6.0% |
| `_ck-view-wide` | **−2 … +9** | 95.2% | 3.7% |
| `_ck-before-hero` | **−2 … +9** | 89.5% | 4.6% |

**Every one of the 6.2 million channel samples in every arm landed inside −2 … +9**, which is
exactly the bound the mismatch predicts and not one code value wider. That was the stop condition:
had anything moved further, it would have been a second bug wearing this one's clothes. Nothing did.

These are night scenes, so most of the frame sits in the part of the curve the old code was
crushing, which is why 90%+ of samples lift. A daylight frame moves far less; the midtone delta is
−1 to −2 everywhere.

**On the committed PNGs under `examples/treeisland/renders/cosmickey/`**: 18 frames, and *nothing
reads them* — no test, no doc. They are the recorded output of ADR-358's investigation, and the
`before/` and `after/` arms are two scene configurations, not two binaries. **They are deliberately
not regenerated.** Replacing the frames of a finished investigation with today's renderer would
destroy the record that investigation exists to be, and they are not baselines anything is checked
against. Anyone comparing a fresh render against them will see the shadow lift above; that is this
change, not a regression.

**The documentation tables that quote AgX numbers *are* updated**, because those are claims about
what the renderer does now: `docs/image-formation.md` (the `(8, 1, 0.2)` case and the four-stop ramp)
and `docs/hdr-lab/README.md` (the grey ramp, the shipped-defaults ramp and the hue table). Every one
of their old values was reproduced exactly by the model before being replaced, which is what makes
the new values trustworthy rather than merely different.



## Consequences

- Every AgX frame changes. Shadows lift, midtones drop 1–2 levels. Grades tuned against the old
  behaviour were tuned against a bug; nothing is re-tuned here, because that is an art decision and
  this is a correctness one.
- The three unreachable controls are reachable and all default to their previous behaviour, so no
  existing frame moves because of them.
- Fog-coupled particle frames change toward the corners and not at the centre, which is the
  signature to expect and the one to check a suspicious diff against.
