# ADR-900: A route's chain delays, holds an event through its attack, and takes its depth from a signal

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-011 (parameters and modulation: the fixed-order chain), ADR-088 (reactions are
routes), ADR-097 (spatial depth), ADR-179 (a program that asserts emission owns it)
**Found by:** the GV3 revision's modulation audit (`reports/modulation.md` §2, §4, §6, §7 C-D) and the
owner's brief §3, §5 and §16: a musical world needs correlated but non-identical responses --
stagger, delay, and depth that follows the song's sections -- and the chain could express none of
them. The same audit measured GV2's event routes arriving at 0.6-6.4% of their amount.
**Implemented by:** `ProcessorChain::delay`, `ProcessorChain::process`, `ProcessorChain::State`
(`src/params/processor.*`); `ModRoute::depthSource`/`depthMin`/`depthMax` and
`Modulator::applyRoutesWhere` (`src/params/modulation.*`); `chainToJson`/`chainFromJson`,
`routeToJson`/`routeFromJson` (`src/params/serialization.cpp`); `TimelineMode`,
`TimelineSource::hitBetween`/`positiveSpans`, `Source::readsTriggers`, `SourceRack::update`
(`src/signals/source.*`); `SignalBus::setEventKind`
**Tests:** `tests/unit/test_route_chain.cpp` (`[adr900]`); seek exactness in
`tests/integration/test_route_seek.cpp` (ADR-901)

---

## Context

The chain was `gain -> offset -> curve -> clamp -> threshold -> smoothing -> envelope -> remap`, then
`x amount x spatialGain x masterGain`, then the op. Four things a musical response needs were missing
or broken.

1. **No delay.** Every route on one signal moved on the same frame. The only stagger was a copy of a
   timeline source with a different `offset`, which works for scored sources and for nothing else.
2. **One-frame events were swallowed by the smoothing.** Smoothing ran on every sample, events
   included, so a one-frame event reached `1 - e^(-dt/attack)` of its amount: 81% at a 10 ms attack,
   24% at 60 ms, 1% at 1.4 s. GV2 multicam's `music.build -> scene/windSpeed` (attack 2.4 s) reached
   0.7%; its `music.section -> nodes/elder-2-cap/emissiveBoost` 0.8%; its eighteen `beat.pulse`
   routes 81%. The routes bound, applied, and did almost nothing, with no warning.
3. **No depth.** `amount` is a number in the file, not a parameter, so there was no way to make a
   route move its target more in the drop than in the verse -- section energy could not scale an
   Add route at all.
4. **A scored pulse could not trigger an envelope source.** Envelope sources fire on bus events, and
   a timeline source only published values. GV3 scored 475 kicks as 25 ms step pulses in a value
   timeline, which also fall between frames at 30 fps (109 of them, measured by ADR-902's rule).

## Decision

### The delay stage (first, `delayMs`)

- **A time-stamped history on the chain's own clock.** `State::clock` is the sum of the dts the chain
  has been given (or where a seek's replay left it, ADR-901). Each frame records `(clock, x, event)`;
  the stage then reads the signal at `clock - delay`. A delay is therefore the same number of seconds
  at any frame rate: 100 ms lands an event on frame 6 at 60 fps and frame 3 at 30 fps.
- **Values are interpolated** between the two samples either side of the delayed instant, so a delay
  that falls between frames (250 ms at 30 fps is 7.5 frames) is still exactly that many seconds, as the
  test at 30, 60 and 120 fps holds; the first version held the latest sample, which rounded the delay
  up to the frame grid and failed that test at 30 fps. Next to an event sample the value holds, since
  an event signal's value is its strength for one frame and 0 otherwise. Before the first sample the
  first one holds, as a timeline's first key does.
- **Events stay one-frame events.** Every event with a time in `(emittedThrough, clock - delay]`
  lands on this frame as one event at the strongest strength; afterwards the held value of an event
  sample is 0, as the bus clears it. Two instants within 1 µs are one instant (a sum of dts and a
  replay's `k / 60` must agree on the frame an event lands on).
- **A repeated instant re-emits** what it emitted (dt = 0: a seek's landing frame, a paused redraw),
  so an event is not lost on the frame a seek lands on and not emitted twice after it.
- **Ceiling 4000 ms** (two bars at 120 BPM), clamped. It bounds the history a route keeps and a seek
  checkpoint carries. The history is pruned to the one sample being held plus what is still ahead of
  the delayed instant, and compacted in place, so a steady state allocates nothing.

### Events are held through their attack

An event whose level (after the stages before smoothing) is above the smoothed value **latches that
level**, and the smoothed value rises to it in a straight line over `attackMs`, credited from the
start of the frame the event arrived in -- the interval the one-pole always credited a sample to, so
an attack shorter than a frame peaks on the event's own frame with no added latency. **The frame the
rise completes in shows the level in full**; the rest of that frame is not integrated, and the decay
(`decayMs`) runs from that frame on. A stronger event during a rise re-aims it from wherever it has
got to; a weaker one leaves it alone. An event below the smoothed value (a chain that inverts it
before the smoothing) does the same over `decayMs`.

- **An edge with no time on it lands the event at once, exactly as before** (an attack of 0 for a
  rising event, a decay of 0 for a falling one), and a signal with no event flags is smoothed exactly
  as before: `test_route_chain.cpp` checks both sample for sample against the old stage.
- **"Full amount" is exact, at every frame rate**: the sampled peak is the event's level whatever the
  attack, the decay and the frame rate (swept at 24-120 fps). The price is that the decay starts on a
  frame boundary -- up to one frame after the rise's end in continuous time -- which is where the old
  stage already started it for an attack of 0. Previews at 30 fps and finals at 60 fps show the same
  peaks.
- **Rejected on the way: decaying the rest of the completing frame.** Continuous-time exact, but the
  sampled peak is then `e^(-dt_left/decay)` of the level -- 7% for a 20 ms attack and a 5 ms decay at
  30 fps -- so a short pulse would look different in a preview and a final, and the brief's "must reach
  its full amount" would hold only for decays long against a frame.

### Depth follows a signal (`depthSource`, `depthMin`, `depthMax`)

    depth = depthMin + (depthMax - depthMin) * value(depthSource)
    final = current + (op(current, y) - current) * depth

So an Add route's offset scales, a Multiply route's deviation from 1 scales, a Replace route
crossfades from the value it found, and a depth of 0 leaves the target exactly as the route found it.
`depthMin` gives a floor: section energy 0..1 onto 0.3..1.0 keeps a quiet section's response at 30%.

- **Stateless**: a function of this frame's depth signal, so a seek lands where a play does whenever
  the depth signal does (ADR-091). Any signal on the bus can be the depth source.
- **A route without a depth source writes exactly what it always wrote** (no `a + (b - a)` rounding).
- **An unknown depth source leaves the route unbound**, reported as an unknown source is. A replay's
  bus that does not carry the depth signal skips the route, as it skips an absent source.

### Timeline sources can score events (`"mode": "event"`)

- Each key whose value is **above zero** is a hit at its time, of strength `value`. The source fires a
  one-frame event on the frame whose interval `(previous instant, this instant]` holds a hit, so a hit
  cannot fall between frames and a scored pulse triggers an envelope source.
- Keys at or below zero are ignored, so a score written as `0 -> 1 -> 0` step pulses reads the same in
  either mode: switching GV3's `kick` to event mode is a one-key change.
- The first frame after a reset (dt = 0: a play's first frame, a seek's landing) counts its instant as
  a hit time; a redraw of the same instant does not fire it again. Loops wrap; an interval longer than
  a loop fires every hit in it once.
- The bus is told the output is an event (`SignalBus::setEventKind`), because `declare` keeps the kind
  a name was first declared with.
- **The rack updates publishers first and trigger readers (envelope, random) second**, so an envelope
  listed before the timeline that triggers it still sees the hit. Only readers of rack-published
  events see a difference; the engine's own events (onsets, beats, music.*) are published before the
  rack runs, as before.

### Serialisation

- The chain writes `"delayMs"` (always, like every other chain field); absent reads as 0; a negative
  value is refused.
- A route writes `"depthSource"`, `"depthMin"` and `"depthMax"` only when it has a depth source.
- A timeline source writes `"mode": "value"|"event"`; absent reads as value; an unknown mode is refused.
- Entity reactions read and write chains through the same functions, so they take `delayMs` too.

### Rejected

- **Reordering the envelope before the smoothing.** It fixes nothing when `envelope` is None, and with
  an envelope it still smooths the event's first frame away.
- **Holding the event's level as the one-pole's target until it is reached.** A one-pole never reaches;
  99.9% takes 6.9 time constants, so a 60 ms attack would take 410 ms.
- **Starting the rise at the event's frame instant** instead of the start of its interval. Same
  sampled peak, one frame later: 33 ms of latency at 30 fps on every pulse.
- **A delay by a second, time-shifted signal pipeline** (a pure function of time with no history). One
  pipeline per distinct delay, and the source rack cannot be evaluated at another instant without its
  own copy. The history plus the seek replay (ADR-901) is exact on the replay grid for a fraction of it.
- **Changing `masterGain` and `spatialGain` into depths.** They multiply the output, so below 1 they
  pull a Multiply route's target towards 0 instead of reducing the modulation (`reports/modulation.md`
  §2). They should become depths, but that changes ADR-097's reactions in scenes with fields and the
  UI's master gain, and neither is this brief. Recorded as a defect.

## Consequences

**Every event route with an attack now arrives in full.** Measured over the repository's projects and
scenes with the old and new stages replicated in a script, at 60 fps (a one-off; ADR-902's audit is the
maintained instrument): **551 event routes with an attack in 47 files; 121 reached less than 10% of
their amount, 69 more less than 50%, and 329 more less than 90%; after this change every one of them
reaches 100%.** The families:

- **The Glowmere Valley 2 family** (`glowmere-valley-2`, `-2-song`, `-2-multicam`,
  `glowmere-atmospherics`, `glowmere-stylized` and the `_diag-water-*` / `_pre-defects` copies):
  11 routes per project that reached 0.6-48.7% now reach 100%. In GV2 multicam:
  - `music.build`/`music.break -> scene/windSpeed` and `-> particles/spores/spawnRate` now move the wind
    and spores by their full ±0.18-0.2 and +70/-80 (0.6-1.2% before);
  - `music.phrase -> particles/spores/turbulence` +0.18 and `music.section -> nodes/elder-2-cap/emissiveBoost`
    +0.06 arrive in full (1.2% and 0.8% before);
  - `music.drop -> scene/volumeScattering` +0.09 (6.4% before), `-> particles/spores/burst` +220 (81%);
  - `music.downbeat -> lightrig/GlowmereValley/elder-practical/intensity` +0.3 (24.3% before), and the
    downbeat and drop routes onto `nodes/elder-2-gills/emissiveBoost` (31.0% and 21.2% before) -- in
    full at the parameter; the gills are a program-lit procedural part, which the boost reaches only
    once agent/emission's post-program multiplier lands;
  - the eighteen `beat.pulse` routes onto the hero pulses, the travel beam and the aurora's edge
    (+8 and +2.7) peak at their full amount instead of 81% of it: **the hero rings are 23% brighter at
    their peak**;
  - `music.beat -> material/glowmereTissue/emissionIntensity` and `-> nodes/valley/water/swell` reach
    100% (48.7% and 60.4% before).
- **`ufo-stack`** (11 routes below 50% before), **`glowmere-lyrics`** (12), **`constellation`** (1).
- Routes that already reached 81-98% (a 2-25 ms attack: cathedral, chamber, night-shift, helix,
  hyperspace, lab, temple, worlds, the Tree of Life island and its `_vx2-*`/`_ca-vg-*` copies, the
  craft-lights profile and the Glowmere scenes' saucer reactions) now reach 100%.
- **Glowmere Valley 3 is unchanged by the attack rule**: its kick route has no attack and its other
  timeline routes are value pulses (continuous signals), which are smoothed exactly as before.

- **The rack's new update order changes nothing shipped**: no project in the repository has an
  envelope or a random source (167 LFOs, 32 control sources, 4 macros, no timelines outside GV3).

No route's behaviour changes unless it reads an event with an attack, a depth source, a delay, or an
event-mode timeline; no project in the repository has the last three, since they did not exist.

The change was verified by the old-versus-new controls in `test_route_chain.cpp` (the old stage lets
less than half of a 60 ms-or-longer attack through at 30, 60 and 120 fps, the new stage lets it all
through); by the replica scan, where every one of the 551 routes reaches 100%; and by ADR-902's audit
of GV2 multicam, which samples the chain itself and reports no `event-swallowed` route.
