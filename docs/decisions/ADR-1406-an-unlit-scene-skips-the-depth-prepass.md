# ADR-1406: A scene decides its depth prepass; silent resonance rows are skipped

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World's shape-language overhaul, Stage 7)
- Code:
  - `scene::Environment::{ambientOcclusion, contactShadows}`, parsed, carried into the frame's environment and written back only when false (`src/scene/composition.cpp`).
  - `SceneRenderer`: AO, the contact-shadow steps and `needsDepthPrepass` honour them, and the shadow mask counts directional lights only when one casts.
  - `kResonanceSilent` / `RES_SILENT` (`src/spatial/resonance.*`, `shaders/fields.wgsl`).
- Tests:
  - `tests/unit/test_cosmic_key.cpp` ("A scene may turn off ambient occlusion and contact shadows ...").
  - The resonance CPU and GPU suites, which are unchanged by the skip (parity holds).

## Context

Cymatic World's ground is an unlit, painted surface: its light comes from a material program. Its deformed ground still paid for a **depth prepass** (the whole opaque geometry drawn a second time, every deformer re-run per vertex) because of three things:
- screen-space AO, on by quality tier;
- contact shadows, on by quality tier;
- the half-resolution shadow mask, encoded whenever any directional light exists. A scene with no authored light is given a default key light, which casts.

On the motion overhaul's denser mesh the prepass cost 8-34 ms for nothing.

The resonance samplers also evaluated every mode row, including modes that a focus window (ADR-1405) had silenced and rings that had faded.

## Decision

1. A scene's `environment` can say `"ambientOcclusion": false` and `"contactShadows": false`. The quality tier still decides when they are on otherwise. Both default on, and a scene that never sets them saves byte-identically.
2. The shadow mask shades directional shadows. With no enabled directional light casting a shadow it is given no lights, so it neither encodes nor asks for the prepass. A requested shadow AOV export (ADR-255) keeps its lights: it is asked for on its own account, and the GPU suite's AOV test holds it. An unlit scene authors one dark, non-casting directional light in place of the default casting key.
3. **The reverse case.** `"depthPrepass": true` asks for the prepass whatever else wants it. It is for a deep, self-occluding surface with a costly material, where early depth rejection (hidden fragments never shaded) saves more than the extra geometry pass costs. Cymatic World uses it after its correction round: the deep cells hide one another from the pulled-back camera, and with the deformation baked (ADR-1407) the prepass costs 6 ms and saves 11 (35.3 → 29.8 ms at High).
4. A resonance row whose every drive is below 1e-5 is skipped by every sampler, CPU and GPU alike. A focus window's weight is exactly 0 beyond three widths (a step of e⁻⁹), so silenced modes cost nothing.

## Consequences

- Cymatic World: the prepass is gone (−8 ms on the old mesh, −34 ms on a 548k-vertex one). AO and the mask are 0.
- Scenes that light anything are unchanged: their lights cast, and they never set the flags.
- A scene that turns AO off and then lights a surface loses AO on it. That is its declared choice.
