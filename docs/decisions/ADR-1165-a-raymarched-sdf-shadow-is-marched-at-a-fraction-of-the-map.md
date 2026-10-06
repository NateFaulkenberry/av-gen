# ADR-1165: A raymarched SDF's shadow is marched at a fraction of the shadow map's resolution

- Status: Accepted (proto/digital-mosh)
- Amends ADR-1160 (a raymarched SDF casts from the light's view) and ADR-034 (raymarched SDFs in the shadow maps).
- Found by: DIGITAL MOSH's live profile. Raymarched SDF shadows were the frame's largest cost, and the coordinator
  ruled that a merge blocker for every scene that casts them.

## Problem

ADR-1160 made a raymarched caster's shadow correct. A correct shadow costs:

```
(texels its bounds cover in each shadow view) x (march steps) x (its node count), per view, per frame
```

On DIGITAL MOSH the olive is five SDF objects of 90 to 94 nodes, with noise displacement and a field read in the
tree. It casts into three cascades of a 2048 atlas.

**A/B on the Dream** (`--live-profile --mode headless`, 1920x1080, M2 Max). All variants were run in one session on the
pass-3 scene:

| Variant | GPU p50 (Ultra) | Shadows category | Meaning |
|---|---|---|---|
| As ADR-1160 left it | 83.0 ms | 42.5 ms | |
| Every SDF `castShadows` off | 56.3 ms | 1.0 ms | the true SDF shadow cost: **27 ms** |
| 8 march steps instead of 48 | 66.8 ms | 31.7 ms | steps matter (about 60%) |
| 1024 atlas instead of 2048 | 64.9 ms | | texels matter |
| A texel-sized hit floor on the march | 82.1 ms | 41.8 ms | convergence does not matter (reverted) |
| Only the trunk casting / only one limb / only the Tanguy object | 60.9 / 64.0 / 56.3 ms | | about 5 to 8 ms per tree object |

The category labels are intervals between pass-end timestamps, so cost moves between neighbouring categories. The
totals are what count. The cost is texels times steps, and the shadow atlas does not shrink with LIVE AUTO's render
scale. So the last rung of the live ladder still paid it in full: 19 ms of a 35 ms Emergency frame.

## Decision

`QualitySettings::sdfShadowScale` is a divisor of the shadow map's resolution: 1 is full, 2 is half, 4 is a quarter,
and the result is never smaller than 512 texels. When it is above 1, the raymarched casters are marched into a
low-resolution layer, and that layer is composited into the map.

1. **The low march.** For each shadow view there is one depth-only pass into a low-resolution `Depth24Plus` layer
   (`SdfRenderer::lowResShadowLayer`). It uses that view's own frame block and the unchanged shadow pipelines
   (`fs_sdf_shadow`, ADR-1160's rays), so a low texel holds exactly the depth a full texel at its centre would hold.
   Nothing is reimplemented.
2. **The composite.** Inside the view's ordinary full-resolution pass, after the meshes, one triangle
   (`shaders/sdf_shadow_composite.wgsl`) copies each texel's *nearest* low-resolution depth under the ordinary
   `Less` test. Empty texels discard. Nearest, not interpolated: blending a caster's depth with the cleared far plane
   across a silhouette would invent a surface halfway to the light. The map's filtering (PCF/PCSS) softens the
   coarser silhouette as it softens any other.
3. **Where it applies.**
   - **Tiers:** Offline keeps 1. Every offline frame marches every texel, and
     `QualityPolicy::assertOfflineIsUncompromised` now asserts it. Preview, Realtime and High take 2.
   - **Live ladder:** every strategy's Low and Emergency rungs take 4, as a floor. Medium and above keep the tier's
     value.
   - **Profiles and Optimize:** the levers are `sdfshadowhalf` and `sdfshadowquarter`. The project ceiling key is
     `sdfShadowScale` (`live.overrides`, the Optimize output). The A/B arms are `sdfshadowfull`, `sdfshadowhalf` and
     `sdfshadowquarter`, for `--ab` and `--quality-arm`.
   - **Editor:** the Live panel's "Levers now" names the current scale.
   - `SdfStats::shadowMarchResolution` reports the low layer's size, or 0 when the march went straight into the map.

## Measured

The A/B was ORIGINAL against OPTIMIZED in one process (`--quality-arm sdfshadowfull --compare <lever>`, ADR-1110).
Pictures were compared by the Quality Lab at matched settings. The noise floor is the self-difference of two
ORIGINAL runs.

| Scene (Ultra, 1080p) | Full → half | Picture, half | Full → quarter | Picture, quarter |
|---|---|---|---|---|
| DIGITAL MOSH, the Dream (pass 3: the olive large in frame) | | | 82.3 → 63.8 ms (**-18.5 ms**, -22.5%) | SSIM 0.9967 (floor 0.9989), mean \|d\| 0.13 steps, edges 6.9% different |
| DIGITAL MOSH, the Dream (pass 4 land: the olive small, on its knoll) | 59.8 → 53.5 ms (**-6.4 ms**) | SSIM 0.9991 (floor 0.9993), mean \|d\| 0.06 | 54.9 → 50.7 ms (-4.3 ms) | SSIM 0.9981, mean \|d\| 0.09 |
| Liminal with its SDFs casting (see below) | 132.3 → 96.7 ms (**-35.6 ms**, -26.9%) | SSIM 0.9999 (floor 1.0000), mean \|d\| 0.00 | 124.9 → 84.1 ms (**-40.8 ms**, -32.7%) | SSIM 0.9996, mean \|d\| 0.01 |

**The existing scene.** No example in the repository casts a raymarched SDF shadow today:

- Liminal sets `castShadows: false` on every one of its SDFs and has no shadow-casting light;
- the Space and Sonic VFX scenes are the same.

So no existing scene inherits ADR-1160's cost. What a scene would inherit by turning shadows on is measured on
`liminal.scene.json`, with both SDFs (the world and the stairwell) casting and a directional sun added. The infinite
interior's bounds cover every cascade, which makes it the worst case: 132 ms at full resolution, 97 ms at half.

**Correctness.** The test `A raymarched SDF's shadow marched at a fraction of the map is the full-resolution shadow`
(`[gpu][sdf]`) checks a sphere caster at scales 1, 2 and 4:
- the darkened area is within 10% of the full-resolution shadow's;
- the centroid is within 1.5 px;
- the stats name the low layer (at least 512 texels at 2 and 4, none at 1).

ADR-1160's tests run at the default, which is now half on the live tiers, and pass unchanged: the SDF's shadow still
matches a mesh caster's within 25% in area and 4 px in position from three vantages.

## Consequences

- Live frames with raymarched casters cost a quarter to a half of the shadow march. Offline is unchanged.
- **The live picture changes slightly where an SDF's shadow edge falls.** It is coarser by one or two shadow texels.
  The SSIMs above are the size of that change.
- One extra depth pass per shadow view, and one triangle per view. They are drawn only when a raymarched caster
  exists and the scale is above 1.
- Memory: one `Depth24Plus` array of views x (atlas / scale)^2. At half of a 2048 atlas with 8 views that is 32 MB,
  allocated only when used.

## Rejected alternatives

- **A cheaper march (fewer steps, or a texel-sized hit floor).** Fewer steps cut 16 ms but change the shadow's shape
  (ADR-1160's step test). The hit floor saved under 1 ms, because the rays converge quickly and the cost is the
  texels.
- **Caching the shadow of static SDFs.** The cascades refit to a moving camera every frame, and DIGITAL MOSH's
  casters deform every frame. Neither case ever hits a cache.
- **Bilinear upsampling of the low layer.** Wrong for depth at silhouettes; see above.
