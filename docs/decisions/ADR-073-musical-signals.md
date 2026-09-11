# ADR-073: Musical signals

Status: Accepted

## Context

ADR-063 ends with a line that turned out to be the whole problem: *"Not yet wired: nothing
subscribes to these events."*

`signals::MusicalEventDetector` is complete. It recognises beats, downbeats, bars, phrases,
sections, energy trends, builds, breaks, drops and impacts; it is conservative on purpose; it is
tested against twenty seconds of steady tone and refuses to find drama in it. And outside those
tests, nothing in the project ever called it. Builds, drops and phrases were detected in principle
and never once in practice, so every scene was still inferring drama from a raw band level — which
is the exact failure ADR-063 was written to end. A classifier nobody calls does not end it; it
documents it.

The gap was never the classifier. It was that the detector takes a `MusicalFrame` and the engine
has an `AnalysisFrame`, and nobody had decided *when* to hand one over.

## Decision

`app::MusicRuntime` sits between the analysis pipeline and the signal bus, and does three things.

**One signal per event kind, named by the classifier rather than by hand.** `music.beat`,
`music.downbeat`, `music.bar`, `music.phrase`, `music.section`, `music.energyRise`,
`music.energyDrop`, `music.build`, `music.break`, `music.drop`, `music.impact` — each name is
`"music." + signals::musicalEventName(e)`, built by walking the enum, so the bus and the classifier
cannot drift apart by a rename. Each is declared momentary, exactly as `beat.pulse` is, which means
the existing attack/decay/peak-hold processors shape them without a line of new modulation code.
There is no second event system: a musical event is a signal, and a signal is something the
modulation graph already knows how to route.

The enum has no count sentinel, so the count is discovered at declare time by round-tripping each
index through `musicalEventName` / `musicalEventFromName`. `musicalEventName` answers `"beat"` for
anything out of range, so the first index that fails to round-trip is one past the end. A hard-coded
eleven would have worked until the twelfth event was added, at which point it would have silently
published onto `music.beat`'s id — a failure that looks like a classifier bug and is not one.

**The detector's clock is the analysis clock, never the render clock.** This is the decision the
rest follows from. ADR-063 was careful to smooth in seconds rather than in frames so that a 30 fps
offline render and a 120 fps window would agree about where a drop is; feeding it one frame per
*render* frame would have thrown that away at the first call site, because the number of samples and
the dt between them would then be a property of how busy the GPU was. Analysis frames arrive at the
hop rate and are a pure function of the audio, so the runtime consumes one `MusicalFrame` per
analysis frame and nothing else.

Offline, `Engine::update` walks every analysis frame whose centre has passed and publishes only the
last — so the classifier is fed from inside that loop, not from `publishFrame()`, which at 30 fps
would show it one analysis frame in three. `consume()` is idempotent per `frameIndex`, which is what
makes it safe to call from both places: the loop walks the batch, `publishFrame()` offers the last
of it again, and the second offer is recognised and dropped.

**The metre comes from the analysis frame, not from the bus.** `beat.bar`, `beat.phraseCount` and
friends are extrapolated forward by `deltaTime` between analysis frames, so they step at slightly
different moments at different frame rates. `AnalysisFrame::beatCount` does not. Bars, phrases and
sections are divided out of that count inside the runtime, using the project's `phraseBars` and
`sectionPhrases`, for that reason alone.

**Events accumulate between publishes.** One render frame at 30 fps consumes about three analysis
frames, and an impact in the first must not be overwritten by the quiet in the third —
`Engine::update` already merges onsets across the frames it skips for the same reason.  `consume()`
collects the strongest of each kind, `publish()` writes them once per render frame and clears.
Firing and strength are tracked separately: a beat during near-silence is a real beat with a
strength of zero, and folding the two together would drop it.

`publish()` runs unconditionally, so no audio means every `music.*` signal reads false rather than
holding whatever it last said. The runtime is reset on a load, a seek, a stop and an input change,
because the detector's energy history across a playhead discontinuity reads as a drop.

Nothing here goes near the audio thread. `consume()` runs on the render thread over a frame the
analysis thread has already finished with, and allocates nothing per frame: every accumulator is a
fixed array and the detector reuses its own moment buffer.

## Consequences

The eleven signals exist and carry strength, so an author routes `music.drop` at a bloom intensity
the way they already route `audio.rms`, with the same processors and the same project file syntax.

Determinism is exact offline and approximate live, and the difference is worth naming. Offline, both
render rates feed the classifier the identical analysis sequence, so the recognised moments are
bit-identical — the test asserts equality, not closeness. Live, `AnalysisRunner` publishes through a
triple buffer that keeps only the newest frame, so a render thread slower than the analysis thread
loses frames and the detector sees a longer dt. That is a pre-existing property of the live pipeline
rather than something introduced here, but it does mean a live window and an offline render of the
same file can disagree at the margins. Closing it would mean queueing analysis frames rather than
latching the latest, which is a change to ADR-004's contract and not this one's.

`MusicRuntime` is header-only. That is a build accident, not a preference: `src/app/*.cpp` is
globbed into the application target while `avgen_tests` names the individual app sources it
compiles, and an implementation file not on that list leaves the test binary unable to link an
`Engine`. The definitions move out of the header the moment it joins that list.

What is still not built is the other half of ADR-063's milestone 4 — composable visual *responses*
with duration, easing, cooldown and probability. The overlap between that and the existing
route/macro/state machinery still needs resolving, and this ADR deliberately does not resolve it: it
puts the events where the machinery that already exists can reach them, and stops.
