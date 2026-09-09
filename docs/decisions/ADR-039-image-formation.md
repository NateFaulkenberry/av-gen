# ADR-039: Image formation, selective post and tone mapping

- Status: Accepted (2026-09-09)
- Research: `docs/research/cinematic-camera-and-color.md`, `docs/research/render-passes-and-post.md`

## Decision
- The image formation order is fixed and documented: scene shading in scene-linear HDR, volumetrics,
  transparency, **exposure**, temporal effects (motion blur), lens effects (distortion, chromatic
  aberration), bloom and halation, colour grade, tone map, output effects (vignette, grain,
  sharpen). Exposure moves before bloom so bloom thresholds are meaningful in exposed units.
- **Tone mapping default becomes AgX** for its hue retention on saturated emissives, with ACES,
  Reinhard, PBR neutral and clamp retained. Grading contrast compensates for AgX's flatter default.
- **Selective post** uses the emission and identifier targets from ADR-035: bloom weighted by
  emission rather than luminance alone, halation restricted to warm highlights above a threshold,
  and per-effect masks by object or material identifier.
- **Halation** is a wide, red-weighted bloom tier; **anamorphic** is a horizontally scaled bloom tier
  with optional ghosts. Both default to off, and both are parameters like everything else.
- **Auto-exposure** is part of this chain, metering the pre-exposure HDR target.

## Consequences
- Positive: the image stops being uniformly glowy; bright emissives read as bright rather than as
  white; the pipeline becomes explainable to an artist.
- Negative: reordering exposure before bloom changes existing scenes' look, so the showcase projects
  need their bloom thresholds re-tuned once (documented in the migration note).
