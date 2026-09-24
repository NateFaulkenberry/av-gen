# ADR-705: one fog law, one density, and Horizon Density on top of it

Status: **accepted.** Date: 2026-09-24. Resolves ADR-569, which was a finding that changed no
behaviour. The owner ruled for its option 2. Also implements the brief's §7 Horizon Density, the
last Phase D control, which ADR-569 had blocked.

## The ruling

> Make the surface pass Beer--Lambert, with its density derived from the volumetric one. One law,
> one density. Every scene's look changes, most of them a lot, and that cost is accepted. Glowmere's
> depth separation must come back. Render before and after for every affected scene. Re-tune where
> an intended look is lost, and say which scenes were re-tuned and which were left changed. Then
> add Horizon Density on top.

## The census

The census is `python3 tools/fog_law_census.py`, run on this branch before any code changed. It
counts value > 0 over the 105 tracked shipped scenes, with no `_` probe scenes:

| | scenes |
|---|---|
| both laws active | **36** |
| surface law only | **25** |
| march law only | 2 (`grid-catchup-lab`, `volumetric-atmosphere-lab`) |
| neither | 42 |

**All 63 scenes that set either law are affected**, the two march-only ones included, because the
surface pass now also draws their air past `volumeMaxDistance`. The 12 non-probe projects whose
parameters, presets, routes or tracks drive the fog are affected too. With `--all` the probes add 4
both-law scenes and 1 march-only scene, plus 21 probe projects; they were migrated mechanically and
not re-tuned. The census tool now also prints the new state: 0 both, 0 surface-only, and 63 fogged
scenes, of which 38 are marched and 25 integrated only.

## The law

One medium. Its extinction per metre is `sigma(p) = volumeDensity * volumeAbsorption *
heightProfile(p)`, and its colour is `fogColor` wherever the closed form carries it. **Each metre of
a view ray is counted once**, by whichever pass carries it:

- **The march** covers `[0, R]`, where `R = volumeMaxDistance`. It stops at the depth buffer, as it
  always has, and it lights the air.
- **The surface pass** (`applyFog`) integrates the same air in closed form over `[R, d]` only:

      T_surface = exp( -volumeDensity * volumeAbsorption * (d - R) * mix(1, mean, fogHeightAmount)
                       * (1 + horizonDensity * (R + d) / 2000) )
      colour    = mix(fogColor, lit, T_surface)

  `mean` is the height layer's mean over the segment, taken with the ADR-567 antiderivative. `R` is
  `frame.fogHeight.w`, filled by `VolumeRenderer::surfaceFogStart`. It holds the march's own clamp
  of `volumeMaxDistance` when the scene has a march, and 0 when it does not: the density is 0 or
  `volumeMaxDistance` is 0.

  **It ignores the renderer's debug pass toggle, on purpose.** `--disable volume` has to remove
  the march and exactly what the march draws. The first version handed the march's segment to the
  surface pass whenever the pass was toggled off. The Volumetric Lab's "the two ways of not running
  the volume agree at the byte" failed on it: 151,604 bytes differed, by at most 3 levels. That
  version made the diagnostic repaint the air instead of subtracting it, so the lab case was right
  and the code was changed.

The two transmittances multiply to `exp(-integral of sigma)` over the whole ray.

**`volumeMaxDistance` 0 now means "no march at all"**, and `VolumeRenderer::enabled()` skips the
pass for it. That is how the 25 surface-only scenes stay cheap. They have a density and no march, so
the closed form carries their whole ray.

**Why a handover and not both passes over the whole ray.** The march already extinguishes every
surface nearer than `R`. If the surface pass also integrated `[0, R]`, the near segment would be
counted twice, `T^2`. That is one density drawn as two, which is the ADR-569 problem under a new
name.

`Environment::fogDensity`, the `scene/fogDensity` parameter, its scene-file key and the day-night
`fogDensity` curve are **removed, not aliased** (ADR-441).

## Migration (commit 1, mechanical only)

- **The 36 both-law scenes** lose `fogDensity`. `volumeDensity * volumeAbsorption` is now their
  surface density.
- **The 25 surface-only scenes** get `volumeDensity = sqrt(ln 2) * fogDensity / volumeAbsorption` and
  `volumeMaxDistance 0`. At that density both laws let half the light through at the same distance.
- **Drivers of `fogDensity`** are retargeted to `volumeDensity`, scaled by the scene's own
  `volumeDensity / fogDensity`: the Night Shift track and the Season route. Drivers whose other
  target already drove `volumeDensity` drop the fog target instead: the Infinite atmosphere knob,
  and the Infinite Temple director's three knobs. Presets and looks that set both keys drop the fog
  key. The Worlds presets and the AI few-shot example convert at `sqrt(ln 2) / 0.5`.
- **The day-night curve** now drives `volumeDensity`, converted by the same rule.
- **The world composer**: `atmosphere.fog` scales the one density, and `lighting.volumetric` scales
  `volumeScattering`. `ArtDirectionProfile` and `TreeLook` lose their `fogDensity`.
- **Project scene hashes** (`assets.scene.path.sha256`/`size`) are recomputed wherever a scene
  changed.

## Evidence

Every affected scene was rendered at a fixed second before the change, after the mechanical
migration and after re-tuning. The 63 scenes were rendered at 4 s and 960x540, and 11 projects at
8 s. Glowmere Valley 2's project was rendered at 1280x720 with a depth AOV, at supersample 1 because
the AOV requires it, at 3, 14, 38, 44, 50 and 68 s. The same seconds were used in every arm. 3, 50
and 68 s are the wide establishing shots with depth.

The renders are under
`/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/98709788-977c-4996-8ad0-299492e98a7d/scratchpad/horizon/`:

- `before/`, `after-raw/`, `after/`, and `after-horizon/` for Infinite: PNG per scene, plus
  `GLOWMERE__t*.depth.exr`.
- `sheets/final/<scene>.png`: before | after-raw | after-retune, one per scene, and `_contact_*.png`.
- `sheets/final-horizon/`: the Infinite sheets with the Horizon Density arm.
- `sheets/glowmere_horizon_off_on.png`: Glowmere at `horizonDensity` 0, 1, 2, 4 and 8.

**Each arm was rendered with its own frozen binary and shader copy.** The build under test and the
shaders loaded at run time can therefore not drift between arms.

### Glowmere's depth separation, by number

This is mean luma, 0-255 on the output, in view-depth bands from the depth AOV: near < 60 m, mid
60-180 m, far 180-600 m. The standard deviation of the far band is the veil. Fog flattens distant
texture, so the lower the deviation, the more the distance reads as distance.

| second | band | before | after-raw | after-retune |
|---|---|---|---|---|
| 3 s | far mean / sd | 16.8 / 11.3 | 15.5 / 11.9 | **16.0 / 11.4** |
| 3 s | near - far | 68.4 | 69.7 | **68.7** |
| 50 s | far mean / sd | 25.3 / 16.1 | 24.7 / 17.1 | **25.5 / 16.5** |
| 68 s | far mean / sd | 24.6 / 21.6 | 24.0 / 23.1 | **24.5 / 22.2** |
| 68 s | mid mean | 30.6 | 29.9 | 28.8 |

The raw unification lifted the far band's texture and darkened it. The re-tune puts both within
about a level of before. The cost is the mid band, which reads 0.6-1.8 levels darker. At
`horizonDensity` 1 the far-band deviation matches before exactly (11.2 / 16.1 / 21.6), with the mid
band a further 0.3 darker. It is recorded here as the knob to reach for, not shipped.

**The finding that the numbers make plain: the 48x did not happen on screen.** ADR-569's 48x was
worked out on uniform air. The flagship scene sets `fogHeightAmount 1`. Its camera sits 10-30 m up
and its mist layer ends 4 m above the ground with a falloff of 0.1, so the old surface fog was
already integrated through thin air and was mild. The whole-frame difference before the re-tune was
0.5-1.2 levels. The scenes where the unification cost the most were not the flagship. Glowmere
Stylized's scene lost the veil on a near hill (6.4). Infinite (12.2 as a project) and Alien Wander
(10.6) lost their far haze. First Block (8.0), a surface-only scene, went the other way: its near
city fogged hard.

## Re-tuned scenes (commits 2 and 4): old -> new

Each value was chosen by a small grid over density, march reach and scattering. The criterion was
the frame's mean absolute difference against `before`, then a check by eye on the three-arm sheet.
The last column is that difference, raw -> final.

| scene / project | old | new | why | diff |
|---|---|---|---|---|
| `world/glowmere-valley-2.json` + `.scene.json` | vd 0.006, scatter 0.5, reach 320 | vd 0.009, scatter 0.6, reach 220 (scene) | far veil back, see the table above | 1.13/1.21 -> 0.96/1.09 (project), 1.61 -> 1.08 |
| `world/glowmere-atmospherics.json` + `.scene.json` | same as valley-2 | same as valley-2 | same family, same loss | 1.35 -> 0.97 |
| `world/glowmere-stylized.scene.json` | vd 0.006, reach 320 | vd 0.012, reach **40** | the veil on the near hill needs the closed form, which fades to `fogColor`, to start sooner | 6.40 -> 1.11 |
| `world/glowmere-stylized.json` (project) | (reach from its scene) | adds `scene/volumeMaxDistance` 320 | this project's own camera preferred the old reach; the scene's new 40 made it 2.71 | 0.84 -> 0.84 |
| `composition/glowmere-lyrics.json` | vd 0.006 | vd 0.012 | shares the stylized scene | 4.44 -> 0.93 |
| `infinite/infinite.scene.json` | vd 0.00055, reach 300, scatter 1.0 | vd 0.00165, reach 40, scatter 0.577, **horizonDensity 6** | far blocks had lost their haze. Density alone half-restored it (3.19); horizon finishes it | 4.80 -> 2.02 |
| `infinite/infinite.json` | atmosphere knob vd 0.00018..0.0016 | 0.00054..0.0048 | scales with the scene | 12.18 -> 3.44 |
| `machine/machine.scene.json` | vd 0.0016, reach 260, scatter 0.8 | vd 0.0032, reach 40, scatter 0.566 | background columns lost their grey | 3.15 -> 0.47 |
| `machine/machine.json` | preset vd 0.0011/0.0021/0.0031/0.0019 | x2 | scales with the scene | 3.83 -> 0.86 |
| `reassembly/reassembly.scene.json` | vd 0.0016, reach 110, scatter 0.8 | vd 0.0048, reach 15, scatter 0.462 | back floor | 2.45 -> 1.00 |
| `characters/alien-wander.scene.json` | vd 0.016, reach 140, scatter 0.55 | vd 0.064, reach 15, scatter 0.3 | hill haze. **Only partly restored**, see below | 10.58 -> 6.58 |
| `city/first-block.scene.json` | fog 0.0035 (mechanical vd 0.00583) | vd 0.00175 | the half-distance conversion fogged the near city | 8.01 -> 1.59 |
| `cathedral/cathedral.scene.json` | fog 0.016 (0.0266) | vd 0.0133 | as above | 2.98 -> 1.97 |
| `organic/plants.scene.json` | fog 0.02 (0.0333) | vd 0.01 | as above | 1.89 -> 0.37 |
| `helix/helix.scene.json` | fog 0.02 (0.0333) | vd 0.0167 | as above | 1.68 -> 0.86 |
| `organic/organic.scene.json` | fog 0.012 (0.0200) | vd 0.004 | as above | 1.71 -> 0.13 |
| `organic/fungi.scene.json` | fog 0.03 (0.0500) | vd 0.0125 | as above | 1.67 -> 0.25 |
| `worlds/worlds.scene.json` + `worlds.json` presets | fog 0.012 (0.0200); presets x1.665 | vd 0.009; presets x0.45 | as above | 1.76 -> 0.70, project 1.73 -> 1.21 |
| `qa/renderer-qa-water.scene.json` | fog 0.002 (0.00333) | vd 0.00067 | as above | 1.71 -> 0.23 |
| `camera/behaviors.scene.json` | fog 0.004 (0.00666) | vd 0.00133 | as above | 1.36 -> 0.14 |

**The surface-only pattern is systematic and worth knowing.** Matching the two laws at the distance
where half the light gets through, which is what the mechanical conversion did, fogs these scenes'
near subjects harder than exp-squared ever did. Beer--Lambert starts extinguishing at the first
metre, and exp-squared hardly starts before `0.3 / fogDensity`. For small, close-framed scenes the
density that restores the look is 0.2-0.5x the mechanical one.

## Scenes left changed

These scenes were rendered, looked at and deliberately left as the unified law draws them. The
number is the frame's mean absolute difference against `before`:

- **No re-tune did better than the mechanical value:** `labs/character/autonomy-demo` (3.34),
  `character-intelligence-lab` (2.91), `river-crossing` (2.12), `guard-post` (1.82),
  `perception-crowd` (1.45) and `pasture` (1.24); `world/world` (2.02); `chamber` (1.99);
  `temple` (1.84); `tide` (1.24). In each the far subjects are a little less fogged and the near
  ones a little more.
- **Glowmere members where the raw result was already the closest:** the multicam project (2.62),
  whose surface-only air converted mechanically, and the multicam scene (1.36); both `-song` files
  (0.98 / 0.97).
- **Visibly the same:** `treeisland/tree-of-life-ocean-world` (2.02), whose haze is the day-night
  curve and is the water's shading difference; `P__hyperspace` (1.44); and `moonrise`,
  `alien-squad`, `alien`, `hero`, `stress`, `terrain`, `snow`, `farm-animals`, `hyperspace`,
  `grove` and `lab`, all under 0.8. Night Shift, as a project at 8 s and 40 s, is 0.55 and 0.80;
  its scene file on its own renders a flat frame at 4 s and 20 s, 0.00.
- **Unchanged to the pixel or within 0.1:** all seven weather scenes and the Season project, both
  tractor-beam labs, `constellation`, `qa/renderer-qa`, the nine footik and motionmatch labs
  (their camera is close and the old surface fog contributed nothing), and the two march-only labs.

**Not restored, said plainly.** Alien Wander. Its old look was exp-squared at 0.02, which crosses
the march at 20 m: near air clean and hills buried. Beer--Lambert cannot give that curve in 150 m.
Horizon Density is per kilometre and does not move at that range: every arm tried was worse, 7.1 to
8.7. The re-tune brings the haze back onto the hills, but bluer and with the hill silhouettes more
distinct than before. It is shipped as the best of the arms tried.

## Horizon Density (commit 3)

`scene/horizonDensity` has a hard range of 0..8, a soft range of 0..2 and a default of 0. The air
at distance `s` from the eye is `1 + horizonDensity * s / 1000` times as dense, so 1 doubles it a
kilometre out.

- **The march** multiplies every environment-layer sample by that factor. Placed media are exempt:
  they have their own size and density (ADR-564).
- **The surface pass** integrates the factor over `[R, d]`, which gives `(1 + h (R + d) / 2000)`.
  That is exact for uniform air, and first-order under a height layer, where it is taken outside the
  layer's mean.
- **The particle transmittance estimate** multiplies it into its midpoint samples. Without it the
  particle coupling would correct for a different atmosphere from the one it is in: ADR-567's third
  reader.
- **Reach.** It is an ordinary parameter, so it can be modulated. The scene serialiser writes it
  only when non-zero, and the project serialiser keeps it as a parameter. It is the Environment
  panel's **Horizon density** row, it is in the AI environment tools' vocabulary, and it is the
  Infinite Temple director's depth knob, range 0..2, which had lost its fog target.

**Why it is not a third law.** It is a property of the one density, read from one number by every
pass that draws that density. The test below checks that the march and the surface pass agree with
it on.

**Off is off.** All 82 review renders at `horizonDensity` 0 are **pixel-identical** to the build
without the control (`after-h0/` against `after/`).

## Tests

- `test_volume_gpu.cpp`, **"The surface fog and the march agree on the transmittance of the same
  air"**, is the one-law invariant. An unlit box sits 36.5 m out, with black fog and no scatter, so
  the pixel ratio is the transmittance itself. The march alone, the surface pass alone and a 12 m
  handover agree to 0.3%, and match `exp(-sigma * 36.5)` at extinctions 0.01, 0.033 and 0.06.
  Measured: 0.6917 / 0.6912 / 0.6908, 0.2996 / 0.2986 / 0.2984, 0.1122 / 0.1114 / 0.1113.
  - **Broken by restoring the old law** (`exp(-(sigma d)^2)`): surface 0.873 / 0.233 / 0.008 against
    the march's 0.692 / 0.300 / 0.112, three failures.
  - **Broken by double counting** (the surface pass ignoring the handover): the march reads 0.480 /
    0.090 / 0.013, three failures.
- **Horizon Density at 0 renders exactly as the environment's default.** The hash is equal at 0 and
  different at 1.
- **Horizon Density thickens the far air monotonically and leaves the near air alone.** The far box
  reads 0.2888, 0.2813, 0.2737 and 0.2598 at 0, 2, 4 and 8. The near box moves by less than 0.1%.
- **Horizon Density is one law.** Marched 0.2850, surface 0.2838, handover 0.2838, against a
  closed form of 0.2851. With the factor removed from the march alone, the case fails.
- `test_composition.cpp`, **"Horizon Density is a scene parameter that both serialisers keep"**,
  checks the path end to end. A scene file sets the value and it reaches the environment. A route
  modulates the final, and the renderer's environment follows. The scene serialiser writes the base
  and round-trips it. `parameterToJson` / `parameterFromJson` keep it, which is what a project's
  parameter block uses. An unset value writes nothing.
- **Tests that encoded the old law, changed and why:**
  - `test_procedural_gpu`'s "fog pulls toward colour": 0.05 exp-squared becomes 0.2 at absorption
    0.5 with the march off. That is the same exp(-4) at 40 m, now Beer--Lambert.
  - `test_sdf_gpu`: 0.3 becomes 0.6, march off. The case only checks direction.
  - The three `test_volume_gpu` surface-fog cases (ADR-058's layer integration and the
    styled-ambient defaults): 0.02 becomes 0.0333 and 0.01 becomes 0.0167, the equal half-distance
    conversion, with `volumeMaxDistance` 0 so they still see the surface pass alone, which is what
    they are about.
  - `test_world_composer`: `atmosphere.fog` now scales `volumeDensity`.
  - `test_day_night`'s hash fields: the curve was renamed.
  - `test_environment_panel`: asks for the new row and asserts `scene/fogDensity` no longer
    registers.
  - The sequencer, AI tool, camera director and control-plane tests used `scene/fogDensity` as "some
    registered parameter", and now use `scene/volumeDensity`.

**Full suites, on the branch's final code, after a second `cmake --build` that did no work:**

- `avgen_render_tests`: **423 cases**, the baseline 419 plus the 4 above. Exit 0. It was this suite
  that found the handover-toggle defect described under "The law".
- `avgen_tests`: **3,193 cases**, the baseline 3,192 plus the 1 above. Exit 0. Its single
  `FAILED:` is the expected `[!shouldfail]` at `test_character_lab_slopes.cpp:187`.
  - One earlier run also failed the motion-matching timing ratio at `test_matching_loop.cpp:207`
    (2.6x against a 1.2x bound). That was while other worktrees' suites were loading the machine.
    The case passes 3 of 3 in isolation at 1.0x, and the final full run was clean. It is unrelated
    to fog.

## Consequences

- **`fogHeightAmount` below 1 is still a way for the two passes to disagree.** At 0 the surface
  pass treats its segment as uniform air while the march thins it with height. It is geometry, not
  the law or the density, and 48 of the 63 fogged scenes leave it below 1. Making it always 1 would be
  a second re-lighting pass, so it is named here and not done.
- **The surface-only scenes pay nothing.** `volumeMaxDistance` 0 keeps their march off.
- **The Glowmere flagship's retune is a density and reach change, not a horizon one.** Horizon
  Density ships at 0 everywhere except Infinite.

## Revisit when

- Someone wants the old exp-squared "clean foreground, buried horizon" in a scene smaller than a
  kilometre. Horizon Density's unit is per kilometre, so a scene that small needs a much larger
  value than its soft range. Alien Wander is that case.
- `fogHeightAmount` is reconsidered, per the first consequence above.
- `volume.wgsl`'s step redistribution lands (the production plan's item 3). It moves the march's
  quadrature, and the one-law test's 3% tolerance is what will say whether the handover still
  meets.
