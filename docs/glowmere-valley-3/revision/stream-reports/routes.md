# Routes stream (ADRs 900–902): the agent's final report

The finishing agent reported this on 2026-09-26. Branch `agent/routes`, final commit `dd401647`
(the code fix is `52f89e8e`). This is the agent's report, lightly trimmed.

## The two GPU failures: cause and fix
- **The cause:** the seek replay (ADR-901) remembered where the previous seek landed. A second seek to the same instant was told it already stood there, skipped the replay, and left the smoothed routes reset. `RenderJob::start` seeks twice to its first frame, which gave 20 of 20 hashes differing and 1,740 EXR mismatches.
- **The fix:** `ReplaySignals::begin` forgets the last landing.
- **The regression case** "A second seek to the instant the last one landed on lands where the first did" fails before the fix (8 of 82 assertions) and passes after.
- **The cost:** repeating a seek to the same instant costs 4.8 ms instead of nothing.

## Suites
| Run | Result | Exit |
|---|---|---|
| CPU, full | 3,575 cases: 3,555 passed, 19 skipped, 1 failed as expected (the shouldfail) | 0 |
| GPU, full, under the lock | 509 cases: 508 passed, 1 skipped (no NDI runtime) | 0 |
| Route tests `[adr900]`, `[adr901]`, `[adr902]` without benches | 57 cases, 2,518 assertions | 0 |

**Caveat:** other agents' CPU suites ran during the GPU run, so its bit-identity passes are
unproven (docs/testing.md entry 29). A clean re-take script waits until no CPU suite runs.

"Glowmere Valley 2 from several viewpoints" passed in this full run, but failed on main in the
coordinator's run and in an earlier subset, so it looks intermittent or order-dependent.

## What was built
- **ADR-900, the route chain:**
  - a `delayMs` first stage (4000 ms ceiling);
  - a one-frame event reaches its full amount through attack and decay;
  - `depthSource`, `depthMin` and `depthMax`;
  - `"mode": "event"` for timeline sources.
- **ADR-901:** a seek replays the project's routes, bit-exact at 60 fps.
- **ADR-902:** one liveness registry with 23 rules, asked at bind, at load and by `--audit-routes`.

## Where an artist finds the controls
- **Modulation panel → Routes tab → each route's row:**
  - a `delay ms` slider;
  - a `depth` combo (`(none)` plus every signal);
  - `depth min` and `depth max` once a depth source is chosen;
  - a red `[dead: <rule>]` or amber `[hazard: <rule>]` badge, with every finding on hover.
- **Modulation → Sources tab → a timeline source's row:** a `keys are` combo, either values (a curve) or events.
- **Transport panel:** "(N warning(s))" beside the project name.

## The audit
- **Command:** `avgen --project <project.json> --audit-routes <out.json|-> [--fps <n>] [--log <level>]`.
- **Exit codes:** 0 written, 2 no project, 3 load failed, 4 could not write. It needs no window or GPU.
- **Schema:** format `"avgen-route-audit"`, version 1, `tier: "configured"`. Top-level `routes[]`, `tracks[]`, `effectDefaultRoutes[]`, `effects[]` and `summary`. Each item carries a `verdict` (live/dead/hazard), a `reason` and `findings[{rule, verdict, reason}]`.
- **GV3:**
  - routes: 33, of which 30 live and 3 hazard. The hazards are pulse routes whose 25 ms pulses are missed at 30 fps: `timeline.kick` and two `timeline.gap` routes;
  - tracks: 71, of which 68 live and 3 dead: the fireflies' two `emissionIntensity` arcs, and `paintedGround2/emissionIntensity`;
  - effects: all live.
- **GV2 multicam:** everything live.

## Changes to existing scenes
- **Event routes with an attack now arrive in full:** 551 routes in 47 files; 190 had reached less than half. GV2's hero rings are 23% brighter at their peak. GV3 is unchanged.
- **Renders that start mid-film** open with their routes where a render from zero has them.
- **Loads** log dead and hazardous items once. GV3 loads with 3 warnings, its dead arcs.

## Defects found, not fixed
- `masterGain` and `spatialGain` multiply a route's output, so below 1 they pull a Multiply route's target toward 0.
- The first played frame after a load reads `sources/*` values from before the load.
- The audit cannot see effects that a project's `effects` list replaces, terrain settings overridden by a program, or content-dependent phase rates.
- **GV3:**
  - the 3 dead arcs;
  - terrain `groundGlow` is inert under `paintedGround2`;
  - `lodCount` is an unknown setting;
  - 49% of the navigation grid is unreachable.

## How GV3 should use the chain
Checked on a scratch copy of GV3: 35 routes, all live, 0 hazards.
1. **Event mode for the scored pulses:** add `"mode": "event"` to the `kick`, `gap` and `crash` timeline sources.
2. **Section energy as a depth source:** a value-mode timeline `energy` keyed on the section markers. The starting values, stepped at each section start:

   | Segment | Energy |
   |---|---|
   | cold open (0) | 0.25 |
   | riff groove (7.86) | 0.55 |
   | pull-back (26.33) | 0.35 |
   | groove 2 (30.02) | 0.60 |
   | lift (59.56) | 0.75 |
   | arrival (74.33) | 0.90 |
   | plateau (89.10) | 0.65 |
   | lead forward (133.40) | 0.80 |
   | suspension (148.17) | 0.40 |
   | break (162.94) | 0.20 |
   | riser (170.33) | 0.60, rising linearly to the drop |
   | drop (177.71) | 1.00 |
   | tail (222.02) | 0.30 |
3. **A staggered kick cascade** with section-aware depth, each with `depthSource: "timeline.energy"`, `depthMin` 0.35 and `depthMax` 1.0:
   - TissueWarm at 0 ms (peak ×1.6);
   - Cap at 90 ms (×1.4);
   - TissueCool at 180 ms (×1.3, decay 140 ms).
4. **The breath as a travelling wave:** the existing `lfo.breath` routes get `delayMs` 0, 460 and 920 on Cap, TissueCool and TissueWarm.
5. **`audio.treble` → spores' emissive** gets the depth fields, with `depthMin` 0.5.

**How depth works:** `depth = min + (max − min) × energy`. A ×1.6 kick peak becomes ×1.29 in the
break and stays ×1.6 in the drop.

**Recommended values:**
- 30–250 ms delays for a ripple on each hit (one beat is 461.5 ms);
- whole beats for LFO phase offsets;
- `depthMin` 0.3–0.5.

**What to avoid:**
- `audio.rms` as a depth source: it is read unsmoothed and flickers.
- Any `music.*` signal as a depth source: they are one-frame events.
- Envelope sources: a seek does not replay them.

**Previews against finals:** resolution has no effect. At 30 fps, value-mode pulses miss hits and
event mode catches them all.
