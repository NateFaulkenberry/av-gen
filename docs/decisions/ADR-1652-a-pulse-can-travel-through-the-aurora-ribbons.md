# ADR-1652: A pulse can travel through the aurora ribbons

- Status: Accepted (eng/aurora-borealis)
- Extends ADR-1641 (the aurora ribbons) and is driven by ADR-1601's envelopes through an ordinary route.
- Asked for by Aurora Borealis (01-research-scene.md §6 E3; 03-plan.md: "the kick drives the E3 pulse"): every aurora
  parameter is global, so a kick could only flash the whole sky. The brief wants events to drive accents with shape.
- Code:
  - `src/world/atmospherics.{hpp,cpp}`: `AuroraShape::pulse` (0..1), `pulseGain` (0..16, default 2), `pulseThicken`
    (0..8, default 1), `pulseWidth` (0.02..1 curtain heights, default 0.15), validated; packed into a new
    `AuroraGpu::ribbon2` only when the ribbons are on and `pulse` > 0 (otherwise every lane is 0).
  - `src/world/effects/kinds/aurora_effect.cpp`: four fields in the Ribbons section (`shape/pulse...` in the file;
    parameters `fx/<id>/pulse`, `pulseGain`, `pulseThicken`, `pulseWidth`, all routable).
  - `shaders/common.wgsl`: `AtmosAurora::ribbon2`. `AuroraGpu` 272 -> 288 bytes; `FrameUniforms` +32 (the size and
    offset asserts in `scene_renderer.hpp`, `effect_conformance.cpp`'s frame size 2660 -> 2692).
  - `shaders/atmosphere_fx.wgsl`, in `ribbon pulse (ADR-1652)` blocks: `atmosAuroraRibbonsPulsed` and the branch that
    calls it.
- Tests:
  - `tests/rendering/test_aurora_pulse_gpu.cpp` (`[adr1652]`):
    - "with the ribbon pulse at 0 the sky is byte-identical to the shader with no pulse code (ADR-1652)" (every other
      pulse setting off its default; a curtain aurora with a pulse also identical; the control: on, it differs);
    - "the ribbon pulse brightens the ribbons as it passes and climbs as it decays (ADR-1652)": at 1 it adds over 15% of
      the ribbons' light; the light it adds is centred higher in the frame at 0.55 than at 1, and higher again at 0.25,
      and fainter;
    - hidden `[measure]`: "aurora ribbon pulse and step scale: cost (measure)".
  - `tests/unit/test_aurora_audio_response.cpp`: "aurora ribbon pulse packs only when on, validates, round-trips and is
    routable (ADR-1652)".

## Decision

**One front, climbing the curtains.** At height `h` above each rim (in curtain heights, the march's own rim test, so it
follows each rim's curve), the front stands at `1 - pulse` with width `pulseWidth`:

`glow = pulse * exp(-((h - (1 - pulse)) / pulseWidth)^2)`

- sheets are `1 + pulseThicken * glow` times wider there, and their emission `1 + pulseGain * glow` times brighter;
- route an envelope onto `pulse` (fast attack, a few hundred ms of decay): the kick puts the front on the lower rims, and
  as the envelope decays it climbs a curtain height and fades with it. No new pass, no new state, no new clock: the
  position is the parameter.
- Upward, not outward: a front rising through the curtains is the aurora's own pulsation, and it reads on every sheet in
  view at once, which an outward ring from the viewer would not (and would read as a vortex).

**A copy of the march, not a branch inside it.** The first version gated the two terms inside `atmosAuroraRibbons`; at
pulse 0 its pixels moved (the identity test failed: the compiler built the shared loop differently). So the pulsed march
is a copy of the function with the two edits, called only when `ribbon2.x` > 0; pulse 0 runs exactly the function it ran
before. The price: a change to the ribbons belongs in both functions (said in the comment above the copy). Nothing
fails automatically if they drift apart; a later refactor could generate both from one source.

**Ribbons only.** The curtains never read it (they pack nothing).

## Evidence (M2 Max, AC power, Release)

- Identity at 0: hash for hash against the shader with the pulse blocks removed.
- Cost (hidden `[measure]`, 1080p mostly-sky frame, 48 planes, the scene label over 8 interleaved reps of 100 frames;
  bimodal clock states): pulse 0 median ~12.3 ms (fastest 11.25), pulse 0.7 median ~13.3 ms (fastest 12.12). About
  **+0.9-1.0 ms while a pulse is up** (wider sheets mean fewer of the march's early-outs), and nothing at 0. A kick
  envelope is above 0 a fraction of the time.

## Consequences

- The scene routes `kickWave` (an ADR-1601 envelope) onto `fx/aurora/pulse` with depth 1 and leaves `pulse` at 0.
- Budget the pulse's cost at the kick's peak, not its mean.
- A held-open envelope (never returning to 0) costs the pulsed march all the time.

## Rejected

- **A global flash** (intensity or edge brightness on the kick): the weak version the brief calls out.
- **A field-bus `Wave`** sampled by the march: a new dependency in the hot loop for the same front.
- **Gating inside the shared march**: not identical at 0 (above).

## Revisit triggers

- An outward or along-ribbon travel is wanted: add a mode lane (`ribbon2` has none free; a second vec4).
- Several overlapping kicks: a second front from a second envelope.
