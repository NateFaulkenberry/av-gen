# ADR-1580: A project's feeds route each response hit class and the level family to a source

- Status: Accepted (eng/stems-feed)
- **Amended** by ADR-1620: `hits` on the mix gives no kick; `"kick": "audio"` is a load warning.
- Plan: `docs/prototypes/stems-multichannel/08-feed-plan.md` (approved with the owner's answers, 2026-10-20).
- Code:
  - `src/audio/feeds.*`: the `feeds` block;
  - `src/analysis/response_feed.*`: `ResponseFeed`, `FeedFolder`, `mergeFeeds`, `FeedProvider`;
  - `src/sonic/response.*`: `ResponseModel::step` reads a feed;
  - `src/sonic/sonic_runtime.*`: `SonicSetup::feed`;
  - `src/analysis/channel_analysis.*`: the per-source feed queue and `LiveFeedMixer`;
  - `src/sonic/live.*`: `LiveSonic::frame(..., feeds)`;
  - `src/app/engine.*`: `setFeeds`, `rebuildFeeds`, load and save;
  - `src/ui/feeds_row.*`.
- Tests:
  - `tests/unit/test_feeds.cpp`;
  - `tests/integration/test_stem_feeds.cpp`.

## Context

Stems and live input channels published `stem.<key>.*` (ADR-1520/1521), but `response.*` (ADR-1062, ADR-1510) read
only the main mix. On a full mix the bass line's attacks fire `response.low`, and the kick detector has to find a kick
under everything else. A project with a clean drum stem had no way to say "the kick comes from the drums" except by
rewiring every route from `response.kick` to `stem.drums.kick`, which loses the envelopes and the performer's knobs.

## Decision

**(a) feeds, not (b) a per-source response vocabulary, and not (c) both.** The owner chose (a).
- (b) would declare about 27 signals per source and step a response model per source in every step and every seek
  replay.
- It would also force every route on `response.*` to be rewritten.
- Much of it duplicates `stem.<key>.level` and `.kick/.snare/.hat`.
- (a) adds no signal, and the same route plays the drum stem's kick.

**The block (project top level):**

```json
"feeds": {"hits": "stem.drums", "kick": "stem.kick_in", "hat": "audio", "levels": "stem.bass"}
```

**What each key routes:**
- `kick`, `low`, `snare`, `hat` and `onset` route one hit class each (and its envelope).
- `hits` sets every class not named on its own.
- `levels` routes the whole level family: `bass`, `level`, `levelSlow`, `intensity`, `sustain`, `transient` and `flux`.
  It is one source so that their adaptive loudness reference (ADR-1510) is the loudness of the signal they read.
- `melodic` and `pitch` stay on the mix (the timbre stage is mix-only). `note` stays MIDI. `hatRate` counts the hat feed.

**What a value names:**
- `"audio"` is the mix.
- `"stem.<key>"` is an offline stem of that key, or, while an input is open, the input source of that key.
- The block is written only when a feed is set.
- A value that is neither is a load warning, and then the mix feeds everything.

**Inactive and unknown sources:**
- An inactive source feeds nothing: no hit, and its levels below the gate.
  - This covers a stem outside its span, and an absent, disconnected or stalled channel.
  - The mix is not substituted, so a dead kick channel cannot pass for a working one.
- A key that names no stem or source is a project warning, and that part reads the mix. A typo does not black out a show.
- A fed source gets the hits tier (the causal detector) whatever its own `hits` says. The saved flag is unchanged.

**How:**
- `ResponseModel::step` reads a `ResponseFeed`: the seven things of a hop's `CausalOnsets` it ever used.
- With no feeds it is built from the mix's onsets every step, float for float. The default path is bit-identical:
  `--sonic-trace` on Feline, Trench and Hyperborea compares equal, every column, before and after.
- **Folding by time:** a source hop belongs to the first mix hop at or after it (project time = stem time + offset).
  - Events in the window are OR-ed at their strongest, with the least drumness.
  - The levels are the newest hop's.
  - More than 250 ms stale counts as inactive.
- **Offline:** `SonicSetup::feed` holds one merged feed per analysis frame (about 60 B a hop). It is rebuilt when the
  feeds, the stems or the track change, and hashed into the seek-replay key. A seek therefore replays it exactly; the
  test compares every `response.*` after a seek and after a play.
- **Live:**
  - `ChannelAnalysis` pushes each fed source's feed into a fixed SPSC queue: no lock, no allocation, dropped when full
    and counted.
  - `LiveSonic::frame` folds the queued hops into each mix snapshot by the input's frame clock. Both streams come from
    one capture callback, so their clocks agree.
  - A hop that arrives after its mix hop was stepped folds into the next one.

**ADR-1510 is kept:**
- Each level reads against the adaptive reference of its own source.
- Hits stay level-free.
- `tools/check_response_range.py` passes unchanged (no feeds).

## Consequences

- **Measured on Feline Footwear and its stems.** With hits from the drum stem, `response.low` stops firing on bass
  notes (781 to 494 attacks).
- **A percussive levels source pulses.** With levels also from the drum stem, `response.level` pulses with the
  hits: ripple 0.65, a kick envelope's scale, against 0.14 from the mix. Route levels from a sustained source (a bass,
  a pad, the mix).
- **What the fixture tests check:**
  - With hits and levels from the drum stem, its 8-10 s silence feeds no hit, and the level falls under 0.05 while the
    mix's stays above 0.3.
  - A late stem feeds nothing before its offset.
  - A typo warns and reads the mix.
- `stem.<key>.kick/snare/hat` start firing on a fed stem that had `hits: false`. No shipped scene routes them.
