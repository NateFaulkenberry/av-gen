# ADR-1284: The grazing emissive rim follows the emissive texture

- Status: Accepted (2026-10-08), proto/bio-valley (the VFX art pass; found by the hero asset artist)
- Code: `shaders/pbr_shade.wgsl` (`emissiveBase` now carries the emissive texel).
- Tests: `tests/rendering/test_ecosystem_gpu.cpp` (`[adr1284]`).

## Context

The lit path adds a Fresnel rim tinted with the emissive colour, so glowing objects read as luminous at grazing
angles:

    rim = pow(1 - N.V, 3) * emissiveBase * 0.3 * intensity

`emissiveBase` was the material's emissive factor, never multiplied by the emissive texture. That is harmless for an
untextured emissive material, whose factor is its emission everywhere.

A glTF-correct asset is different. It puts the colour and placement of its emission in the texture, with factor
(1, 1, 1) and an emissive strength. The asset artist's veilcap is such an asset: factor 1, strength 3-4. It got a
neutral white rim of up to 0.3 x 4 = 1.2 over its whole surface at grazing angles, including where the texture
emits nothing. Its dark tissue read as pale grey plaster.

The artist's measurement on the moss apron (the ground beside it measures about 5):
- 92 as exported;
- 1 with the emissive texture and factor removed;
- 18 with the factor at 0.05;
- no change from removing the base colour, roughness or normal maps.

## Decision

The emissive texel multiplies `emissiveBase`, which the rim and the emission share. Where the texture emits, the
rim is that emission's colour; where it is black, there is no rim. Without an emissive texture the texel is
(1, 1, 1), so the result is unchanged bit for bit.

## Consequences

- Only materials with an emissive texture change. The GPU test pins the result:
  - an emissive-textured sphere whose texture is black matches the same sphere emitting nothing;
  - a white texture matches no texture;
  - the untextured rim is still there.
- `avgen_render_tests` with the fix: exit 0, 649 passed, 1 skipped, 0 failed. No existing golden or render test moved.
- The texture is now sampled once, before the rim, instead of after the factor; the same uniform guard applies.
