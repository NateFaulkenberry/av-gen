# ADR-140: The march and the composite are two costs, and one of them is below the instrument

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** Phase F, task F2 — measure the march separately from the composite
**Related:** ADR-077 (what the timeline can be used for), ADR-113 (a measurement carries its
conditions), ADR-139

## Context

The volumetric atmosphere is two render passes — a raymarch into a scaled target and a full-
resolution depth-aware composite into the HDR target — and both marked themselves `"volume"` on the
frame timeline. Every number the project has ever quoted for volumetrics, including §3.2's
"Constellation `volume` 2.29 ms, 64%", is their sum.

They are not the same cost and they do not respond to the same things. The march is
O(pixels × steps) and is the thing every scalability lever in Phase F moves. The composite is
O(full-resolution pixels) with a fixed 4-tap footprint and moves with none of them. Summed, a lever
that halves the march looks weaker than it is, and a lever that only touched the composite would be
invisible.

## Decision

The two passes carry two labels: **`volume.march`** and **`volume.composite`**.
`VolumeRenderer::collectTimings` reads the whole-volume figure through
`FrameTimeline::msForPrefix("volume")`, so every existing reader of "the volume cost" is unchanged
to the last digit, and `VolumeStats` gains `marchMs` and `compositeMs` beside `volumeMs`.

## What it found, immediately

1280×800, Apple M2 Max, Dawn/Metal, release, under `tools/gpu-lock.sh`, 120 frames with the first
12 discarded, revision `bfed449`:

| | Constellation | Glowmere |
|---|---|---|
| `volume.march` | **3.867 ms** | **0.655 ms** |
| `volume.composite` | 0.066 ms | 0.066 ms |
| whole frame GPU p50 | 6.226 ms | 13.435 ms |
| volume share of the frame | 63% | 5.4% |

**The composite is 1.7% of Constellation's volumetric cost and 9% of Glowmere's — and both figures
are one timestamp tick.** The counter's period here is 0.065536 ms, and 0.066 ms is a single
interval of it. So 0.066 is not a measurement of the composite; it is the smallest number this
instrument can report, and the composite's real cost is *at or below* it. Six other passes in
Constellation's frame report the same single tick.

**The consequence is that the composite is not a target and cannot be made one.** No optimisation of
it can be demonstrated by this instrument, because the instrument cannot resolve its cost in the
first place. Anything Phase F does about volumetrics is about the march. This is stated as a limit
of the measurement rather than as "the composite is free", because those are different claims and
only the first one is supported.

**The 64% share survives the re-derivation the wave-2 preamble asks for.** Phase A's 2.29 ms of
3.60 ms was 64% of a frame whose median is not a stable statistic (ADR-113). Today's 3.93 ms of
6.23 ms is 63%. Both absolute numbers are inside Constellation's own noise and neither can be
compared to the other; the *share* is the quantity that held, because both terms moved together
inside one session.

## Consequences

The `[.perf]` timeline test "a pass's timeline number responds to that pass's own workload" now
reads `volume.march` rather than `volume`, and so is strictly sharper than it was: it was previously
diluted by a composite that does not move with the step count.

The per-pass JSON record (`--bench-json`) and the per-frame log line now carry both labels, so this
split is in every record taken from here on rather than in this document only.

## Alternatives considered

**Keep one label and infer the split from an A/B that removes the composite.** Rejected: an arm
that removes the composite does not render the fog at all, so it measures the whole volume, not the
composite. Removal attribution (`attributeRemoval`) is structurally unable to separate two passes
that are always encoded together — which is the same blindness §3.2.3 records about the contact
march, and the same fix: mark the thing you want to measure.
