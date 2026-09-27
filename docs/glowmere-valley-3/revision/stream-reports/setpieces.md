# Set pieces stream (ADRs 928–931): the agent's final report

Reported on 2026-09-27. Branch `agent/setpieces`, final commit `cf118345` (`integrate/revision`
`a6598157` merged in). The coordinator merged it into `integrate/revision` with no conflicts. This is
the agent's report, lightly trimmed. The validated GV3 plan is
[../audit/data/setpieces/gv3-ufo.plan.json](../audit/data/setpieces/gv3-ufo.plan.json); the beam
GPU frames are in `~/Desktop/av-gen-review/setpieces/`.

## What was built
- **ADR-928:** set-piece templates (abduction of 1–3 animals, survey, flyby), and a beat that begins on the clock.
  - Staging is seek-exact: a step ends when `elapsed + 1e-10 >= duration`, because a trace, a render and a seek compute time differently.
  - The abduction's default `targetClearance` is 6.5 m. GV3's meadows carry 3.2 m of canopy everywhere, so the old 3 m default refused every animal.
- **ADR-929:** a plan places set pieces, and the project keeps its staging.
- **ADR-930:** each moment is a world event and a bus event. The first frame after a seek carries the event, and a seek replays routes keyed on staging beats.
- **ADR-931:** the Director evaluates in scratch and proposes only the winner.
  - `director.evaluate` and `director.compare` render a scratch copy, trace it, run the Critic's adapter, and call `critic submit … --video-start a --wait --json --strict`.
  - They attribute findings to plan items, and keep evaluations per revision.
  - Configured with `avgen --critic PATH --critic-url URL`, and the variables `AVGEN_CRITIC`, `AVGEN_CRITIC_URL`, `AVGEN_RENDER_PREFIX` and `AVGEN_EVALUATE_SIZE`.

## Where the controls are in the UI
- **Director panel → "UFO set pieces":** one row per set piece, in viewer words, e.g. "abduction: lifts 2 animals, flown by saucer".
  - Edits: the event, the placed moment, "at (s)", "over x, z (m)", animals lifted, bearings, hover height, beam colour, and "meant to be seen from (m)".
  - Each edit is one undo. An edit that would block the set piece is refused with the reason.
- **Parameters panel → staging → `setpiece/<key>`:** labelled knobs, e.g. "hover height above the ground (m)", "beam colour: red/green/blue", on the Beginner and Intermediate layers.
- **Modulation panel → the route source picker:** `setpiece/<key>/<moment>`.

## Suites
- **CPU, final:** 3,739 tests, 3,720 passed, 19 skipped, 0 failed, exit 0.
- **GPU (`629ede31`):** 522 cases, 521 passed, 1 skipped, exit 0.
- **Live smoke test** against a private Critic: passed in 5.2 s. It found, and the agent fixed, the reader rejecting the Critic's bare `NaN`.

## How GV3 should use this
**Scene changes (`tools/gv3/cast.py`):**
1. **Add the second craft `scout`:**
   - the visitor model at 0.6 scale;
   - `scout-beam`, a copy of `visitor-beam` parented to it;
   - an entity for each;
   - the actor `{"name": "scout", "body": "scout", "parts": [{"name": "beam", "entity": "scout-beam"}]}`.
2. **Remove the hand-written `abduction` scenario** (`scenario()`). The validator refuses set pieces on a craft that an autostarting scenario drives.
3. **Remove `horse_light()`'s timeline keys** on `fx/horse-light/glow` and `rim`. The plan's cues replace them and follow E5's lift.

**The plan** (E1–E5; the file is linked above):
- E1, `survey`: scout, bar 13, at (−20, −190).
- E2, `flyby`: saucer, bar 15 + 0.3 s.
- E3, `abduction` of 1 animal: scout, bar 37, far up the valley.
- E4, `abduction` of 2 animals: scout, bar 57, by the river.
- E5, the centrepiece: saucer, the beam on bar 93, the horse.
- Two cues: the horse's glow and rim ride E5's lift.

**Measured on a scratch copy** (192 s traced at 60 fps):
- E1's beam at 22.63 s; E2 crossing at 26.65 s; E3 lifting `bull-10` at 66.97 s.
- **E4 left without beaming,** because only `cow-19` was within reach. Re-validate it after the characters stream's re-homing. If it still finds one animal, name two, or set `"animals": 1`.
- E5: beam at 170.35 s, lift at 172.78 s, the horse retired at 177.72 s (one frame after the drop). The craft held within 0.354 m.

**Commands:**
- Compile, install, save and trace, on the CPU:
  `build/release/tools/avgen_cast_trace --project P.json --plan ufo.plan.json --save-project P.ufo.json --seconds 226 --hz 20 --out cast-ufo.json`
  - Exit 0 means every item was built; exit 2 means some were blocked (see `plan.blocked`).
  - `setPieces[]` in the output gives each beat's measured time, the animals, and how still the craft held.
- **Song Mode peaks:** write `{"name": "setpiece/<id>/<beat>", "subject": <craft>, "seconds": t}` from `setPieces[].beats` into the song plan's `events`. `visitor` must be a starred hero.
- **The Director's evaluation loop:** `avgen --critic ~/Documents/GitHub/creative-critic/.venv/bin/critic --critic-url http://127.0.0.1:8765`, with `AVGEN_RENDER_PREFIX=<repo>/tools/gpu-lock.sh`.

**Previews against finals:** set pieces are simulation, identical at every resolution. Render and
trace at 60 fps. Check E1's thin, far beam at the delivery size; it may need `beamEmissive` of about 1.2.

## The Critic's adapter change (not yet made; the Critic repo was off-limits to the agent)
`creative-critic/adapters/avgen/avgen_adapter.py` hard-codes GV3's saucer beats. Replace them with
events read from the cast trace:
```python
events = []
for sp in cast_doc.get("setPieces", []):        # the cast trace JSON
    for beat, t in sp.get("beats", {}).items():
        if beat in ("rest", "transit", "hover"):
            continue
        events.append({"type": "ufo", "label": f"{sp['id']} {beat}", "t": t,
                       "position": sp.get("craftAt", {}).get(beat) or trace.at(sp["craft"], t),
                       "radius": 8.2, "entity": sp["craft"]})
    for a in sp.get("animals", []):
        if a.get("retired", -1) >= 0:
            events.append({"type": "ufo", "label": f"{sp['id']} {a['entity']} taken", "t": a["retired"],
                           "position": a.get("atRetire"), "radius": 8.2, "entity": a["entity"]})
```

## Defects found, not fixed
- Other code that compares absolute times without a tolerance could still land a frame apart.
- An abduction that finds fewer animals than asked leaves without beaming. This is by design, and the validator warns.
- The event-driven agent test's 180 s deadline fails under heavy load.
- The Critic cannot resolve a subject for a static rig's shot.
