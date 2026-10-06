# ADR-1164: A state trigger can hold, and a state can be timed

- Status: Accepted (proto/digital-mosh)
- Amends ADR-031 (scene states).
- Found by: DIGITAL MOSH. Its arc is a ladder of seven stages on one rising signal, with a respite in a
  breakdown, a collapse in four timed strata and a keyframe that holds before the dream goes on.

## Problem

There were two gaps.

**1. Crossing triggers can strand a ladder.** A `signal` or `macro` trigger fires on the frame its signal crosses
the threshold. A ladder's rungs are triggers `from` their predecessor (A to B at 0.3, B to C at 0.6), so a
crossing that happens while the machine is somewhere else is lost. The machine strands when:

- the signal jumps past two thresholds in one frame;
- a transition is running (`from` checks the committed state, which stays the old one until the morph ends);
- a state masks the signal and then unmasks it, as DIGITAL MOSH's breakdown respite does.

In each case the machine reaches B with C's threshold already exceeded and waits for a crossing that never
comes. In the arc simulation this left Trench in its respite for the rest of the song.

**2. Nothing could time a state.** `bar` triggers with `every` count bars globally (`barCounter_ % every`), not
bars since the state was entered, so "the collapse lasts four sections" or "the keyframe holds for 20 seconds"
could not be said.

## Decision

- **`"hold": true`** on a `signal` or `macro` trigger: it fires on every frame its condition holds (`value >=
  threshold`, or `<` when `falling`), not only on the crossing. With `from`, it is a level ladder that cannot
  strand. The existing rules still apply: the first trigger to fire wins the frame, and a trigger never re-fires
  toward the state already pending.
- **`"kind": "elapsed"`**: it fires when the committed state has lasted `threshold` seconds. It never fires
  mid-transition. The machine records when the state was committed. Use `from` for a state's own length; a
  target with `quantize: "bar"` lands on the bar.
- **`"idle": true`** on any trigger: it fires only while no transition is running or waiting for its beat. Without
  it, a periodic trigger (DIGITAL MOSH's camera moves on every phrase) interrupted the ladder's 12-second morph every
  phrase, the held rung restarted it, and the two restarted each other: the measured arc never left its first
  vantage. Marked idle, the move waits for the morph to land.
- **Reach:** JSON (`hold`, `idle`, and `elapsed` in the kind list), round-trip, and the World panel's state list. That
  list now also shows the comparison (`>=` or `<`), "(while)" for a hold, "after N s" and the `from` state.

## Consequences

- Test: `Hold triggers leave a state whose exit is already exceeded; elapsed triggers time a state` (`[adr1164]`).
  The signal jumps past two rungs in one frame:
  - without `hold`, the machine strands on the first rung;
  - with it, the machine reaches the top;
  - an elapsed trigger leaves the top after 2 s and not before;
  - `hold` and `elapsed` round-trip.

  `An idle trigger waits for the running transition to land`: a held trigger toward B and a periodic pull toward
  C, both from A. Without `idle` they restart each other and nothing lands; with it, B lands.
- DIGITAL MOSH's arc uses `hold` for every rung, and `elapsed` for the collapse strata (6 s, 5 s, 4 s, then the cut
  3.5 s later) and for the keyframe's hold (20 s).
- **Seek:** unchanged. The state machine is not replayed on seek (a seek replays signals, ADR-870, not states),
  before this ADR or after. Offline renders of a song therefore start at 0, as the arc requires.

## Rejected alternatives

- **Compound trigger conditions (AND of signals).** That is a small expression language. A single signal built by
  an interpret source (ADR-1020) already combines inputs, and `hold` makes its ladder robust.
- **Bars since entry.** Seconds plus a bar-quantised transition say the same thing and stay correct when the
  tempo is unknown or live.
