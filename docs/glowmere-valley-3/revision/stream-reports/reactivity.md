# Reactivity stream (ADRs 924–927): the agent's final report

Reported on 2026-09-27. Branch `agent/reactivity`, final commit `4b8cf846` (16 commits on
`integrate/revision` `06be0d88`, with main `808f32e4` merged in). The coordinator merged it into
`integrate/revision` as `a6598157` (one index conflict). This is the agent's report, lightly trimmed.
Evidence is in `~/Desktop/av-gen-review/reactivity-adr924-927/`:
- the sheets and side-by-side videos for s16, s33 and s35;
- `gv3-proposal.json` and `gv3-tuned-proposal.json`;
- `install_reactivity.py`.

## What was built
- **ADR-924, `PlanRoute` and `PlanSource`:** a plan item carries a project route verbatim (`delayMs`, `depthSource` and the rest), plus its level, group, owner, layer and reason. It compiles to an ordinary `ModRoute` stamped with `planItem`, installs as one undo, revises in place, and keeps hand edits.
- **ADR-925, the reactive catalogue:** generated from the parameters, the composition and the effects, in 15 groups. Every candidate passes the liveness registry first, and heroes are found from the scene.
- **ADR-926, the validator:**
  - it refuses dead targets;
  - it flags ONE_SOURCE, ONE_PHASE, OVER_SATURATED and PHASE_RATE_TRAP;
  - its ROUTE_HAZARD flag covers `music.*` or `audio.rms` as a depth source, hue driven by audio, and a multiply that darkens between hits;
  - it adds two liveness rules, `layer-emits-nothing` and `no-field-named`.
- **ADR-927, the default proposer:**
  - musical layers are measured from the sections' audio profiles;
  - meso: heroes take the kick, clap, downbeat, breath, phrase, lead and bass in turn, with parts 35 ms apart, the practical light 50 ms later and spores 90 ms later;
  - micro: the smallest glowing layer echoes the kick, small layers flicker on the hats, and lamps flare on the claps;
  - macro: section energy drives the ecology light, fog, wind, particle density and water glow, and hue is keyed per section;
  - beat routes take their depth from `section.energy`, normalised so the quietest section answers at 35% and the loudest in full.
- **The headless dump,** `avgen --project <file> --propose-reactivity <out.json|-> [--fps n]`, plus the assistant tool `director.propose_reactivity`.

## Where each visible control lives
- **Modulation panel → Routes:** each planned route is a normal row marked `[plan: <key>]`, with its reason on hover.
- **Modulation panel → Sources:** `lfo.two-bar-breath` and `timeline.glowing-plants-hue-by-section`, whose parameters are under Parameters → sources.
- **Director panel → the plan view:** route and source rows marked ok, warning or blocked.
- **Director panel → Plans in this project → "Reactivity: N routes: micro a, meso b, macro c".**

## Suites
- **CPU, final code (`50eb345d`):** 3,653 tests, 3,652 passed, 1 failed, exit 8. The failure is a wall-clock ratio test under load averages of 30–66; it passed 3 of 3 alone, and in the previous full run.
- **CPU, previous commit:** 3,653 of 3,653 passed.
- **GPU, full, under the lock:** 520 cases, 519 passed, 1 skipped, exit 0.

## The GV3 proposal (a scratch copy with the meter pins)
**60 routes and 2 sources.** The audit finds all 60 live, 0 dead and 0 hazard. Validation gives 0 errors
and 8 OVER_SATURATED warnings, caused by GV3's own shared-material routes.
- **Micro (15):**
  - hero spores echo their hero 90 ms late;
  - river motes, flowers and shelf-fungi on the hats (+0.18, at 30 and 70 ms);
  - beacons on the claps (+0.30, 60 ms);
  - water sparkle on the hats (×1.3, 150 ms).
- **Meso (32):**
  - elder: kick, +0.60;
  - lantern: clap, +0.47;
  - spire: downbeat swell, +0.39;
  - bloom: two-bar breath, ×0.90–1.12;
  - veil: phrase, +0.50;
  - umbra: lead, ×0.95–1.14;
  - cairn: bass, ×0.95–1.12;
  - ridge, scree and ember: section change, +0.44, relighting 1.4–2.4 s outward;
  - the elder's practical light on the kick (×1.3, 50 ms);
  - the fungi on the kick (+0.30, 90 ms).
- **Macro (13):**
  - hue by section: +0.03 before the arrival; +0.06 in the pull-back, suspension, break and tail; 0 from the arrival; −0.08 in the drop;
  - particle density ×0.75–1.35;
  - aurora on the lead;
  - ecology light ×0.65–1.35;
  - fog ×0.85–1.20;
  - wind ×0.80–1.30, with gusts on the breath;
  - water glow ×0.85–1.20.
- **The tuned variant:** with the four shared-material routes removed, the routes stream's energy arc, and a bar-triggered wave, it is **63 routes, all live, 0 errors, 0 warnings**.
- **Renders:** s16, s33 and s35, with and without the proposal, change 8–28% of each frame's pixels.

## How GV3 should install and tune it
1. **Meter pins:** delete `control.phraseBars` and `control.sectionPhrases`. Set `"music/meter/bar1Beat": 0` and `"music/meter/phraseBars": 8`.
2. **Data changes:**
   - Remove `timeline.kick → material/glowmere2TissueWarm/emissionIntensity` and the three `lfo.breath → material/glowmere2{Cap,TissueCool,TissueWarm}/emissionIntensity` routes. The per-node lanes replace them.
   - Keep the gap dips and the crash, and set `kick`, `gap` and `crash` to `"mode": "event"`.
   - Set the 13 section energies to 0.25, 0.55, 0.35, 0.60, 0.75, 0.90, 0.65, 0.80, 0.40, 0.20, 0.60, 1.00, 0.30.
   - Add the field `bar-wave`: radial pulse at [-12, 0, 52], wavelength 40, speed 20, width 0, falloff smoothstep 180–300, trigger `{"source":"beat","everyN":4,"offset":0}`. Name it in `emissiveField` on fungi, shelf-fungi and beacons, with amounts 8, 5 and 3.5.
3. **Propose and install:**
   - Run the dump.
   - Append `install.routes` and `install.sources`, merge `install.parameters`, and add `install.directingPlan` to `directingPlans`. `install_reactivity.py` does this, with `--drop KEY`.
   - Confirm with `--audit-routes`.
4. **Tune per item:** amount, `delayMs`/`attackMs`/`decayMs`, `remapOut*` and `depthMin`/`depthMax`. Keep `hueOffset` within ±0.08. If the elder's 20 ms lead matters, point its kick routes at the event-mode `timeline.kick`. A route edited after installing is kept by later revisions.
5. **Previews against finals:** judge small-layer amounts and hue on 1080p or 4K stills. Attacks of 8 ms or less peak the same at 30 and 60 fps.

## Defects found, not fixed
- The Modulation panel's own route and source edits have no undo (`control_panel.cpp:1648`, `1821`, `1876`).
- **GV3's section energies (0.29–0.87) flatten the arc:** the cold open sits above the break, and the riser above the drop.
- **GV3's shared-material routes put all ten heroes in lockstep** on `lfo.breath`.
- Water tears are not on this branch; their catalogue family is tested only synthetically.
- GV3's 3 dead arcs and 3 pulse-between-frames hazards remain.

## Merge notes
Expect conflicts with `agent/setpieces`, all of them appends on both sides: `issue.hpp`/`.cpp`,
`plan.hpp`/`.cpp`, `scene_facts.hpp`, `edit_capture.*`, `edit_history.*` and `directing_context.hpp`.
Keep both sides, and update the table sizes and `static_assert`s: issue codes 27, content domains 12.
