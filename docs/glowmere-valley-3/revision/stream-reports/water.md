# Water stream (ADRs 914–916): the agent's final report

Reported on 2026-09-27. Branch `agent/water`, final commit `a3f8e0d7`, with main merged twice. The
coordinator merged it into main as `3f720bfa`; the tree is identical to the one the stream's
suites ran on. This is the agent's report, lightly trimmed. Before/after stills, a one-second clip
and `index.txt` are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-tears/`.

## Suites
- **CPU** (`ctest -L unit -j 4`): 3,699 tests. 3,679 passed and 19 were skipped. The one failure, "event-driven: the assistant watches the film…", hit its 180 s wall-clock limit at 185.7 s under load; it passed alone in 112 s.
- **GPU,** the whole suite under the lock: 532 cases, 531 passed, 1 skipped, all 619,179 assertions passed, exit 0.

## What was built
- **The unregister fix:** removing or detaching a terrain drops all its water parameters.
- **ADR-914, bounded advection:** every travelling water field uses a two-phase crossfade. Late-film streaking went from ×3.8 to ×1.03.
- **ADR-915:** the water's fades count 1080-row reference pixels. Renders at or under 1080 internal rows are bit-identical to before.
- **ADR-916, deliberate water tears:** thin, stepped seams of ripples packed into stripes, as in the owner's reference.
  - They sit on a rigid lattice along the wind that drifts slowly, and gusts tighten them.
  - The water outside a seam is untouched.
  - At `tears` 0, the tear code is compiled out, so the surface is byte-identical to before.
- **Route audit:** a new rule, `tear-setting-unread`, and six phase-rate rows. Never route `scene/windDirection` while the tears follow the wind.
- **A merge fix for reactivity:** its catalogue and proposer looked for the tears under old draft names. They now point at the real paths (`b426422a`).

## Where the controls are in the UI
The controls are `nodes/<terrain>/water/tears/…`, on the Intermediate and Advanced layers.
- **Parameters panel:** nodes → `valley/water/tears`.
- **World panel:** the terrain → Inspector → water → `tears/…`.
- **Labels:** amount; shear (m); coverage; step size (m); spacing (m); stretch; follow the wind; direction; drift (m per second); wind (gusts tighten the seams).
- **Routing:** only amount, shear and coverage can be routed.

## Defects found, not fixed
- GV3's scene sets `"shoreFade": 1.6`, which nothing reads; `edgeFade` was probably meant.
- Still water drifts along its own wind direction (45°), about 10° off GV3's scene wind.
- The baked flow field is still discontinuous. ADR-914 bounds its effect on the water only.
- `avgen --ab` cannot start at a given second.

## How GV3 should use this
1. **The water base** (project parameters, which override the scene):
   - `nodes/valley/water/ripple` from 0.05 to **0.1**;
   - `rippleScale` from 5.2 to **2.6**;
   - the `audio.bass → nodes/valley/water/ripple` route from 0.015 to **0.03**.
   
   With these, previews and finals agree within 3–4%.
2. **Tears,** in the scene's valley water block:
   ```json
   "tears": 0.35, "tearShear": 6.4, "tearCoverage": 0.8, "tearCell": 3.2, "tearSpacing": 20.0,
   "tearStretch": 4.0, "tearDirection": "wind", "tearDrift": 0.15, "tearWind": 0.6
   ```
   Or as project parameters `nodes/valley/water/tears/{amount, shear, coverage, cell, spacing, stretch, followWind, drift, wind}`.
   - **The step is 3.2 m, not the reference's 1.2 m,** because GV3's cameras are tens of metres from the water; at 1.2 m the stripes fade to slick lines.
3. **What each shot shows:**
   - s08 reads best: a seam crossing the moon's glint by the boat, changing 3.98% of the frame;
   - s12: a combed band and a slick line;
   - s14 and s39: faint slick lines;
   - s26: almost nothing at 171 s;
   - no added shimmer anywhere (s26: 0.0018 a frame with and without).
4. **Options:**
   - a bass accent: `audio.bass → nodes/valley/water/tears/amount`, add 0.15, attack 120 ms, decay 900 ms;
   - with the reactivity arc installed (×0.70–1.30), use amount 0.36;
   - to move the seams with the music, route `scene/windSpeed` or `scene/wind/gustAmount`.
5. **Previews and finals use the same settings.** s08 changes 3.98% at 960×540 and 3.93% at 1920×1080.
