# ADR-1471: A resonance source can be a square plate

- Status: Accepted (art/resonator-quality)
- Amends ADR-1400 (the resonance field) and ADR-1405 (designed orders). It adds a source kind, so every existing source
  draws exactly what it drew.
- Found by: the Resonant Visual Worlds overhaul ([`docs/prototypes/resonator-quality/05-plan.md`](../prototypes/resonator-quality/05-plan.md)
  §1.1). Cymatic World "relies too heavily on a repeated radial/cellular pattern".

## Problem

The resonance field has two mode geometries:

- **point:** `R_m(kr) cos(m(alpha - theta))`, the circular-membrane family (rings, petals, targets);
- **lattice:** `n` plane waves half a turn apart (stripes, squares, hexagons: a periodic carpet).

Neither draws the Chladni figures of a vibrating plate, the curved crossing nodal networks, loops and lobes that the
word "cymatic" calls up. Those are the square-plate modes `cos(n pi u) cos(m pi v) - cos(m pi u) cos(n pi v)`, and
their compound forms. A CPU look-development sketch of the families (the overhaul's proofs) showed a plate (3,2),
(5,1) or (7,2) reading as a different world from the point bank's rings, where more orders or centres on point
sources stay a target.

## Decision

Add `"kind": "plate"`, with `"plates": [[n, m], ...]` (1..8 each, n != m), cycled along u like `orders`. Mode (n, m)
keeps the bank's wavenumber k (so the plate's side is `L = pi sqrt(n^2 + m^2) / k`), and is:

```text
phi = sqrt(2) / 2 * ( c(a) + c(-a) - c(90 deg - a) - c(a - 90 deg) ) * w,  tan a = m / n
c(b) = cos(k x . (cos(theta + b), sin(theta + b)))
     = sqrt(2) * ( cos(n pi u / L) cos(m pi v / L) - cos(m pi u / L) cos(n pi v / L) ) * w
```

`w` is the source's `reach` and `depth` envelope, as for a lattice; env2 is `w^2` (a mean square of 1, the sand's
normalisation).
- **Encoding.** The mode stores `a` in `shape.y` and geometry 3 in `shape.w`. The ripple test (geometry 2,
  ADR-1403/1409) becomes `2`, not `> 1.5`, on the CPU and the GPU.
- **Orientation.** A plate source turns as one (one orientation per source, `spin`), like a lattice.
- **Excluded pairs.** `(n, n)` is identically zero, and `(n, 0)` has coincident waves that need another
  normalisation, so neither is accepted.

## Consequences

- Cymatic World's overhaul gets the Chladni vocabulary as a resonator source, so audio, memory, competition,
  focus, drift, readers, bakes and routes all apply unchanged. Its family director crossfades plate figures with
  hexagonal, radial and arc families.
- Existing sources are untouched:
  - the JSON and the structural hash of a source with no `plates` are byte-identical;
  - Cymatic World, Waveform Dimension, Resonant Silk and Sonic VFX's Cymatic Plate render bit-identically (equal
    md5) with the binary and shader before and after this change;
  - the existing `[gpu][resonance]` parity cases pass unchanged.
- Tests:
  - `[unit][resonance][adr1471]`: the mode equals the plate formula at 25,600 points for (3,2), (5,1), (2,1); its
    mean square is ~1; a plate is not a ripple row; the JSON round trip, hash and validation (`(n, n)`, `(n, 0)`,
    out of range, empty, malformed); the resolved modes' angles and the shared orientation; no `plates` key without
    plates.
  - `[gpu][resonance][adr1471]`: a plate source beside a point and a lattice, in every channel, against the CPU
    reference.

## Rejected alternatives

- **More point orders and centres.** That is still the circular family (the brief's anti-pattern, "more radial
  rings").
- **Rotated readers of a one-direction lattice** (four readers at ±a, ±(90 − a), with signed strengths). This is
  exact, but costs four fields per figure and Cymatic World uses all 16. It also fixes one (n, m) per reader set.
- **A continuous "plate angle" morph.** Non-rational angles give quasi-periodic mush, not organised figures (the
  proof `cw-B`). A plate's figures are discrete, and the music crossfades between them.
