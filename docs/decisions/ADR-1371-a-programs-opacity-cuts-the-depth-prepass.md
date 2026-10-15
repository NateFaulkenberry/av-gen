# ADR-1371: A program's opacity cuts the depth prepass

- Status: Accepted (2026-10-15), art/digital-ocean-2 (Digital Ocean's yacht wake)
- Code: `fs_proc_depth` (`shaders/procedural.wgsl`).
- Tests: `tests/rendering/test_procedural_gpu.cpp` ("A program-cut procedural does not occlude the geometry behind it").

## Context

A procedural material with `alphaMode: "mask"` is cut where its opacity is under the cutoff. The lit pass
(`pbr_shade.wgsl`) takes that opacity from the material program when one writes it. The depth entry used by the
prepass and the shadow passes, `fs_proc_depth`, tested only the material's own alpha and its base-colour texture.

So a program-made cutout kept its full depth in the prepass. Where the lit pass discarded a fragment, anything behind
it failed the depth test, and the cleared colour showed through.

Digital Ocean's yacht wake is a masked strip whose program cuts world-anchored foam out of it. It drew as solid strips
of the sky's horizon colour wherever the foam was cut away.

## Decision

On a masked draw (a uniform per-draw flag), when the material has a program the fragment's instance admits,
`fs_proc_depth` evaluates the program with the same per-fragment context the lit pass builds:

- position, local position, normal, uv, instance lanes, time, audio, view direction, distance, and material emission;
- the screen-space inputs left at zero: curvature, cavity, occlusion and footprint.

It multiplies the program's opacity by the texture's alpha in the order the lit pass uses, and discards under the
cutoff. Draws that are not masked, or have no program, run exactly the code they ran before.

## Consequences

- Program cutouts are correct in depth and in shadows: the shadow passes use the same entry, so a cut casts no
  shadow, as the lit pass shows.
- Cost: the program runs a second time, in the prepass, only for masked draws with a program. The wake is a few
  thousand pixels.
- A cutout program that reads curvature, cavity, occlusion or footprint sees zeros in depth, so its depth cut can
  differ from its colour cut. Those inputs need derivatives or the AO texture, which the depth entry does not have.
  Cutout programs should key their opacity off position, uv or noise.
