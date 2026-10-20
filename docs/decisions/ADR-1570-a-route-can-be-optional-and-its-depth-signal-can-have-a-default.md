# ADR-1570: A route can be optional, and its depth signal can have a default

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-900 (depth from a signal) and ADR-902 (route liveness). Additive: a route without the new keys binds,
  evaluates and saves exactly as before, and writes no new key.
- Code:
  - `src/params/modulation.hpp`: `ModRoute::optional`, `ModRoute::depthDefault`, runtime `depthMissing`.
  - `src/params/modulation.cpp`: `bind` leaves an optional route with an unknown source unresolved without
    reporting it; a depth signal the bus lacks binds with `depthMissing` when the route has a default; `applyRoutes`
    reads the default for such a route.
  - `src/params/serialization.cpp`: `"optional": true` and `"depthDefault": <number>`, each written only when set.
  - `src/params/liveness.cpp`: an absent optional route is not `unknown-source` (its target is still checked); a
    depth signal with a default is not `unknown-depth-source`.
- Tests: `tests/unit/test_route_fallback.cpp` (`[adr1570]`), each with a control that fails under the old rule.

## Problem

Rainbow World is a live scene (no audio file). The stems feature (ADR-1520, ADR-1521) publishes `stem.<key>.*` from a
project's stem files offline and from mapped input channels live. The scene should follow the drum, bass and synth
stems whenever they exist, and fall back to the main mix (`response.*`, `audio.*`) on a plain stereo input.

The route system could not say that:

- A route on `stem.drums.kick` in a session with no drum stem is an **unknown source**: it is logged, counted as a
  binding problem, shown in the project warnings, and called dead by the liveness audit. A live scene that is fine on
  a stereo input would report a dozen problems every time it loads.
- A fallback route on `response.kick` cannot stand aside while the stem is active. ADR-900's depth signal is the right
  tool (`stem.drums.active`, depth 1 -> 0), but a depth signal the bus does not carry rejects the route, so the
  fallback died in exactly the session it exists for.

## Decision

Two optional keys on a route:

- `"optional": true` -- the source may legitimately be absent. An unknown source leaves the route unresolved and
  silent, with no warning, no binding problem and no liveness finding. An unknown target is still reported.
- `"depthDefault": <value>` -- the value the depth signal is taken to have while the bus does not carry it. The route
  binds and its depth is `depthMin + (depthMax - depthMin) * depthDefault`. Without the key, the old rule stands.

The pattern for a stem route and its fallback:

```json
{"source": "stem.drums.kick", "target": "...", "optional": true, ...},
{"source": "response.kick", "target": "...", "depthSource": "stem.drums.active",
 "depthMin": 1.0, "depthMax": 0.0, "depthDefault": 0.0, ...}
```

- No stem anywhere: the stem route is silent, the fallback plays at full depth.
- A stem declared later (stems analysed at load, an input source mapped): the engine rebinds (it already does after
  `setStems` and `setInputSources`) and both resolve.
- The stem active: the stem route drives and the fallback's depth is 0. Inactive (outside its span, a disconnected
  channel): the fallback is back. `stem.<key>.active` is published deterministically and is on the seek replay's
  bus, so seek equals play holds.

## Alternatives rejected

- **A `fallback` signal name on one route.** One chain would then process two signals of different kinds (a stem
  event and a mix envelope), with one amount. Two routes keep each signal's own chain and calibration.
- **Shipping stem routes only in the offline showcase project.** The live scene would then ignore stems mapped from a
  multichannel interface, which is half of what ADR-1521 is for.
- **Declaring every `stem.*` name in every session.** It would make `stem.drums.active` read 0 on a stereo input,
  which works, but it puts names on the bus for stems that do not exist, and the Modulation panel would offer them.
