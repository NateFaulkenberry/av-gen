# ADR-331: A cap on an authored value is a refusal, and this one refused in silence

**Status:** Accepted
**Date:** 2026-09-18

`material/emissive` was registered with a hard maximum of **50**. A scene file asking for 256 ran at
50, and nothing anywhere said so:

```
CHECK_THAT( high.p.emissive->value(), WithinAbs(256.0, 1e-3) )   ->  50.0f is within 0.001 of 256.0
CHECK_THAT( high.p.emissive->hardMax(0), WithinAbs(256.0, 1e-3) ) ->  50.0f is within 0.001 of 256.0
```

Found by the HDR Lab, whose own fixture asked for 256 and got 50, and filed in
`docs/engineering-labs.md` §8 item 6 as "ADR-225 in an authoring format — the same shape as the
Lighting Lab's `coneDegrees`, in a different parser". It matters because `emissive` is the only route
a scene file has above unit radiance: `baseColor` and `emissiveColor` clamp to [0, 1] by the colour
they are. The same lab measured a neutral highlight beginning to bloom at scene-linear **0.55** and a
pure blue not until **7.6**, so values well above 1 are the working range rather than a pathology.

Two things were wrong, and they want different answers.

---

## 1. The cap is an affordance, so it rises; it does not disappear

A hard range belongs to the **parameter**. Its job is to keep a modulation route and a typed entry
inside something a person could have meant, and 50 is a round number somebody picked — not a bound
the quantity has, the way `roughness`'s 1 and a colour channel's 1 are bounds the quantity has.

A number in a scene file is not a UI gesture. It is an authored statement, and a ceiling that
overrules it is a refusal wearing a slider's clothes. So:

```cpp
p.emissive = r.f("material/emissive", m.emissiveIntensity, 0.0f,
                 std::max(kAuthoredEmissiveCeiling, m.emissiveIntensity), 0.0f, 8.0f);
```

The **soft** range stays 0..8, which is the only part of it the panel draws, so the control a person
works with is unchanged. And the cap is not removed: with no ceiling at all a route could drive this
to infinity and a typo would be indistinguishable from an intention. `std::max` means every object
authoring 50 or less keeps exactly the range it had — nothing shipped moves, and an arm asserts that
an ordinary 2.5 still reports `hardMax` 50.

The general rule this is an instance of: **a parameter's hard range must contain the value its file
authored, wherever the ceiling is an affordance rather than a property of the quantity.** The
distinction is not automatable and was not automated. `roughness` is still [0, 1]; `lod/spread` is
still capped at 0.5 for the reason its own comment gives (beyond that the band is wider than the gap
between adjacent thresholds and a level could never be reached).

## 2. A clamp that still happens has to speak

Which is the other half, and the more useful one, because the first half only fixes the field the
lab happened to try.

Every range in the eighty-row table now reports the authored values it overrules — by name, with the
number the engine actually runs:

```
procedural 'proc/thing': material/roughness is 5 and runs at 1 -- the parameter's range is [0, 1]
procedural 'proc/thing': material/baseColor.y is 2 and runs at 1 -- the parameter's range is [0, 1]
```

This is ADR-278's rule for **values** rather than keys. That ADR built `warnUnknownKeys` because "a
setting the application does not read is not a setting, and the file gives no sign"; a setting the
application reads and then overrules is the same defect with an extra step.

Three choices inside it, each with a reason rather than a taste:

* **A warning, not a refusal**, matching ADR-278 for the same two reasons: a file written by a newer
  build and read by an older one should lose a value and not a world, and the report has to survive
  being wrong about the future.
* **`ProceduralParameters::clamped`, data rather than a log line.** It is `unknownKeys`' argument
  again: a test can assert what would be warned about without capturing spdlog.
* **A field filled by the one traversal**, not a second pass over the same numbers. A second pass is
  a second copy of an eighty-row table, and the drift between a copy and the thing it copies is
  precisely what ADR-278 spent its length on one layer up.

Colours report **per component** — `material/baseColor.y` — because a colour clamps channel by
channel and "baseColor was moved" does not say which channel the file lost.

## 3. The number that makes this a finding rather than noise

The whole CPU suite — 2,257 cases, which load every scene in `examples/` — emits **zero** of these
warnings. Nothing in the repository authors a value its own table refuses. So the first one anybody
sees will be a real one, which is the only condition under which a warning like this survives.

## The arms

`tests/unit/test_procedural_authored_range.cpp`, verified against the old table:

* an authored 256 is the value **and** the hard maximum, and reports no clamp (a fix that raised the
  ceiling and still announced a clamp would be telling the author they were overruled when they were
  not); the soft maximum is still 8;
* the control: an ordinary 2.5 keeps `hardMax` 50 and reports nothing, so this is not "the cap was
  deleted";
* a `roughness` of 5 still runs at 1 **and** is reported, with the authored and running numbers, and
  the parameter is checked to actually run at what the report says — the half a message on its own
  could get wrong (`REQUIRE( r.reported("material/roughness") ) -> false` before);
* a colour reports `.y` and `.z` and not `.x`, the in-range control inside the same vector;
* a default `ProceduralGeometry` reports nothing at all.

## Consequences

- `src/scene/procedural.hpp`: `ClampedAuthoredValue`, `ProceduralParameters::clamped`.
- `src/scene/procedural.cpp`: `Registrar::report`; `material/emissive`'s ceiling becomes a floor
  under the hard maximum.
- `tests/unit/test_procedural_authored_range.cpp`.
- `docs/engineering-labs.md` §8 item 6 closed.

## Revisit triggers

- **The first shipped scene that trips the report.** Then the question is which kind of bound it hit,
  and §1's distinction is the thing to apply — not to widen everything.
- **The same defect in another parser.** `LightRig`, the particle reader and the composition's own
  environment block all clamp authored numbers into ranges, and none of them says so. This ADR is the
  pattern; `clamped` beside the parsed thing is the shape.
- **A parameter panel that offers the hard range.** The soft range is what it draws today, which is
  why widening the hard one costs nothing. A control that typed into the hard range would make
  `hardMax` visible and the argument in §1 would need restating in terms of what the panel shows.
