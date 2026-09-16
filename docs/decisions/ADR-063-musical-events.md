# ADR-063: Musical events

Status: Accepted

## Context

The analyzer publishes rms, six bands, spectral centroid and flux, onsets, tempo, and a beat clock
with phase, bar, phrase and section counters. What it does not publish is *structure*: the
difference between a beat and a downbeat, between a passage getting louder and a drop, between a
break and silence.

Those are what a visual narrative is cut to, and without them every scene infers drama from a raw
band level — which is why so much audio-reactive work, including this project's own, ends up as
"everything pulses to rms".

## Decision

A classifier over the signals that already exist. It takes a `MusicalFrame` of values rather than
reaching for the bus, which is most of why it is a separate object: "does a break followed by a
loud downbeat produce a Drop" should be answerable without a device, a file or a decoder.

**Two energy time constants, not a derivative.** A short one that follows the music and a long one
that remembers what it has been doing; their ratio is the trend, and it is far steadier than any
difference between adjacent frames.

**Smoothing is in seconds, not frames.** A per-frame coefficient would make a 30 fps offline render
and a 120 fps window disagree about where the drop is, which for a deterministic engine is
unacceptable. A test plays the same music at both rates and requires the same events in the same
order within a quarter of a second.

**A drop is a two-part shape.** A break, then a resolution into loudness inside a window. Without
the break beforehand, a loud bar is just a loud bar. The first implementation tested for loudness at
the instant the break ended — which is by definition the moment energy has only just crept back over
the break threshold, so it never fired once. It is now armed by the break's end and satisfied within
`dropWindowSeconds`.

**Breaks are relative to a decaying peak**, not an absolute level, so a quiet piece is not
permanently in a break.

**Conservative on purpose.** A classifier that finds drama in a constant tone will find it
everywhere, and a visual system cut to a false drop looks broken in a way that one cut to nothing
does not. The most important test feeds twenty seconds of steady tone and requires that no
structural event fires at all.

## Consequences

Eleven event kinds with a strength that says how much of the thing happened, not how confident the
classifier is.

The thresholds are exposed because "what counts as a drop" is an artistic decision and the defaults
are only defensible, not correct.

Not yet wired: nothing subscribes to these events. Milestone 4's visual event graph — composable
responses with duration, easing, cooldown and probability — is the other half, and the existing
route/macro/state machinery already covers a good deal of it. That overlap needs resolving before a
second event system is built beside the modulation one.
