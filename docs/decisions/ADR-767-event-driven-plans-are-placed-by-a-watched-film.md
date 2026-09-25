# ADR-767: Event-driven plans are placed by a watched film

**Status:** Accepted
**Date:** 2026-09-25
**Related:**
- ADR-763, ADR-765 and ADR-766 (live performances; recording);
- the Motion lead's ADR-828 (character events) and ADR-800 (seek parity);
- ADR-870 (audio replay);
- spec §54 ("event-driven proposals").

**Implemented by:**
- `TimeRef::Kind::Event`, `ObservedEvent` and `MusicalContext::observed` in `src/directing/time_ref.*`;
- `Plan::observation`;
- `app::watchFromCopy` / `watchWorldEvents` / `makeWatchHook` (`src/app/directing_record.*`);
- `ai::DeferredResult` and `ai::WatchHook`;
- the `director.watch_events` tool, and observation attachment in the validate/propose/record tools.

**Tests:**
- `tests/unit/test_directing_events.cpp` (`[directing][events]`);
- the `[directing][agent][events]` ScriptedProvider test.

## Decision

- **A plan time can name something that happens in the film:**
  `{"event": "abduction/beam", "subject": "visitor", "occurrence": 2, "offsetSeconds": -1}`.
  - It is placed only by an **observation**: the events a play of the project raised from zero,
    carried in the plan itself (`observation: {events, until}`). The same plan and the same project
    therefore compile to the same times whenever they are compiled. Watching again refreshes them.
  - An unwatched event time is an error that says how to find out. An ambiguous one names its
    candidates. One that never happened suggests the nearest names.
- **Watching (`director.watch_events`, host hook).**
  - It plays a scratch copy from zero with the recording conditions: no live control, no audio,
    every body simulated.
  - It lists every world event with its time and who raised it: scenario beats, characters' named
    completions, goals' arrivals.
  - It runs off the main thread, exactly as recording does (ADR-765).
  - The observation is kept on the task and attached to the next plan that uses events and carries
    none. The model writes `{"event": ...}` and never copies a time.
- **Honest about audio.** Every event time carries a warning: it was timed with audio off, and
  behaviour that audio drives can move it in a render. Recording the characters involved fixes
  their behaviour.

## Consequences

- **Measured on the benchmark:**
  - Two watches of 30 s give identical event lists (9 events), 7.8 s each.
  - The played project (audio off) raises the second `abduction/beam` at exactly the watched
    second. A marker and a shot placed on it land there.
  - The agent test watches, proposes "mark the second beam", and is approved. The marker lands on
    the observed second, and the stored plan keeps its observation.
- **Proven red:**
  - Resolving every occurrence to the first moves the marker and fails the checks.
  - Not attaching the observation leaves the proposal with nothing to build.
- **Cost:** watching 120 s of the benchmark takes about 21 s, off the editor's frames.
- **Not done:** a panel control to watch. Event-driven plans come from the assistant, or from a
  plan written with an observation.
