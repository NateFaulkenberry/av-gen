# Aurora Borealis pass 3: the implementation plan (coordinator, 2026-10-24)

Synthesises 21-research-pass3-audio.md (A) and 22-research-pass3-scene.md (B) against 20-brief-sabertooth.md and
20b-brief-river-light.md. The reports hold the measurements; this file holds the decisions.

## Audio
- The owner re-bounced every stem at 83 BPM with 3% Varispeed (all ten files 70.18 s, 2026-10-24 19:25-19:31); no
  relabel. Copies: `assets/audio/stems/sabertooth/` (gitignored via `.git/info/exclude`). A re-verifies alignment and
  re-measures the event lists on the new files; the implementer uses A's updated §sync and event lists.
- Stems read by KEY (`stem.<key>.*`). Roles: Kick `drums` (keeps the drum signals), Cloud Diving `synth`, BASS `bass`,
  the rest `other`. No classifier on single-instrument stems except Kick's role. `feeds.beat` = the Kick stem.
- Feline remains available: the existing Feline project/stem set is kept (a second project variant or the current one
  renamed; the implementer picks the existing mechanism with the least churn). Sabertooth becomes the default.
- Mapping: A's table (cloudSwell/cloudHue, crashStar 300 ms refractory, moogNote ×1.6 catch-all + moogGlow, kickBeam,
  snareBeam 100 ms refractory, tomHit/tomBody, blockTick). All transients attack 0. Mix-only: transient effects don't
  fire; the aurora keeps its mix response. Pass 2's ring-of-two beam rule is retired (kicks 1.7/s).

## Effects (B's recommendations, adopted)
1. Cloud Diving → aurora: B §1 parameter set; drum/bass routes into the aurora removed; never route speeds/ribbonSteps/
   ribbonScale. Swell attack may go to 500-600 ms if the flash gate needs it.
2. Crash → shooting star: the existing comet, fast (0.45 s crossing, 1.0 s life, no wisps), pool of 2, environment
   response (aurora lift near the path, selected facets, river mirrors it). Pass-2 comet and cometCue removed.
3-4. Glints: valley mesh split into left and right files, each with its own program; per-crease random in v; cosine-comb
   subsets; Moog new subset per note (left); tone block bar-position sequence and toms whole-plane flashes (right).
5. Kick/Snare river light: engine E2 `path` wave on the water's river course; variant v4 (core + halo + coarse shore
   caustic + gentle broken wake under the reflection). Kick amber single core, snare cyan twin line, faster/shorter.
   Pass-2 river pulses (perpendicular groundPulse) removed. Beam brightness fades in with distance at launch.
   Owner's staged order (20b): path beam with deterministic test events → wake/caustic → ripples + sparse rays (E3) →
   Kick+Snare on Sabertooth → profile. Comparison clips at each step.
6. Bass/HH: decided only after the primary mappings work (B's zero-op candidates: bassFlow on water streak strength;
   once-a-bar open-hat sparkle).

## Engine changes (ADR 1661-1669, each with an off = byte-identical test)
- E1: material op ceiling 48 → 64.
- E2: `path` wave kind tied to a terrain water feature (B §5 tests).
- E3: upward rays via the existing Light Beam shells along the path (stage 3; drop if it doesn't earn its place).

## Stops
- Stop 1, after the river beam stage 2 (path beam + wake/caustic, deterministic events + on Sabertooth): owner review
  of the beam with a comparison clip, plus the sky (Cloud Diving + Crash) and glints clips. The beam is the effect the
  owner has steered most.
- Final: brief §16 report; live acceptance on AC + the LG 60 Hz (verify the paced display is the LG at 60 Hz before any
  live run; research B's live run paced at 120 Hz and is void), sound-to-light on the onset paths, both suites exit 0
  (`run-suite.sh gpu` bare; gpu-lock only for cpu-assets).

## Budget
B's combined worst case 12.4 ms GPU p50 at the lift (all effects on) vs today's 13.7 ms; E2's beam +0.3-0.4 ms upper
bound; rays +0.05-0.15 ms. Comfortably inside the 33.3 ms period.
