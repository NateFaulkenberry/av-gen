# Request to the AV Gen owner: follow-ups after glTF water materials (ADR-2020)

Date: 2026-11-11. From: av-agent (M8 follow-up). Asked at AV Gen `1f5ad901`; answered by AV Gen ADR-2021 at `7f81a5ed`
(pinned in its own folder, `artifacts/m8-avgen/av-gen-7f81a5ed`). *verified* = seen in a render or its passes.

## Status after 7f81a5ed (checked on alpine runs `avgrun_000mwcudjabb8cffa` and `avgrun_000mwcudpn9c37080`)

| # | What we asked | Status | Evidence |
|---|---|---|---|
| 1 | Water pixels carry their object ID in the `id` AOV | **Done** (verified) | Ground lake region: `water` on 60,000 of 60,000 px (was terrain tiles). Verify: 12 silhouette artefacts fixed, 0 regressed. Real test `test_the_lake_pixels_carry_the_lake_id_in_the_id_aov`. |
| 2 | A way to tint the screen-space and sky-mirror reflection | **Done** (verified): `ssrTint` | `[0.64, 0.79, 0.88]` brings the ground lake from 150, 161, 168 to 132, 153, 164 (reference 141, 167, 181). Mean ΔE on views with lake falls (ground 3.93 → 3.17). |
| 3 | Far-shore trees reflect as a soft grey band | **Done** (verified): `ssrBlur` | At 0.05 the treeline's column std is 17.1 (reference 17.8, before 9.3). The band is still lighter than the reference's: 123, 140, 148 against 90, 112, 122. |
| 4 | The waterfall is not reflected | **Not built.** Option A tried on our side: a trade-off, see below | AV Gen confirmed the cause: Fall and Spray are BLEND and drawn after the water's frame copy. |
| 5 | Light bands at the frame edges | **Done** (verified): `ssrEdgeFade` | At 0.004, the left edge reads 122, 146, 161 against 122, 146, 162 just inside it (before: 148, 169, 189 against 140, 156, 169). |

AV Gen measured the three new keys as free: +0.08 ms on our ground view, inside the noise. We did not re-measure,
because the GPU was shared at the time.

## Open: the waterfall's reflection (item 4)

**Option A (our side) was tried** on run `avgrun_000mwcw01pq8be2b4`, derived from `avgrun_000mwcudpn9c37080`.
- **What was changed:** `Fall` was set to MASK at cutoff 0.3 in a copy of the GLB (`override_material` with
  `alpha_mode`). The texture's alpha spans 0-0.65; a cutoff of 0.3 keeps 86% of the covered texels, and 0.4 keeps only
  22%. `Spray` stays BLEND: its alpha never exceeds 0.18, so a cut-out would delete it or make it a solid sheet.
- **Looked at** (`artifacts/m8-review/fall-mask-{a,b}.png`, `fall-mask-close.png`):
  - In the lake views (ground, hero, elevated), the fall is now reflected, as in the reference.
  - In the close views (closeup, footing), the fall becomes a flat, opaque, hard-edged slab. Its mottled see-through
    texture is lost, because that texture lives in the alpha. The closeup also shows a dark sliver at the fall's left
    edge.
- **Measured:** verify against the run before it: 4 fixed (the fall's own silhouette, since cut-out pixels now write
  IDs), 31 unchanged, 0 regressed. ΔE moves by at most 0.05 on any view (closeup 1.47 → 1.52).

**So A is not good enough wherever the camera is close to the fall.** If the owner agrees, this section becomes the
request for **option C**: draw blended meshes a second time into the water's frame copy (ADR-2021). That reflects the
soft fall and keeps its look. The cost AV Gen states is one more draw of every blended mesh plus scratch targets of
about 48 MB at 1080p. On alpine the blended meshes are only Fall and Spray.

## Not raised (small, may not be worth an engine change)

- The reflected far treeline is still lighter than the reference's (see item 3). It may be the reference's own haze
  and lighting difference, not the reflection.
