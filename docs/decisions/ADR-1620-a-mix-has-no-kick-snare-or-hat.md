# ADR-1620: A mix has no kick, snare or hat; the classes come only from a drum source

- Status: Proposed (eng/mix-hit-phaseout, The Living Prism P1-8, option A; owner review of the migrated scenes before
  merge)
- Supersedes:
  - the mix half of ADR-1060 (the causal kick, snare and hat as `response.*` and as live `audio.onsetLow/Mid/High`);
  - the mix half of ADR-1062 (`response.kick/snare/hat` on the mix) and its `response.hatRate`;
  - ADR-1068's MIDI demotion (`midiDrumness`; the timbre gates in the detector stay);
  - ADR-898's band onsets on the bus (`audio.onsetLow/Mid/High`; the frames keep them, below);
  - ADR-1580's `hits` default ("audio" no longer gives a drum class).
- Assessment: `docs/prototypes/living-prism/02-audio-modulation-audit.md` §15; probe B4
  (`docs/prototypes/living-prism/probes/B4-hit-phaseout/`).
- Code:
  - `src/analysis/response_feed.{hpp,cpp}`: `ResponseFeed::fromMix`, `isDrumClass`; the feed loses `drumness`;
  - `src/sonic/response.{hpp,cpp}`: the mix step reads `fromMix`; ADR-1068's demotion is gone; `response.onsetRate`
    (was `hatRate`, same id);
  - `src/audio/feeds.{hpp,cpp}`: `Feeds::drumClassFed`; a drum class naming `"audio"` is a load warning;
  - `src/app/engine.cpp`, `src/analysis/channel_analysis.cpp`: the offline and live mix feeds are `fromMix`;
  - `src/signals/audio_signals.{hpp,cpp}`: `audio.onsetLow/Mid/High` off the bus;
  - `src/scene/route_liveness.{hpp,cpp}`: an unfed drum class is a `silent-source` (routes) and an
    `effect-never-fires` (signal triggers);
  - `src/sonic/signal_events.{hpp,cpp}`: no band-onset derivation; the derived Sonic events step the project's feeds;
  - `src/directing/reactivity_proposer.cpp`: the Director's kick layer is `beat.pulse`, the clap and hats `audio.onset`;
  - `tools/drum_signals.py`: the live scenes' drum vocabulary (below), and every generator that used a mix class.
- Tests: `[adr1620]` in `tests/unit/test_sonic_response.cpp`, `test_drum_recall.cpp`, `test_feeds.cpp`,
  `tests/integration/test_stem_feeds.cpp`; and the rewritten cases in `test_signal_bus.cpp`, `test_signal_trigger.cpp`,
  `test_audio_features_bus.cpp`, `test_live_panel_no_hit_meters.cpp`, `test_route_fallback.cpp`,
  `test_conditioned_signal.cpp`, the Director's tests and their golden plan.

## Problem

The owner: "could we phase out the kick / snare / hh detectors on a mix? since they misfire often they aren't very
useful." Option A ("THIS"): remove all three on a mix. Measured (§15.1, B4): on Feline Footwear against its drum stem's
own classifier, the mix kick was 0.54 precise (its other half bass notes and hat clicks); on the synthetic kit 0.57.
The mix snare and hat agreed better (0.93, 0.85) but the snare decides 41-67 ms late and the hat fires on kick clicks
even on a separated stem. The class names are trustworthy only on a separated drum source (kick 1.00/1.00 there).

## Decision

**One rule: a class name comes from a drum source; a mix gives onsets, their register and the beat.**

- `response.kick`, `snare`, `hat` and their `*Env` fire only from a source a project's `feeds` name for them (`hits`
  or the class's own key: a drum stem offline, a drum input live). The mix's feed (`ResponseFeed::fromMix`) carries
  `low`, `onset` and the levels and never a drum class, offline and live. The detector itself is unchanged and still
  runs on every stream: it is the per-source classifier (`stem.<key>.kick/snare/hat`, fed classes).
- Unfed, the three are silent, and the route audit says so: a route on one is dead (`silent-source`, naming this ADR
  and the replacements), and an effect's signal trigger on one never fires (`effect-never-fires`).
- `feeds.hits` still defaults to the mix, which now means `low` and `onset` only. `"kick"`, `"snare"` or `"hat"`
  naming `"audio"` loads with a warning and feeds nothing.
- `audio.onsetLow/Mid/High` leave the bus in both modes.
- ADR-1068's MIDI demotion (`pendingDrumness_`, `midiDrumness`) goes: it only ever corrected the mix. The detector's
  own timbre gates (hat tilt, snare noise share) stay; on the melodic material they hold the detector's false drums at
  0 kicks, 0 snares and 7 hats for 88 arp notes, 0/4/0 for 30 chord stabs.
- `response.hatRate` counted the hat class; it is now `response.onsetRate`, the onset feed's onsets a second (the same
  id, the same 1.5 s window and 12/s full scale; settings `onsetRateSeconds`, `onsetRateFull`).

**Kept declared, not undeclared.** §15.2 proposed not declaring the three on a plain mix. They stay declared, with
their ids, because the bus cannot undeclare, the feeds change at run time (a drum input appears, a stem is added), and
a seek's replay rebuilds exactly the frame signals' prefix of the bus (a name declared later is not replayed). The
audit gives what not declaring would have: a route on an unfed drum class is reported, with the fix.

**The drum vocabulary for scenes (`tools/drum_signals.py`).** Every migrated scene reads one conditioned signal
(ADR-1602) per idea, the drum source's class first and a stand-in on a mix:

| signal | drum source (`stem.drums.*`, active) | on a mix |
|---|---|---|
| `signal.kick` (event) | `stem.drums.kick` | `beat.pulse` x 0.7 while `signal.busy` |
| `signal.snare` (event) | `stem.drums.snare` | `beat.pulse` x 0.9 on beats 2 and 4 (`signal.backbeat`) |
| `signal.hat` (event) | `stem.drums.hat` | `response.onset` |
| `signal.kickEnv`, `snareEnv`, `hatEnv` | the same through 160, 120 and 60 ms decays (the response model's releases) | |
| `signal.busy` | | 1 while `response.onsetRate` >= 0.05 (0.6 onsets a second) |
| `signal.backbeat` | | 1 on beats 2 and 4 (`beat.bar` windows) while busy |

The scales (0.7, 0.9) are the mix classes' median strengths on Feline (kick 0.69, snare 0.90), so a route tuned on the
old hit lands at the size it was tuned at. The busy gate is there because the beat clock runs on through a drumless
passage: on Feline's outro (182-210 s) 14% of the kick stand-in's and 16% of the snare stand-in's pulses landed on no
drum at all; gated, 3% and 4%, at the same recall.

**By class, not by the target's use (a departure from §15.3's table).** §15.3 mapped flashes, bursts and glitches to
`response.onset` sized by the register (ADR-1604), and only motion to the beat. Measured on Feline, neither half holds
on a real mix:
- the register at a kick's onset reads a median low share of 0.00, on the mix and on the drum stem alike (ADR-1604's
  0.81-0.85 is the synthetic kit), so a flash sized by it would vanish;
- `response.onset` is 0.09 precise against the stem's snares at every strength threshold, so a snare effect on it would
  fire on every hat (951 fires against 185).

The beat clock is the honest kick and snare on a mix (it is what a mix's kick and backbeat land on, and it never fires
off the grid); the onset is the honest hat.

**Second phase (unchanged here).** `AnalysisRunner` still writes the mix's causal kick/snare/hat into the live frames'
`lowOnset/midOnset/highOnset`, and a file's frames keep ADR-898's band onsets. Nothing on the bus reads them any more;
their remaining readers are internal and need P1-4 (an `AudioHistory` fed from a source) for a replacement: the GPU
`AudioHistory` onset rows (ADR-1116), the Effect Library's band-onset triggers through `TriggerClock`, the Director's
span profile (layer presence) and Astral Forge's `SongAnalysis` (`kickT/snareT/hatT`, a file's band onsets). Stopping
the runner's writes now would blank the live onset rows with no replacement.

## Evidence

**B4 re-run (Feline Footwear and its drum stem, 60 fps; reference: the stem's classifier).**

| signal | before | after |
|---|---|---|
| `response.kick` / `snare` / `hat`, no feeds | 467 / 185 / 606 fires; P 0.54 / 0.93 / 0.85 | **0 / 0 / 0** |
| the same, `"feeds": {"hits": "stem.drums"}` | | 406 / 235 / 545: **event for event** the stem's (P = R = 1.00, 0 ms) |
| `signal.kick` stand-in (mix) against the stem's kicks | (the mix kick: P 0.54, R 0.62, +17 ms) | **P 0.67, R 0.62, +17 ms** |
| `signal.snare` stand-in against the stem's snares | (the mix snare: P 0.93, R 0.73) | P 0.70, R 0.56, 0 ms |
| `signal.hat` stand-in against the stem's hats | (the mix hat: P 0.85, R 0.94) | P 0.45, R 0.79 (0.98 against any drum hit) |
| fires off any drum hit (`stem.drums.onset`, -30..+120 ms): kick / snare / hat | 8 of 467 / 0 of 185 / 14 of 606 | 11 of 374 / 8 of 187 / 11 of 951 |

The kick is more precise and its errors are the grid's; the snare and hat trade class precision (the owner's option A
accepts this, §15.2) for one rule and no decision latency. Against any drum hit the mix classes were already on a drum
98-100% of the time -- their misfires were the wrong drum, not no drum -- so "off any drum" does not fall: it stays at
1-4%.

**Per scene family, before and after (`--sonic-trace --trace-finals`, Feline, 30 fps; every target a migrated route
moves; a fire is a rise of the target's final, off-drum against the stem's onsets):**

| project | migrated routes | fires before (off-drum) | fires after (off-drum) |
|---|---|---|---|
| Sonic Abstract: Sacred Geometry Flight | 14 | 1,842 (3%) | 2,146 (5%) |
| Sonic Abstract: Neon Vector World | 23 | 8,853 (3%) | 9,498 (5%) |
| Sonic Abstract: Glitch Signal | 9 | 1,728 (4%) | 2,652 (7%) |
| Sonic VFX: Event Horizon | 13 | 4,406 (2%) | 4,720 (6%) |
| Sonic VFX: Storm Cell | 8 | 2,035 (2%) | 2,662 (5%) |
| Sonic VFX: Feedback Mirror | 11 | 4,573 (2%) | 4,727 (4%) |
| Digital Mosh Live | 10 | 930 (0%) | 854 (11%) |
| THE RIFT LIVE | 1 | 179 (0%) | 187 (10%) |
| RD Pins | 3 | 646 (1%) | 813 (2%) |
| Resonator: Cymatic World | 6 | 1,200 (1%) | 1,487 (1%) |
| Experiments: Flow Storm | 3 | 927 (1%) | 901 (5%) |
| Resonator: Waveform Dimension | 2 | 440 (0%) | 484 (4%) |

The off-drum fires that rose are the beat clock's: in THE RIFT's case every one lies in 145-186 s, where the drums play
(7.2 hits a second) but skip beats the grid keeps. On Trench (sparse, percussive) the fire counts move +8-32%.

**Route audit, all 44 live projects (`--audit-routes`, before on main, after on this branch):** 3,213 routes; dead 228,
hazard 527, live 2,458 in both; every project's summary identical; no non-live item names a removed signal.

## Migration

No shim (ADR-441): every use was rewritten in its generator and the project regenerated; each regenerated file differs
from main only by the renames, the added drum sources and (offline) the amounts named below.
- **Sonic VFX (16) and Sonic Abstract (10):** `tools/sonic_vfx/signals.py` maps the classes `kick`, `snare`, `hat`
  (and `*Env`) to the drum signals and `hatRate` to `onsetRate`; `kit.Scene` installs the drum sources a scene reads;
  the vocabulary text names them.
- **Generative and Resonator** (flow x3, oscillators x2, reaction-diffusion x3, Phase Wall, RD Pins, Cymatic World,
  Waveform Dimension): their routes read `signal.*` and `response.onsetRate`; they write through `kit.Scene`.
- **Digital Mosh** (live and both offline projects) and **THE RIFT** (live and offline): the band onsets and the live
  remap to `response.*` become the drum signals in every project, at the old live amounts (the onset's x 0.77, 0.78,
  0.91), so offline and live now read the same signals.
- **Chorus Field** (p5, p8, offline): the kick and snare drive the drum signals; the two projects gain a `sonic` block
  (the stand-ins read `response.*`).
- **Rainbow World:** only its mix fallbacks change: the hat glints and the flower shimmer fall back to
  `response.onsetEnv`, the clap and splash to `signal.snareEnv`. With its drum stem (the owner's Feline project) nothing
  changes; every other part of both projects is identical.
- **4 effect triggers** (Salt Flat Mirage, Storm Cell, Ferrofluid Crown, Tesla Choir) and the interpret input (Glitch
  Signal's, on the kick envelope) follow the class mapping through `signals.S`.
- **Not migrated: Glowmere Valley 3** (`examples/world/glowmere-valley-3.json`, an offline film). Its four micro routes
  on `audio.onsetHigh` (river motes, spores, tree fireflies, water sparkle) are now dead. They come from the Director's
  plan, whose hats template now proposes `audio.onset`, but the film is assembled by several generator steps and
  `tools/make_glowmere_valley_3.py` alone does not reproduce the committed project (it drops the set-piece cues).
  The owner decides: leave them, re-run the film's whole pipeline, or point the four (and their plan copies) at
  `audio.onset`.
- **Tools:** `tools/sonic_vfx/drum_recall.py` (it measured the mix classes) is removed; `sound_to_light.py`,
  `sonic_vfx_critic.py`, `check_response_range.py`, `sonic_vfx/matrix.py`, `abstract.py` and `live.py` read the new
  names.

## Consequences

- A live scene on a mix pulses its kick and snare on the beat clock, so the tracker's octave and phase errors (§2.1)
  now show in every scene; a MIDI clock is the mitigation for shows. A drum input keyed `drums` with the hits analysis
  on gives every scene its true classes with no edit.
- The Live panel's **Transients** no longer sizes a scene's kick and snare on a mix: they are the beat clock's pulse.
  It still curves every onset (the hats, `response.onset`), and at Transients 0 nothing is busy, so the kick and
  snare stand aside too. (Digital Mosh Live's test now checks the hats' glints; the Director's GPU proof finds its
  hero glow on a beat of the grid and at rest just before the next.)
- A drum input keyed `drums` WITHOUT the hits analysis is active but never fires a class: the drum signals pick it and
  stay silent. Turn its hits on, or name it in `feeds`.
- The derived event list of a Sonic signal trigger (`SignalEventDeriver`) now steps the project's feeds as the engine
  does; before, a fed project's derived `response.*` triggers were the mix's.
- The route audit does not look inside a signal's inputs (ADR-1602): a signal whose drum input never resolves falls to
  its stand-in, which is the intent.
