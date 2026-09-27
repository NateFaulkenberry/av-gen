# ADR-927: The Director's default reactivity proposal, and the headless dump a generator installs

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-924 (plan routes), ADR-925 (the reactive catalogue), ADR-926 (the validator), ADR-896..899
(musical time, level-free features, band onsets, sections on the bus), ADR-900 (`delayMs`,
`depthSource`), ADR-902 (`--audit-routes`, whose shape the dump follows)
**Found by:** the owner's brief section 3 ("the world itself must respond to the music"), section 4
(the mushrooms: "exactly the sort of opportunity the Director should be finding automatically"),
section 5 (micro, meso, macro; "correlated with the music rather than mechanically synchronized"),
section 16, and the GV3 revision plan's reactivity map (`03-revision-plan.md` section 5)
**Implemented by:** `directing::proposeReactivity`, `musicalLayers`, `ReactivityProposal`,
`ReactivityOptions` (`src/directing/reactivity_proposer.*`); `app::proposeReactivityForProject`,
`runProposeReactivityCommand` (`src/app/reactivity_cli.*`) and `--propose-reactivity`
(`src/app/main.cpp`, `application.cpp`); the tool `director.propose_reactivity`
(`src/ai/director_tools.cpp`)
**Tests:** `tests/unit/test_reactivity_proposer.cpp` (`[adr927]`: the golden plan
`tests/data/directing/reactivity/glade-proposal.json`, determinism, each property one by one, no
claps / no audio / no sections, a wave on the transport clock); `tests/integration/test_reactivity_cli.cpp`
(the dump, its exit codes, its install by plain JSON edits); `tests/integration/test_reactivity_engine.cpp`
(the assistant's tool); `tests/rendering/test_reactivity_gpu.cpp` (`[gpu][adr927]`: a proposed route
reaches the pixels, for every kind of target it proposes)

## Context

With plan routes, a catalogue and a validator, a director can plan reactivity. None does it for them:
the LLM Director Agent is blind to the music's layers and to what in the scene can answer, and a person
starts from a blank route list. The brief asks for a deterministic default -- catalogue x musical layers
-> routes at three levels -- that is not "everything pulses to the beat".

## Decision

**The music's layers, measured** (`musicalLayers`, from the sections' level-free profiles, ADR-899): the
kick, clap and hats are present when their band's onset rate says so (0.5, 0.5 and 0.8 a second on
average, or twice that in some section); the bar, a two-bar breath and the phrase when there is a beat
grid; the lead and the bass (a band's steady level) when the audio is measured; the section when there
are two distinct section energies. Each carries its evidence ("kicks 2.1/s on average, 2.3/s at most").

**Rules** (every number a named choice in `reactivity_proposer.cpp`; the GV3 reports' measurements
where they exist):

- **meso -- one owner per layer, each hero its own.** Heroes that emit (ADR-925; never a character), most
  important first, take the kick (add 0.60, attack 5 ms, decay 300), the clap (0.50, 5/180), the bar's
  downbeat (0.45, 80/1200: a swell), the two-bar breath (x0.88-1.15), the phrase turn (0.70, 250/3000),
  the lead and the bass (x0.92-1.22 and x0.92-1.20 over the band's level window across the song's
  sections), in turn; amplitudes fall 7% a rank (never below 55%). Heroes beyond the layers relight on
  `section.change` (0.80, 40/1800), delayed by their distance from the first at 120 m/s -- the valley
  relighting outward, twelve times a film, not a strobe. A hero's program-lit parts answer 35 ms apart
  (its plain accents are left alone and named); the practical light beside it echoes 50 ms later (x1.3),
  its spores 90 ms later (x1.25). No two heroes share a source, a timing and an amplitude.
- **micro -- hats and claps on small, fast, local things.** The glowing scatter layers that read as
  lights (a quarter of the brightest layer's emission or more; a faint glow in vegetation pulsing reads
  as flicker and is left alone, named): the smallest echoes the kick 90 ms after the lead hero at +0.30
  (the emission stream's fungi, +0.25-0.35, attack 5, decay 320); the other small ones flicker on the
  hats, +0.18, 30 ms and then 40 ms apart; a layer 1 m or taller (lamps) flares on the claps, +0.30,
  60 ms. Free particle swarms glint on the hats (x1.25-1.31), staggered 0-120 ms. The water's sparkle
  catches the hats 150 ms late.
- **meso -- waves along the bar.** A glowing layer (or hero part) that names a triggered wave field
  (ADR-905/906) gets its light-wave depth from the section: x0.35 in the quietest, x1.15 in the loudest,
  gliding over 2-3 s. A field on the transport clock is named in the notes (its wave leaves once, at 0 s),
  and a scene with none is told how to add one.
- **macro -- the section on the world, never the key light.** `section.energy`, remapped over the song's
  own range and gliding over seconds: the ecology light x0.65-1.35, the fog's density x0.85-1.20, the
  wind's strength x0.80-1.30 (its gusts on the two-bar breath a beat after the heroes, x0.80-1.35), the
  free particles' density x0.75-1.35, the water's glow x0.85-1.20 (and, where the water stream registers
  them, its tears x0.70-1.30). The lead lifts a world effect (the aurora) x0.94-1.15, slowly. **The colour
  is keyed, never driven**: one timeline source, `glowing-plants-hue-by-section`, keyed at the section
  boundaries with a one-bar glide -- +0.06 of a turn (cooler) in the quiet sections, +0.03 before the
  song's arrival, 0 from it, -0.08 (warmer) in the drop -- routed to the salient layers' `hueOffset` at
  100%, 85%, 70%, 60%, 250 ms apart, smallest first.
- **Depth follows the section.** Every beat-driven route takes `depthSource` section.energy with
  `depthMin`/`depthMax` chosen so the song's quietest section answers at 35% and its loudest in full,
  whatever scale its energies were written on (never below 0 on 0..1). With no sections, the measured
  `audio.energy`; never `audio.rms`, never a `music.*` event.
- **Left alone, and said:** the key light and ambient (the style shift the owner ruled out, brief
  section 7), shared materials (every surface on one moves in lockstep; the per-node and per-layer lanes
  are the handles), a target a person already routes, a target another plan routes, anything a character
  or a scenario drives. A plan's own previous revision is not in its way.

Deterministic: the same catalogue and music give the same plan, byte for byte (tested).

**The headless dump.** `avgen --project <file> --propose-reactivity <out.json | -> [--fps n]`, no window
and no GPU: an offline engine loads the project, proposes, validates, compiles, and installs the plan in
that scratch engine -- never in the file -- then audits it exactly as `--audit-routes` does. Exit codes:
0 written, 2 no `--project`, 3 the project did not load, 4 the file could not be written. Format
`avgen-reactivity-proposal`, version 1:

    {
      "format", "version", "project", "frameRate", "durationSeconds", "hasAudio",
      "music":   {"tempoBpm", "sections": [{"type", "start", "end", "energy", "audio": {"energy",
                  "onsetRate", "kickRate", "snareRate", "hatRate"}}]},
      "summary": {"routes", "sources", "byLevel", "byGroup", "bySource", "byOwner",
                  "layers": [{"name", "signal", "present", "evidence"}], "depth": {"source", "min", "max"},
                  "notes": [...], "validation": {"errors", "warnings"}, "audit": {"live", "dead", "hazard"}},
      "items":   [{"key", "level", "group", "owner", "layer", "reason", "route": <a project route with
                  "planItem">, "compiled", "issues": [...], "verdict", "verdictReason", "findings": [...]}],
      "sources": [{"key", "reason", "source": {"kind", "name", "settings"}, "parameters": {<path>: <value>},
                  "compiled", "issues"}],
      "validation": [<issue>], "diff": [<line>], "catalog": <ADR-925's catalogue>,
      "overlaps": [{"source", "target", "owners", "note"}],   // a person's routes on what the plan animates
      "install": {"routes": [...], "sources": [...], "parameters": {...}, "directingPlan": <the plan as installed>}
    }

**Installing is copying**: append `install.routes` to the project's `routes` and `install.sources` to its
`sources`, merge `install.parameters` into `parameters`, and put `install.directingPlan` in
`directingPlans` (replacing one of the same id). Edit an item by editing its route before installing (a
route edited after installing is a hand edit a later revision will not overwrite); drop one by leaving
it out.

**The assistant reaches it**: `director.propose_reactivity` returns the plan, the layers, the summary and
the notes and changes nothing; the assistant edits it if the request asks for more and puts it through
`director.propose_plan`, so it arrives in the Director panel's plan view for Preview, Accept or Reject.

## Consequences

- **Pixels** (`test_reactivity_gpu.cpp`, the glade at 384x216, each route switched off as the control):
  the elder's gills on a detected kick (and nothing in the kickless break), the fungi's glow 90 ms after a
  kick, their hue in the drop (measured -0.13 of a turn on the changed 8-bit pixels for the -0.08 key),
  their light wave dimmer in the quiet break than without the section's depth, and a hero part's own light
  wave likewise, the elder's spores and its
  practical light on the kick, the ecology light, fog and wind in the drop, the water on the hats and in
  the drop, the aurora on the lead, and the lamps on a clap.
- **Glowmere Valley 3** (a scratch copy of `examples/world/glowmere-valley-3.json`, with the signals
  stream's meter pins): 60 routes and 2 sources, every route live under the audit, no error, no ONE_SOURCE
  or ONE_PHASE: micro 15, meso 32, macro 13; the kick 6 routes, the clap 5, the hats 4, the downbeat 4,
  the phrase 4, the breath 5, the lead 5, the bass 4, the section change 12, the section's energy 7, the hue
  4. Installed by plain JSON edits into a copy, `--audit-routes` finds all 60 live. Rendered at 960x540
  with and without it (s16, s33, s35; one second each at 30 fps), it changes 8-28% of each frame's pixels
  (side-by-side clips and sheets in `~/Desktop/av-gen-review/reactivity-adr924-927/`).
- The proposal is a starting position: GV3's generator installs, edits or drops items, and the evaluator
  judges the behavioural and meaningful tiers the validator cannot.
- `director.inspect_capabilities`' registry gains `reactive` (ADR-925); the Director tools are ten.
