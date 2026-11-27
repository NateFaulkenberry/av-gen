# ADR-2214: a light's volumetric shadow shares its surface shadow views; there are no volumetric-only shadow maps

- Status: Accepted (2026-11-26), eng/115-volumetrics (programme 115, Agent 2, B1)
- Code: `PunctualLight::volumetricShadow` (scene key `volumetricShadow`, default true); `SceneRenderer` maps each frame
  light to its packed index; `volumetric_light.wgsl` `vlShadow`
- Tests: `test_volumetric_light.cpp` (round trip), `test_volumetric_light_gpu.cpp` (lab case 101: the air is shadowed)

## Decision

1. A light shadows the air through the views it already renders for the surfaces: its cascades (picked by the
   froxel's view depth and faded out at the end of the shadowed range exactly as `shadowFactor` fades), its spot map,
   or its cube face. No view is rendered for the air alone, so the 8-view atlas budget (ADR-1906) is unchanged.
2. `volumetricShadow` (per light, default true) opts a light's air out of its shadow. It means something only with
   `castsShadow`. It is structural, not a parameter: whether a light occludes the air is a property of the rig.
3. The tap is a plain comparison at the froxel's centre with the view's depth bias and no normal offset (there is no
   surface), one tap at Realtime and a 2x2 box at High and Offline (ADR-2213). `shadowStrength` applies as on surfaces.

## Consequences

- A shaft can be no longer than the shadowed range (ADR-358, about 77 m by default): past the last cascade the air
  is unshadowed. Scenes that want long shafts set `environment.shadowRange` (lab e01 sets 120 m).
- SDF casters, which shadow surfaces through a screen-space composite, are not in the air's shadow.
- Clustered local lights are unshadowed in the air in B1; B2 gives a local light with a view its shadow.
