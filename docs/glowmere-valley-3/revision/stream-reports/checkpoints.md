# Checkpoint reports at the usage-limit pause (2026-09-26, about 10:30)

These are the paused streams' own reports, kept for their GV3 guidance. The remaining work
for each is in PROGRESS.md.

## Camera (ADRs 911–913): branch `agent/camera`, checkpoint `3d9795f2`
- **Committed:**
  - the `avgen_cast_trace --camera` pose track, with `tools/camera_stability.py`;
  - ADR-912: a cut drops motion, AO and temporal history;
  - ADR-911: the filtered follow reference, with the knobs `followSmoothSeconds`, `followVerticalSmoothSeconds`, `followLead`, `followGround` and `followHeadingSmoothSeconds`. `followLagSeconds` reads HIST, FollowTrail is deleted, the clearance floor is a softplus, and `aimFollowSmoothingMs` is deleted;
  - ADR-913 and its docs.
- **GV3 before:** 10 of the 11 follow shots failed the bar.
- **GV3 after:** all 11 pass.
  - pitch high-frequency at most 0.040° (it was up to 1.22°);
  - eye vertical high-frequency 0.6–1.99 cm (it was up to 11.2);
  - subject off-centre at most 13.6%;
  - travel 1.00–1.08.
- **Seek:** s19's eye was 0.92 m off play after a seek; it is now 10 µm.
- **Cuts:** each new shot's first frame has 99.7–103% of the sharpness of the frames after it. The delivered final had 6%.
- **Recommended GV3 rig values** (ADR-913; also [../audit/data/camera-gv3-recommended-rig-changes.json](../audit/data/camera-gv3-recommended-rig-changes.json)):

  | Shots | Smoothing (s) / height smoothing (s) / lead | Other |
  |---|---|---|
  | Walkers: s03, s09, s13, s18, s19, s20, s28, s36, s38 | 0.3 / 1.0 / 1 | `followGround` on; delete `followLagSeconds` (s13, s18, s19, s38) |
  | s25 | 0.3 / 1.2 / 1 | `followOffset.y` 3.6 → 4.8 |
  | s24, s27 (the saucer) | 0.6 / 1.0 / 0 | never `followGround` |
  | s05, s10, s17, s29 | no change | on s29 the horse's rise is the shot |

  The knobs are in seconds and metres, so previews and finals take the same values.
- **Found, not fixed:** cow-3 is 9 mm off play 3 s after a seek, on both engines. ADR-158's aim-follow still carries the stride bob into Auto-director shots.

## Water (ADRs 914–916): branch `agent/water`, checkpoint `ddf44a85`
- **`6948f72d`:** water parameters are unregistered with their terrain node.
- **ADR-914, bounded advection:** a two-phase crossfade.
  - On the QA river, fine structure from 10 s to 200 s changes ×1.03; the control changes ×3.8.
  - GV3 s26's accidental seams are gone, at no measurable cost.
- **ADR-915, the water's fades count 1080-row reference pixels:** renders at or under 1080 rows are bit-identical. At 2160 rows, the far water reads ×1.08, against ×1.44 for the control.
  - **For GV3:** go back to ripple 0.1 at rippleScale 2.6, with the bass route at 0.03. The final-only 0.05 at 5.2 is now wrong.
- **Tears (WIP):**
  - all nine settings, with `tears`, `tearShear` and `tearCoverage` routable;
  - at tears 0 the tear code is compiled out, in a second pipeline, for byte-identity;
  - the last change, a band that folds out and back inside its width, is not re-tested.
- **Notes for GV3's tear tuning:** tune on stills of s08, s12, s14, s26 and s39. Tears read only where the reflection has contrast (the moon's glint, lit sky); far bands become smooth slick lines.
- **Evidence:** stills and scripts are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-work/`.

## Set pieces (ADRs 928–931): branch `agent/setpieces`, head `905c91ac`
- **`2ea9c7b5`, built and tested (23 cases):**
  - templates in `src/stage/setpiece.*`: abduction with 1–3 animals lifted in parallel (still-craft gate kept), survey, flyby;
  - beats tied to timeline seconds;
  - colour-channel `set` steps;
  - scenarios starting on another scenario's beat;
  - a bus-id bug fixed;
  - `PlanSetPiece`, which round-trips and is validated (unknown template or craft, no clear air, time outside the song, one craft in two places, impossible travel, and a `REPETITION` warning);
  - compiled to `setpiece/<key>` scenarios plus a marker per moment.
- **`905c91ac`, never compiled:** `avgen --plan P.json [--plan-report R.json]`, and `avgen_cast_trace --plan/--save-project` with a `setPieces` section.
- **The fixture scene** `tests/data/setpieces/setpiece-lab.*` (no licensed assets) is written, for the three-abduction proof.
- **For the Critic:** its adapter hard-codes GV3's saucer beats. It should read the cast trace's `setPieces` section instead.
- **Detail:** `docs/development/setpieces-design-notes.md` in the worktree.

## Render (ADRs 917–919): branch `agent/render`, WIP `7f36b8e3`
- **ADR-917:** `post/referenceHeight` (default 720). Every pixel-sized post value scales by chain height over reference height: bloom and halation pyramids, anamorphic reach, motion-blur tiles, the look stage's low-pass.
- **ADR-918:** `scene/fogSky`, a 128×32 map of the sky's radiance (the background, aurora and comets) that the fog fades toward. The Environment panel row is "Fog takes the sky's colour".
- **ADR-919:** offline floors: volume steps 32, anisotropy 16×, procedural sky cube 1024.
- **State:** it builds and the CPU tests pass. Nothing has run on the GPU.
- **World edge:** report only; a backdrop ring would take 1–2 days.
- **Detail:** `docs/development/render-design-notes.md` in the worktree.
