# 48. Night canyon lighting and water: engine audit and research

This is a research note, read-only. It answers the M3e §8/§9 question at engine level, plus the owner's addition, "especially
the water". Code references are against main 64c1fd94. The scene values come from `strange-attractor-flythrough.scene.json`:
the `night` look (index 3) and the base lights. No GPU renders were run, so every "cause" below is read from code and has
a stated way to confirm it.

## 0. Findings that explain the owner's screenshots

| # | Finding | Evidence | Owner complaint it explains |
|---|---|---|---|
| F1 | **The campfire casts no shadow map at all.** The atlas holds 8 views (`kMaxShadowViews = 8`, `src/rendering/shadow_math.hpp:20`). Directional lights are ordered first (`light_data.cpp:139-157`), so `attractor.sun` (the moon at night, always `castsShadow = true`, `composition.cpp:8111`) takes 4 cascades (`shadowCascades: 4`). The campfire's cube then needs 6 more, and 4 + 6 = 10 > 8, so `shadow_renderer.cpp:261` skips it **silently**. Only directional lights get a warning. Contact shadows are also off scene-wide (`environment.contactShadows: false`), so the fire light is **completely unshadowed**. | the code path; confirm with `stats.shadows.points == 0` / the Shadow Lab overlay at the camp | the fire "glows through" the Sasquatch; no grounding; the bank is lit as if the figures were not there; "very CG" |
| F2 | **The blob is most likely the moon's cascade shadow, displaced by a 3.2 m normal offset.** `sunShadowBias: 3.2` (flythrough `attractorWorld`) becomes `PunctualLight::shadowBias`, added straight to the normal offset in `shadows.wgsl:298`: every receiver is looked up 3.2 m off its surface. On the bank (its normal faces the camp), the lookup point sits 3.2 m out in the camp, so the bank shows a smeared, merged shadow of whatever stands about 3 m in front of it (the Sasquatch group, the pit, the boulder). Wide PCSS (the light's `softness` 1, so up to 24 texels; see B4) makes it the soft "capsule". On the ground the offset lifts the lookup above a 2.3 m Sasquatch, which kills contact. The fire cannot add a shadow (F1), so the darkness is the moon being blocked, while the fire (range 17, windowed) barely reaches the bank. | `composition.cpp:8112` comment: the 3.2 is a day-time anti-acne value for kilometre-scale rock; ACES toe (F5) crushes the result to black | the "huge soft black capsule matching no caster"; no contact shadows |
| F3 | **Spot and cube shadow depth bias is computed as if perspective depth were linear.** `reportView` sets `depthBias = (2*texel + 5 mm) / depthRange` (`shadow_math.cpp:418-419`), which is then subtracted from **hyperbolic** NDC depth (`shadows.wgsl:306`). For the campfire (range 17, near 0.085, 2048 px): the effective world bias is about 0.028·z² m. That is 0.11 m at 2 m, 0.45 m at 4 m, 1.0 m at 6 m and 1.8 m at 8 m, and up to 5× that at grazing angles (slope term). So even once F1 is fixed, fire shadows on the bank 6-8 m away would detach or vanish. | arithmetic on the code; GLM is ZO (`src/CMakeLists.txt:59`), and the depth pass writes raw clip depth (no `frag_depth`) | would reproduce "shadows don't match casters" after F1 is fixed |
| F4 | **Cube and spot shadows get plain PCF only.** PCSS runs only `if cascaded` (`shadows.wgsl:308`). The filter radius is `max(softness,0.2)*1.5` texels, fixed in texels, so the penumbra has no contact hardening. | `shadows.wgsl:307-312` | flat, uniform penumbra |
| F5 | **Night uses ACES-fitted (`post.tonemap: 0`), not the engine default AgX.** The Narkowicz fit's toe has a slope of 0.21 at zero: scene 0.01 → 0.0038 (−1.4 stops) and 0.005 → 0.0015 (−1.7 stops). A night frame lives there. ACES also skews saturated fire and cyan toward yellow-white. | `tonemap.wgsl:40-43`; values computed | the wide shot is near black; characters read as silhouettes; the craft glow goes white |
| F6 | **AO and ambient do almost nothing at night.** GTAO multiplies only ambient and IBL (`pbr_shade.wgsl:1188-1234`), and ambient is `env/intensity 0.012`. The AO radius is not authorable either: it is 5% of the camera-to-target distance, clamped to 0.15-4 m (`scene_renderer.cpp:3757`), which is why it drew fins on the canyon (04-canyon-phase1.md). | | turning AO on will not ground the camp; grounding must come from the light's own shadows and contact march |
| F7 | **The water gets no light at all from local lights.** It has no diffuse and no specular from points (`water.wgsl:1045-1048` walks directional lights only). The SSR source copy is taken **before particles** (`scene_renderer.cpp:5241-5262` copy; particles drawn at ~5380), so the flames and embers can never appear in the river. | | "especially the water": no fire streak and no warm pool on the river |
| F8 | **The night water settings make it a flat sky mirror.** `fresnel` (f0) is 0.35 (physical water is 0.02), `reflection` is 1.3, and `roughness` 0.03 with `ripple` 0.12 is near glass. `streak`, `reflectionRays`, `glints` and `sparkle` are all 0 (defaults, `water_surface.hpp`). The moon glint is GGX with α = 0.03² ≈ 0.001: a pinpoint, not a glitter path. | flythrough `attractorWorld.water`; `water.wgsl:953-954, 1051` | the water reads milky, flat or glowing, with no sharp highlights |
| F9 | **The craft spill is unshadowed and too strong.** `ufo-glow` is 90 (2.6× the fire) and `castsShadow` is false. Its colour (0.45, 0.85, 1.0) through ACES goes white. An unshadowed point under the hull lights the bank straight through the craft. | night look | "unmotivated white spill behind the craft, craft dark" |

Other audit facts:
- Flicker (ADR-1888) is a slow, deterministic intensity breath (it is seek-safe and owner-safe). It moves no position and no shadow.
- Local lights scatter in the volume (ADR-053), but they are unshadowed (ADR-570), and `volumeDensity` 1.25e-4 makes the
  fire's `volumetric: 0.2` invisible.
- There is no local GI: one global IBL cube (`pbr_shade.wgsl:1218`), with no probes, no SH bounce and no emissive-to-indirect. The canyon's "bounce" is
  hand-placed directional fills (`canyon-bounce`, `floor-bounce`).
- Fur is alpha-clip cards (ADR-1888, `blend_method CLIP`) through the skinned lit path. There is no hair BRDF and no transmission.
- There is no Purkinje or scotopic shift anywhere (grep). Night grading is only `post/grade/*` (lift, gamma, gain, temperature,
  saturation).
- Looks can set any registry parameter (`composition_portal.cpp:238`). These are available to a look: `lights/<name>/{castsShadow,
  shadowBias, shadowStrength, contactShadow, position, range, volumetric, outerCone...}`, `scene/shadowRange`,
  `scene/volume*`, `camera/exposure/compensation` and `post/tonemap/operator`. A `post/*` path that fails to resolve under the
  scene prefix logs "look: no parameter", so check the log. These are **not** available to a look: `attractorWorld.sunShadowBias`, `environment.shadowCascades`, `contactShadows` and
  the attractor sun's `castsShadow` and `contactShadow` (it is not an authored light).

## 1. How professionals do it (condensed)

- **Moonlight.** Use one high, cool source, often as a back or rim light to separate silhouettes. Keep it only slightly blue: heavy blue
  reads as theatrical. Expose healthily and take light away in the grade, so the shadows keep detail. The usual build is moonlight, then
  moonlight plus practicals, then practicals alone [1][2][3][4].
- **Firelight.** The fire is a low, warm (1000-2000 K) key that throws hard shadows *away* from itself, with a fast falloff
  (near black 5-8 m out). It needs a breathing flicker plus a weak warm bounce. Under-lighting is dramatic: keep any front fill far below the key [5][6][7].
- **Point and spot shadows.** Bias a perspective map in linear distance, or convert the bias per fragment. Hyperbolic Z makes a
  constant bias wrong at every distance [8]. PCSS sizes the penumbra as (d_receiver − d_blocker)·w_light / d_blocker [9].
- **Contact grounding.** Use screen-space shadows along each light's direction (Bend's Days Gone, Ghost of Tsushima) [10], GTAO for
  ambient [11], and capsule or character occlusion for soft grounding where maps fail [12][13].
- **Local fog scattering.** Froxel volumetric fog with clustered local lights (Wronski AC4, Frostbite, The Last of Us Part II) [14][15][16].
- **Bounce.** Use irradiance probes (DDGI) for dynamic bounce [17]. A cheap stand-in is a large, soft, unshadowed fill co-located with the
  practical, which is what film does with a bounce card.
- **Night tone.** Scotopic simulation means a blue shift plus desaturation and lower acuity. Apply it **only in dark regions**: a global shift
  gives a blue haze [18][19]. AgX rolls saturated fire toward white with less hue skew than ACES or Filmic [20].
- **Night water.** The glitter path widens with surface roughness and source height [21]. Point lights reflect as vertical streaks, and
  planar reflections or SSR with a fallback carry the lit world [22][23].

## 2. Ranked improvements

Cost: S is under a day, M is 1-3 days, L is a week or more. Impact is for the night camp.

### (A) Scene quick wins with existing features

| # | Change | Fixes | Exact parameters to try (night look) | Impact |
|---|---|---|---|---|
| A1 | AgX plus exposure for night | near-black wide shot, white craft glow | `post/tonemap/operator: 1` (AgX), `camera/exposure/compensation: +1.0` (try 0.7-1.5), then `post/grade/lift` slightly cool (≈ [0.004, 0.006, 0.012]), `post/grade/saturation` 0.9 | high |
| A2 | Raise moon fill for shadow detail, not black | silhouettes | `env/intensity` 0.012 → 0.03-0.05; `lights/moon-rim/intensity` 0.32 → 0.5 as a back or rim on the figures; keep `attractorWorld/sunIntensity` ≤ 0.3 | medium |
| A3 | Tame the craft spill | white spill behind the craft | `lights/ufo-glow/intensity` 90 → 15-25, `range` 18 → 8, `position` y just under the hull and not behind it; `ufo-rim-*` 30 → 8-12; colour toward [0.35, 0.75, 1.0] | high |
| A4 | Fake fire bounce (bounce card) | flat, unlit shrubs; no warm wrap on the ground and bank | add `fire-bounce`: point at the pit, y ≈ 0.3, `castsShadow false`, `range` 22-26, `intensity` 4-6, colour [1.0, 0.62, 0.38], `volumetric 0`, `flicker` 0.15 at `flickerRate` 1.5 | medium |
| A5 | Fire haze, as a local glow in the air | the fire does not "light the air" | `scene/volumeDensity` 1.25e-4 → 6e-4-1e-3 at night, `scene/volumeLocalLights` on, campfire `volumetric` 0.2 → 0.6; keep `scene/volumeAnisotropy` ≈ 0.3 | medium |
| A6 | Shorter moon shadow range at night | soft or blurry moon shadows | `scene/shadowRange` 1200 → 150-250 (cascade 0 drops from ≈ 48 m to ≈ 8 m, so texels are 3-6× finer) | medium |
| A7 | Night water (see §3) | milky, flat river | the A-row in §3 | high |
| A8 | *Optional* shadowed fire without engine work | F1 | 4 cascades + 4 spots fit in 8 views: replace the shadowed campfire point with 4 `spot`s at the pit (yaw 0/90/180/270, pitched −20°, `outerCone` 50°, `innerCone` 30°, range 14, intensity ≈ 30 each) plus an unshadowed low point (intensity 8) for the overlap. It still suffers F3 until that is fixed. | high but awkward |

Note on A2 and A6: once B3 lands, the moon's shadow bias also needs a night value.

### (B) Engine fixes and bugs

| # | Fix | Fixes | Have / misconfigured / lacks | Cost | Impact |
|---|---|---|---|---|---|
| B1 | **The shadow budget is fair and loud.** Skip lights with `intensity ≤ 0` or `shadowStrength ≤ 0` when allocating, which frees the day-off campfire and a night-off sun. `log::warn` when a point or spot does not fit (it is silent today). Optionally give cube faces their own smaller atlas (e.g. 6×1024² R32, ≈ 24 MB), so 4 cascades and one shadowed fire coexist. | F1 | has it, but the budget silently drops it: `shadow_renderer.cpp:186-276` | S (skip + warn), M (separate cube atlas) | **highest** |
| B2 | **Perspective-correct bias for spot and cube maps.** Either convert the world bias to NDC per fragment (`bias_ndc = worldBias·f·n / ((f−n)·z²)` at the receiver's light-space z), or write linear distance (`frag_depth = dist/far`) in the shadow pass and compare linearly. Take the texel size at the receiver's distance, not at the far plane. | F3, "shadows don't match casters" | has it, misconfigured: `shadow_math.cpp:418-419`, `shadows.wgsl:296-306` | S | high, once B1 lands |
| B3 | **Moon bias and contact flags are authorable per look.** Expose `attractorWorld/sunShadowBias` and `sunContactShadow` (and `sunCastsShadow`) as live look parameters. The night uses ≈ 0.15-0.3 m; the day keeps 3.2. Better: express the attractor sun's offset in cascade texels so it scales itself. | F2 (the blob), lost contact | has it, misconfigured: `composition.cpp:8112`, set once at build | S | high |
| B4 | **PCSS for local lights, and correct PCSS for cascades.** Run the blocker search on cube and spot views with a light-size parameter (`radius`: a fire is about 0.4 m), using linear depths. For ortho cascades, compute the penumbra in world units (Δdepth·tan(angular size)), not the normalised-depth ratio `(depth−blocker)/blocker` (`shadows.wgsl:251`, which is not a light-size model for an orthographic map). | soft blob vs hard contact; contact hardening at feet | partly has it: PCSS on cascades only | M | medium-high |
| B5 | **Contact shadows per light, safely.** The per-light `contactShadow` flag already exists (`lighting.wgsl:542`), but the scene switch is global (`scene_renderer.cpp:3737`). Let the scene switch default on and turn it off for the attractor sun (B3). The fire's 0.6·max(0.05·depth, 0.25) m march then grounds feet, rocks and the pit. | no contact or grounding | has it, misconfigured | S | high |
| B6 | **The AO radius and strength are authorable.** Add `environment.aoRadius` and `aoStrength` instead of 5% of the target distance. Then turn GTAO on at night with a radius of 0.5-1 m. Only useful with ambient > 0 (A2) or with B9. | grounding in fill light | has it, misconfigured: `scene_renderer.cpp:3757` | S | low-medium |
| B7 | **Water takes local lights.** Walk the froxel cluster in `fs_water` for points and spots: GGX specular with the water's roughness, plus the cine `streak` spread along the view, so the fire and craft paint vertical streaks; add a small diffuse "surface glow" term for the warm pool on the near water. Gate it on a `water.localLights` flag (ADR-1286 notes the 18% cost on another world). | F7 | lacks it: `water.wgsl:1045-1048` | M | **high for the river** |
| B8 | **Particles are in the water's reflection.** Either take the refraction copy after emissive and additive particles, with a second copy only when water is present, or splat additive particles into a half-res "emissive for reflection" target that the SSR adds. | F7 (the flames are missing from the river) | lacks it | M | high for the river |
| B9 | **Fur gets wrap and back-scatter.** Give the alpha-card fur a wrap-diffuse and back-lit transmission term (a Kajiya-Kay-style rim exists in `procedural.wgsl:700` for strands), so the fire rims the fur and the moon edges it. | the Sasquatch reads as a dark lump; the Gray's hard terminator | lacks it for meshes | M | medium |

### (C) New engine features worth an ADR

| # | Feature | Fixes | Cost | Impact |
|---|---|---|---|---|
| C1 | **Scotopic (Purkinje) shift in the tonemap.** Before the operator, compute a rod response V ≈ dot(rgb, [0.0, 0.42, 0.58]) (or Kirk and O'Brien's fit), and blend toward `V·nightTint` with weight `1 − smoothstep(L_lo, L_hi, luminance)`. This affects only dark regions [18][19]. Add a mild acuity loss (blend toward a bloom mip) in the same mask. Parameters: `post/night/{amount, tint, lo, hi}`. | "very CG" night; keeps the warm fire photopic and pushes shadows to a cool, desaturated blue | S-M | high |
| C2 | **Local irradiance probes or a bounce volume.** A small camp-local SH/L1 probe grid (e.g. 8×4×8 over 40 m), relit each frame from the shadowed local lights against the depth or SDF, or baked per look. Firelight bounces off the sandstone onto the undersides of the figures and the shrubs. A DDGI-lite on WebGPU is possible later [17]. | flat unlit shrubs; no warm wrap; dark undersides | L | high |
| C3 | **Emissive-to-indirect.** Register emissive meshes and particle systems (flames, the craft's ring) as cheap unshadowed area or sphere lights automatically, with an intensity from their emissive power. | every bright area has a source; the craft lights its pad | M | medium |
| C4 | **Shadowed local-light scattering.** Sample the campfire's cube (after B1) in the volume march, for that one light only. Figures then cut darker wedges into the haze. | the fire reads as being inside the air | M | medium |
| C5 | **Water glitter path.** Use a stretched-highlight model for directional lights: a separate `glintRoughness`, anisotropic along the view (Beckmann or GGX with αx ≠ αy), so the moon makes a path whose width follows the moon's elevation and the ripple [21]. Pair it with planar reflection for flat rivers (the canyon river is mostly flat) as the SSR fallback [22][23]. | the moon glint is a pinpoint, and reflections fail off-screen | M (glint), L (planar) | medium-high |

## 3. The river at night

**What the engine does** (`shaders/water.wgsl`, ADR-099/1286/1650/1868):
- It is its own pipeline, blended after opaques.
- Reflection = env cube (mid-mip by roughness) + the sky mirror (the frame's own sky, `skyMirror`) + an SSR march against the opaque copy
  (`ssr`, 160 steps, 480 m), optionally multi-ray and spread along the view (`streak`, `reflectionRays`).
- Schlick fresnel with an **authored f0**. The comment at `water.wgsl:947-952` says raising f0 is a deliberate lie, so that night water is not a black ribbon.
- Directional-only GGX glint, shadowed by the moon's CSM.
- Beer-Lambert alpha from the bed depth; optional transmission and absorption.
- Sparkle, glints, foam and streaks.
- AO darkens it under the banks; fog is applied on top; `atmosphereGroundAt·0.35` is added.

**Why it reads flat, milky or glowing:**
1. f0 0.35 with ×1.3 reflection gives 35-45% sky mirror at every angle. Night water should be near black head-on, with all its
   brightness in grazing reflection and in highlights.
2. With roughness 0.03 and ripple 0.12, the surface is glass. The env reflection is one smooth gradient, and the moon is a dot.
3. Nothing local lights or reflects in it: no fire streak (F7: no local specular, and the particles are absent from SSR).
4. The fog (fogColor at night [0.008, 0.01, 0.03]) plus `atmosphereGroundAt` add a uniform veil.

**(A) Water parameters to try at night** (`attractorWorld/water/*` if routed by a look; otherwise the block):

| Key | Now | Try | Why |
|---|---|---|---|
| `fresnel` | 0.35 | 0.04-0.08 | a dark body; reflections only at grazing angles |
| `reflection` | 1.3 | 1.0 | no over-bright sky |
| `roughness` | 0.03 | 0.07-0.1 | a moon glitter path, not a dot |
| `ripple` / `rippleScale` | 0.12 / 0.2 | 0.4-0.55 / 0.35 | facets for streaks and glints |
| `streak` | 0 | 0.6-1.0 | vertical light streaks (fire-lit bank, craft ring) |
| `reflectionRays` | 1 | 6-8 | a smooth streak without noise |
| `glints` | 0 | 6-12 | sparks in the colour of what the SSR found (fire-lit rock, craft cyan) |
| `sparkle` / `sparkleColor` | 0 | 0.2-0.4, [0.7, 0.8, 1.0] | moon sparkle on the chop (slow `rippleSpeed`; flicker-safe) |
| `specular` | 1.0 | 1.5-2.5 | brighten the moon path once the roughness is wider |
| `skyMirror` | 0.2 | 0.35 | the dark sky with stars, sharp |
| `shallowColor` / `deepColor` | dark blue | keep; deep ≈ [0.002, 0.003, 0.008] | dark water |
| `transmission` / `absorption` | 0 | 0.5 / [0.6, 0.35, 0.15] | warm-lit shallows near the camp show the bed |

**(B/C) Engine work for the water, ranked:** B7 local-light specular and streaks; B8 particles in the reflection; C5 glitter path
and planar fallback. Also make the `atmosphereGroundAt·0.35` add authorable, so it can be 0 at night.

## 4. Recommended top-5 plan

1. **B1 + B2 (S, together, one ADR):** the shadow budget skips zero-intensity lights and warns about every dropped light, and spot and cube bias
   is corrected for perspective. The campfire then actually casts, and its shadows sit on their casters at 4-10 m.
   Verify with `shadows.points == 1` at the camp, and a cube-face overlay.
2. **B3 + B5 (S):** the attractor sun's bias, contact and cast flags become look parameters. The night sets bias 0.2 and contact off for the moon, and turns contact
   shadows on for the fire. This removes the blob, if F2 is right; confirm by setting the bias to 0.2 and toggling the moon's cast flag first. It also grounds
   feet, the pit and rocks.
3. **A1-A6 (scene, the art agent):** AgX with +1 EV, a moon rim and fill for shadow detail, the craft spill tamed, a fire bounce light, a little
   haze, and a shorter moon shadow range.
4. **Night water A-row plus B7 (M):** the physical-ish f0, roughness, ripple, streak and glints now; local-light streaks on the water
   next, then B8 so the flames themselves reflect.
5. **C1 Purkinje shift (S-M, ADR):** a dark-region-only cool desaturation, so the moonlit areas read as night rather than dim day,
   while the fire stays warm.

Next tier: B4 local PCSS, B9 fur wrap and transmission, C2 camp probes.

## Sources
1. https://sony-cinematography.com/articles/night-lighting---part-1---tackling-large-night-exteriors/
2. https://nofilmschool.com/tips-shooting-night-exteriors
3. https://www.premiumbeat.com/blog/lighting-day-for-night-interiors
4. https://cinematography.net/edited-pages/Night_Exteriors.htm
5. https://cinematography.net/edited-pages/Creating_A_Camp_Fire_Effect.htm
6. https://broadcastbeat.com/news/lighting-a-scene-for-firelight
7. https://melies.co/cinematic-techniques/lighting/under-light
8. https://gamedev.net/forums/topic/711700-calculating-precise-depth-bias-for-shadow-maps/
9. https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
10. https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/
11. https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
12. https://docs.unrealengine.com/4.26/en-US/BuildingWorlds/LightingAndShadows/CapsuleShadows/Overview
13. https://history.siggraph.org/?p=71423 (Character Shading of Uncharted 4)
14. https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf
15. https://frostbite.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite
16. https://history.siggraph.org/learning/volumetric-effects-of-the-last-of-us-part-two-by-kovalovs
17. https://research.nvidia.com/publication/2019-05_dynamic-diffuse-global-illumination-ray-traced-irradiance-fields
18. https://www2.eecs.berkeley.edu/Pubs/TechRpts/2011/EECS-2011-91.pdf (Kirk, low-light tone mapping)
19. https://resources.mpi-inf.mpg.de/DarkNoise/article.pdf (Kellnhofer et al. 2015)
20. https://blenderartists.org/t/agx-changes-hue/1527123
21. https://www.astronomy.com/?p=41453 (the moon's glitter path)
22. https://dev.epicgames.com/documentation/unreal-engine/planar-reflections-in-unreal-engine?lang=en-US
23. https://www.advances.realtimerendering.com/s2017/PixelProjectedReflectionsAC_v_1.92_withNotes.pdf
24. https://history.siggraph.org/?p=76961 (Lighting Technology of The Last of Us Part II)
