# Request to the AV Gen owner: follow-ups after glTF water materials (ADR-2020)

Date: 2026-11-11. From: av-agent (M8 follow-up). AV Gen commit `1f5ad901` (pinned clone `artifacts/m8-avgen/av-gen`,
read only). *verified* = seen in a render or its passes. *assumption* = an inference we have not checked in source.

ADR-2020 works on the alpine scene. The `Lake` material is drawn as cinematic water with no warnings, and the ground,
hero, 28 mm and elevated views now mirror the massif where the Blender reference does. Run:
`artifacts/m8-e2e/data/projects/alpine/avgen/avgrun_000mwcgs91qa5908d`. Review sheets:
`artifacts/m8-review/lake-water.png` and `water-views-{a,b}.png`. What is still different, most important first.

## 1. Water pixels have no object ID in the `id` AOV (blocks our evaluation)

- **Verified.** In `render/ground/frame_000000.id.exr`, the lake region (rows 600-700, cols 300-900):
  - before (best run, Lake as a PBR mesh): one ID, the lake's (25035132), on all 60,000 pixels;
  - with `waterMaterials`: two other IDs (22806876 on 55,461 pixels, 23396708 on 4,539), which we take to be the
    terrain tiles under the lake.
- **Effect.** Our evaluator compares per-object silhouettes from this pass. Every view with lake now reports the
  water's silhouette at IoU 0.000, and the terrain tiles under it regress too. These are measurement artefacts, and
  they hide real problems on those pixels.
- **Assumption.** The water pipeline draws after the pass that writes the IDs, or does not write the ID target.
- **Ask.** Have water entities write their object ID (and material ID) to the `id` AOV, as an opaque entity does.

## 2. Reflections are grey where the reference is blue (the largest visible difference)

- **Verified (ground view, mean RGB, 0-255).**

  | Region | Reference | AV Gen |
  |---|---|---|
  | Reflected cliff | 114, 147, 166 | 143, 157, 168 |
  | Reflected sky near the bottom of the frame | 159, 182, 197 | 141, 160, 174 |

  The reference's reflection is darker and bluer: about 0.64, 0.79, 0.88 of the wall it reflects.
- **Tried, with no effect on the reflected cliff:**
  - `reflectionTint` [0.80, 0.93, 0.99];
  - `ripple` 0.
- **Tried, with some effect:** `fresnel` 0.15 and 0.30 brighten only the reflected sky.
- **Assumption.** `reflectionTint` tints only the environment-cube sample, not the march or the sky mirror.
- **Ask.** A way to tint (or absorb) the screen-space and sky-mirror reflection, or say which key does it.

## 3. The far shore's trees reflect as a soft grey band

- **Verified.** In the reference, the treeline at the far shore (1-2 km) mirrors as crisp dark trees. Ours is a light,
  smeared band just under the shoreline (ground rows 405-430: reference 93, 115, 126; ours 153, 164, 173).
- **Assumption.** At 128 steps over 2000 m, a step is about 15 m, coarser than the trees. The mirror also does not
  trust geometry nearer than about 0.35-0.5 x `ssrDistance`.

## 4. The waterfall is not reflected

- **Verified.** The reference shows the white fall mirrored in the lake. Ours shows none.
- **Assumption.** The fall (and spray) are alpha-blended, so they are not in the opaque frame copy the water reads.

## 5. Light bands at the left and right frame edges

- **Verified.** Hero and blockout_28mm show a pale vertical band about 20 px wide at the left edge, and the ground view
  at both edges.
- **Context.** `docs/water.md` says off-screen sources fall back to the environment cube. The cube here is a bright
  sky, so the fallback reads as a stripe.
- **Ask.** Fade toward the march's last valid colour, or a darker cube sample, near the screen edge.

## Cost (alpine hero, 1920x1080, M2 Max)
See `docs/architecture/engine.md` ("Lake as water"). Measured with `--live-profile` and `--ab nowaterssr`.
