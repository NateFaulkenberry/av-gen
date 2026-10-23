# ADR-1620: A mix has no kick; the kick comes only from a drum source, and scenes stand the beat in for it

- Status: Accepted (eng/mix-hit-phaseout, The Living Prism P1-8, **option B**: the owner's decision of 2026-10-23,
  `docs/prototypes/living-prism/16-hit-phaseout-option-b-brief.md`; merge after the owner's approval)
- Supersedes in part:
  - ADR-1060: the mix's causal kick as `response.kick` and as live `audio.onsetLow`;
  - ADR-1062: `response.kick` on the mix;
  - ADR-898: `audio.onsetLow` on the bus (the frames keep the band onset, below);
  - ADR-1580: the `hits` default no longer gives a kick.
- **Unchanged:** the mix's snare and hat. That covers the detector, its thresholds, ADR-1068's MIDI demotion,
  `response.snare/hat(Env)`, `response.hatRate` and `audio.onsetMid/High`. The stem and input classes are also
  unchanged.
- Assessment: `docs/prototypes/living-prism/02-audio-modulation-audit.md` §15; probe B4.
- Code:
  - `src/analysis/response_feed.{hpp,cpp}`: `ResponseFeed::fromMix`, which is the mix's onsets with no kick;
  - `src/sonic/response.cpp`: the mix step reads `fromMix`;
  - `src/app/engine.cpp`, `src/analysis/channel_analysis.cpp`: the offline and live mix feeds use `fromMix`;
  - `src/audio/feeds.{hpp,cpp}`: `Feeds::kickFed`; `"kick": "audio"` is a load warning;
  - `src/signals/audio_signals.{hpp,cpp}`: `audio.onsetLow` is removed;
  - `src/scene/route_liveness.{hpp,cpp}`: an unfed kick is a `silent-source` for routes and `effect-never-fires` for
    signal triggers;
  - `src/sonic/signal_events.{hpp,cpp}`: no `audio.onsetLow` derivation, and derived Sonic events step the project's
    feeds;
  - `src/directing/reactivity_proposer.cpp`: the Director's kick layer is `beat.pulse`;
  - `tools/drum_signals.py`: `signal.kick`, `signal.kickEnv` and `signal.drumming`.
- Tests:
  - `[adr1620]` in `tests/unit/test_sonic_response.cpp`, `test_feeds.cpp` and `tests/integration/test_stem_feeds.cpp`;
  - the kick parts of `test_signal_bus.cpp`, `test_signal_trigger.cpp`, `test_audio_features_bus.cpp`,
    `test_live_response.cpp`, the Director's tests, their golden plan, and the Director's GPU proof.

## Decision

**The mix has no kick.**
- `response.kick` and `kickEnv` fire only from a source a project's `feeds` name: `hits`, or `kick` itself. That
  source is a drum stem offline, or a drum input live.
- The mix's feed (`fromMix`) carries everything the detector decides except the kick. The detector itself is
  unchanged and runs on every stream.
- Unfed, a route on `response.kick` is reported dead by the route audit, which names this ADR and the replacement. An
  effect signal trigger on it is reported as never firing.
- `audio.onsetLow` leaves the bus. It is the mix's kick: the bus labelled it "kick (low-band onset)", and live it was
  the causal kick.
- Everything else about the mix's hits stays exactly as it was.

The three names stay declared, with their ids. The bus cannot undeclare, the feeds change at run time, and a seek's
replay rebuilds only the frame signals' prefix of the bus. The audit gives what not declaring would have given.

**Scenes stand the beat in for the kick (`tools/drum_signals.py`).**

| signal | with a drum source (`stem.drums.*` active) | on a mix |
|---|---|---|
| `signal.kick` (event) | `stem.drums.kick` | `beat.pulse` x 0.7, only while `signal.drumming` |
| `signal.kickEnv` | the same through a 160 ms decay (the response model's kick release) | |
| `signal.drumming` | | 1 while the mix's own hat or snare detector has fired in the last 750 ms |

- **Why 0.7:** it is the mix kick's median strength on Feline (0.69).
- **Why the drumming gate:** the beat clock runs on through breakdowns and drumless passages. The gate reads the
  retained hat and snare detectors, so a kick needs audible drums, not just a beat.
- **Why not `response.onset`:** the owner's option B keeps the onset free for what it was used for. Also, the onset
  register reads a median low share of 0.00 at Feline's kicks, on the mix and on the drum stem alike, so it cannot
  tell a kick.

## Evidence (Feline Footwear and its drum stem, 60 fps; the reference is the stem's own classifier)

| signal | precision / recall vs the stem's kicks | fires off any drum hit |
|---|---|---|
| the mix's kick detector (main) | 0.54 / 0.62 | 8 of 467 |
| `response.kick`, no feeds | **0 fires** | |
| `response.kick`, `"hits": "stem.drums"` | 1.00 / 1.00: event for event the stem's | |
| `beat.pulse` ungated | 0.59 / 0.62 | 61 of 424, mostly in the drumless outro |
| **`signal.kick`'s mix stand-in** | **0.68 / 0.60**, +17 ms | 6 of 358; none after 186 s |
| the mix's snare / hat (unchanged) | 0.93 / 0.73 and 0.85 / 0.94 | |

**Drumless material** (Feline's stems played as if each were the mix, 210 s):

| material | the mix's kick (main) | the stand-in (option B) |
|---|---|---|
| Bass stem alone | 379 kicks (bass notes) | 29 |
| Synth stem alone | 7 | 40: the retained hat detector fires 42 times on the synth's plucks, and the gate trusts it |

A stricter gate trades recall for those 40. Two hat or snare hits within 0.75 s gives 24 on the synth, 8 on the bass
and recall 0.52 on Feline. Three within 1 s gives 19, 3 and recall 0.51.

**Per scene** (`--sonic-trace --trace-finals`, Feline, 30 fps). These count the fires of every target a migrated
kick route moves. Other routes move the same targets, so the off-drum share is approximate.

| project | kick routes | fires before (off-drum) | fires after (off-drum) |
|---|---|---|---|
| Sacred Geometry Flight | 3 | 453 (2%) | 359 (5%) |
| Neon Vector World | 11 | 3,888 (2%) | 3,231 (5%) |
| Glitch Signal | 3 | 866 (8%) | 775 (11%) |
| Event Horizon | 8 | 3,059 (2%) | 2,892 (5%) |
| Storm Cell | 4 | 873 (2%) | 1,078 (5%) |
| Feedback Mirror | 8 | 3,176 (2%) | 2,513 (5%) |
| Digital Mosh Live | 4 | 358 (1%) | 388 (13%) |
| Cymatic World | 2 | 178 (1%) | 332 (0%) |
| Flow Storm | 1 | 455 (2%) | 359 (5%) |

**Route audit:** all 44 live projects were audited with `--audit-routes` on main and on this branch. Every project's
summary is identical: 3,213 routes, 228 dead, 527 hazard, 2,458 live. No non-live item is new.

## Migration (no shim, ADR-441; every project regenerated by its generator)

**Kick-only migration.**
- **Projects:** 31 live projects, plus Digital Mosh offline x2 and Chorus Field p5 and p8 (offline).
- **What changes in each:**
  - the kick routes read `signal.kick` or `signal.kickEnv`;
  - the drum sources are added first;
  - Digital Mosh's offline kick routes take the old live amounts (x 0.77);
  - Chorus Field gains a `sonic` block, because the gate reads `response.*`.
- **Checked:** each file differs from main only by these changes, verified by a semantic diff.
- **Effect triggers:** the 3 kick triggers (Salt Flat Mirage, Ferrofluid Crown, Tesla Choir) and Glitch Signal's
  interpret input follow the kick.
- **Snare and hat:** every snare and hat binding is the original one.

**Identical to main again.** These used only the snare or hat, or only `hatRate`: THE RIFT (live and offline), Rainbow
World (both projects), RD Pins, Phase Wall, the oscillator and reaction-diffusion experiments, and Waveform Dimension.

**Glowmere Valley 3 is hand-edited, not regenerated.**
- **What changed:** the 4 micro routes (river motes, spores, tree fireflies, water sparkle) and their 4 copies in
  `directingPlans` read `audio.onset` instead of `audio.onsetHigh`. That is 8 lines; nothing else in the file changed.
- **What is not touched:** the generator, the cues and the set pieces.
- **`audio.onsetHigh` stays on the bus**, so those routes were not dead; the rebind is the owner's instruction.
- **Measured on Rebuild (226 s):** `audio.onset` fires 5.3 a second against `audio.onsetHigh`'s 3.7 (6.2 against 4.6
  in the busiest 10 s), and 96% of the old high-band hits coincide with an onset. The four effects respond more often,
  but not excessively.
- **If it reads as too busy:** raise each route's `chain.threshold`/`thresholdLevel` so that weak onsets do not lift
  them. Nothing else would need to change.

**Tools.** The measurement tools read `signal.kickEnv` where they read the mix kick. `drum_recall.py` notes that its
kick column now reads 0 on a mix.

## Second phase (unchanged here)

The runner still writes the mix's causal kick into the live frames' `lowOnset`, and a file's frames keep ADR-898's
band onsets. Nothing on the bus reads them. Their readers are internal and wait for P1-4:
- the GPU `AudioHistory` onset rows;
- the Effect Library's band-onset triggers;
- the Director's span profile;
- Astral Forge's `SongAnalysis`, which reads a file's band onsets.

## Consequences

- On a mix, the Live panel's Transients no longer sizes the kick, which is now the beat clock's pulse. It still
  drives the snare and hat, and through them the drumming gate: at Transients 0 the kick stands aside too.
- A mix-only show's kick follows the beat tracker's phase (§2.1). A MIDI clock is the mitigation.
- A drum input keyed `drums` gives every scene the true kick, but only with its hits analysis on. Without it, the
  input is active, `signal.kick` picks it, and it never fires.
- A fed project's derived `response.*` signal triggers now step its feeds. Before, they were the mix's.
