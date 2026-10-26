# Cursed Slammers: the owner's POC 4 brief, alternatives to lenticular (verbatim, 2026-10-26)

The owner's covering note: "one more POC for cursed slammers"

---

# AV Gen POC: Explore Alternatives to Lenticular Holographic Engraving

## Objective

The current lenticular-printing POC has not convincingly reproduced the desired physical effect on a conventional screen. Do not immediately abandon the POC. Determine whether a different real-time material treatment can deliver a similarly distinctive holographic collectible aesthetic using the existing monkey-face asset.

The visual target is a premium vintage holographic trading-card or lenticular sticker: recognizable facial imagery, intricate engraved detail, metallic highlights, spectral color separation, and an impression of a physical printed surface.

## Constraints

- Reuse the current face asset and existing scene.
- Do not replace the asset, redesign the face, or build a new environment.
- Preserve the existing renderer architecture and rendering path.
- Target 1080p at 30fps.
- Avoid generic rainbow gradients, generic sci-fi hologram effects, excessive bloom, and smooth plastic-looking rainbow materials.
- Do not introduce unnecessary dependencies.
- Inspect existing material, shader, render-graph, and asset-loading implementations before deciding how to implement the variants.
- Do not assume the previous lenticular implementation is entirely useless. Identify any reusable components before modifying it.

## Research

Inspect these references:

1. Holofoil shader breakdown: https://www.cyanilux.com/tutorials/holofoil-shader-breakdown/
2. Holocloth: https://github.com/dmitrykurash/holocloth
3. Lenticular shader reference: https://natanetoon.com/en/params/effects/lenticular.html

Extract transferable techniques rather than copying another engine's architecture. Verify licenses before copying source code or assets.

## Implement four comparable variants

### A. Holofoil engraving

Combine:
- Fine engraved or contour-aligned surface detail.
- Dark grooves and high-contrast metallic highlights.
- View-dependent Fresnel response.
- Iridescent spectral coloration.
- Restrained, spatially coherent sparkle.
- Optional subtle animation of the spectral bands.

The engraving must follow the facial structure. Do not simply overlay uniform lines across the entire face.

### B. Intaglio / guilloché

Emphasize:
- Intricate curved linework that follows facial contours.
- Crosshatching in shadowed regions.
- Strong black/silver contrast.
- Selective specular highlights.
- Minimal rainbow coloration.

This variant tests whether detailed engraved linework is more important than the holographic component.

### C. Iridescent chrome

Emphasize:
- Dark reflective metal.
- Strong but controlled Fresnel highlights.
- View-dependent spectral color.
- Roughness variation and subtle surface irregularity.
- Clean silhouette and facial readability.

Do not substitute a simple animated HSV gradient for the material response.

### D. Hybrid security foil

Combine the strongest aspects of A and B:
- Engraved facial contours.
- Fine diffraction-like patterning.
- Metallic silver base.
- Localized spectral bands.
- Sparse micro-sparkle.
- Carefully controlled bloom.

Treat the foil pattern as a surface phenomenon, not a full-screen post-processing effect.

## Implementation requirements

1. Inspect the existing implementation and establish a baseline before changes.
2. Produce all four variants on the same mesh, with identical camera, lighting, exposure, and framing.
3. Expose sensible material parameters through the existing material or scene-control architecture.
4. Prefer procedural shader patterns and existing geometry attributes where practical.
5. Avoid expensive per-pixel ray marching, unnecessary render passes, and costly temporal effects.
6. If the mesh lacks useful UVs, normals, tangents, or sufficient detail, identify that limitation before adding complexity. Choose the simplest viable fallback.
7. Ensure color management, HDR values, and bloom do not wash out the engraved detail.
8. Keep the original POC recoverable and make each variant independently selectable.

## Evaluation

Evaluate every variant using:
- A stationary camera.
- A slow camera orbit.
- A close-up facial crop.
- The intended final scene framing.
- A representative live-render performance test.

Record the GPU time and note visual defects such as texture swimming, visible tiling, excessive sparkle, aliasing, washed-out features, and rainbow coloration that does not follow the face.

Do not claim a material is suitable for live use based on a single still image.

## Deliverables

- Four selectable material variants.
- Screenshots from the same camera and lighting setup.
- A short written comparison of face readability, engraved detail, holographic character, motion quality, and performance.
- A recommendation identifying the strongest candidate and its remaining weaknesses.
- A concise explanation of whether the existing asset has enough surface detail and suitable geometry to support the target effect.

Do not perform an unrelated art pass or expand the POC into a general-purpose material framework. The purpose is to make an evidence-based decision about the best visual direction before deciding whether to continue or retire the POC.
