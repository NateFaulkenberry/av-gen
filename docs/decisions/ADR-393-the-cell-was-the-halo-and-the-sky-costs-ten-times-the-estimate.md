# ADR-393: The cell was the halo, and the sky costs ten times the estimate

- Status: Accepted (2026-09-20).
- Extends ADR-390 (the Cosmic Ocean), ADR-387 (the vortex joined the family), ADR-230 (atmospheric
  effects), ADR-170/182 (measurement and probes that can fail), ADR-385 (a stated reason is not
  evidence), ADR-350 (reachability and the round trip).
- Corrects ADR-390 in three places, each measured rather than argued: §8's `any()` instruction,
  §6's cost estimate, and §7's implied acceptance of the built shader.

This is the ADR for the run that made the Cosmic Ocean reachable and then looked at it. Everything
below was found by rendering the Tree of Life with the effect on, which had never been done.

---

## 1. The vortex was the `else`, and a fourth kind would have resolved as one

`resolveAtmosphericEffects` dispatched with `if comet / else if aurora / else vortex`. The vortex
was therefore not a *test* but the fall-through, so a kind the chain did not name did not resolve as
nothing: it resolved as a vortex, and was then dropped as "the second vortex in the scene". A silent
no-op wearing a limit's clothes.

**The probe had to carry a kind the chain does not name.** A probe over the three kinds that existed
proved nothing, because all three were named in the chain — ADR-182 applied to a dispatch rather
than to a pass. `AtmosphereKind` has a fixed underlying type, so a value no enumerator names is a
well-defined value of the type and is exactly the shape a forgotten `case` produces at runtime.
Against the old code it resolved as 1 vortex and 0 dropped.

The dispatch is now a `switch` with no `default` and a `claimed` flag, because **neither guard holds
alone**: the missing `default` makes a new enumerator a `-Wswitch` diagnostic, and
`AVGEN_WARNINGS_AS_ERRORS` is `OFF` (`CMakeLists.txt:33`), so that is a line in a five-thousand-line
build log. `claimed` is the half that holds at runtime.

### `effect_conformance`'s check 4 could not have stood in for it

The conformance layer exists to catch exactly this, and for a fourth kind it had quietly stopped
being able to. It decided which counter was "mine" with a ternary chain whose own `else` was
`counts.vortices` — so a new kind falling into the resolve's vortex arm would have been compared
against the vortex counter and **agreed with itself**. The same shape as the defect, one level up.
Both are exhaustive switches now.

## 2. `any()` does not gain `|| hasCosmicOcean`, and ADR-390 §8 is wrong about that

ADR-390 §8 calls `AtmosphericFrame::any()` "the single highest-risk line in the change" and
instructs that it gain `|| hasCosmicOcean` and nothing else, with the vortex's absence spelled out
in a comment. That instruction was written against ADR-390 §3's design, in which the ocean is **one
more term inside `atmosphereSkyAt`**.

The implementation on `agent/ocean` did not take that route. It is a separate pipeline with its own
`Draw(3)` inside the same render pass, for the reasons stated at the top of
`rendering/cosmic_ocean_renderer.hpp` — chiefly that appending forty-six lanes to `FrameUniforms`
grows the block `ShadowRenderer::upload` copies once per shadow view for data no shadow view reads.

Under *that* design, ADR-390's own argument against `|| hasVortex` applies word for word to the
ocean: `any()` gates the fullscreen **atmosphere** draw, the ocean contributes no term to it, and an
ocean-only scene would run a fullscreen draw that evaluates two empty loops and adds exactly zero to
every sky pixel. A cost, not a fix.

So there are two predicates. `any()` keeps its meaning and gains the note; `anyCosmicOcean()` gates
the ocean's own upload and draw. **What makes the effect reachable is neither of them** — it is one
line in `scene_renderer.cpp` calling `CosmicOceanRenderer::update` from `scene.atmospherics`, which
before this run had exactly one caller in the whole tree, a GPU test.

## 3. `buildAtmosphericFrame` found the vortex a second time, and the comment said otherwise

Found on the way, and ADR-385's shape exactly. The function resolved every effect, then **ignored
`counts.vortices`** and searched `effects` again with a loop that tested `e.enabled &&
e.vortex.active()` and nothing else — no activation window, no timing envelope. The comment directly
above it read: *"Its `enabled`, activation and lifetime envelope were already applied by the resolve
above."* They were applied, to a counter nobody read. A vortex inside a shut window rendered anyway.

One resolve, one answer: the counts now carry the resolved vortex and the resolved ocean, and the
frame is built from them.

## 4. The cell was the halo

**The first render of the Tree of Life with this effect on was a sky of boxes.** Every star sat in a
soft axis-aligned square, four of them meeting at each cell corner, and every planet had one too.
See `examples/treeisland/renders/cosmicocean/01-crop-the-square-lattice.png`; it is not subtle.

The cause is the same line in two places, and it is a consequence of a decision that is otherwise
right. `coCell` places at most one body per cell, inset from the edges, so that **no body crosses a
boundary and no neighbour search is needed** — four hashes per star instead of thirty-six. That
holds only for a profile with *compact support*. Both profiles were exponentials:

```wgsl
let halo = exp(-d / max(radius * (1.0 + co.atmos1.z * 2.0), 1.0e-6)) * 0.35;   // stars
let glow = exp(-(d - 1.0) * 6.0) * co.planet2.x * 0.5;                          // planets
```

An exponential never reaches zero. Whatever it still had at the cell edge was cut off there, by a
straight line, in the two directions the cube face's grid runs. The single-cell scheme and the
unbounded profile are each defensible and cannot both be there.

Fixed by capping the reach inside the cell and falling to exactly zero at it: a body is inset to
0.25..0.75 of its cell, so 0.24 of a cell is the largest reach that cannot cross a boundary. The
galaxy's `if (r > 1.6) return 0` is the same family — a hard cut at ~0.4% of the peak — and is now
tapered.

**The general rule, which is the reusable part:** *a per-cell body with no neighbour search must
have a profile that is zero at the cell boundary, and the cap belongs at the profile, not at the
brightness.* Anything else is a lattice you will see.

## 5. The measured cost is +5.05 ms, and ADR-390 §6 is wrong by an order of magnitude

`--ab cosmic`, 3 pairs of 240 frames interleaved, 1920x1080, `tools/gpu-lock.sh`, on the shipped
Tree of Life project plus one Cosmic Ocean at the "Blue Cosmic Ocean" style:

```
A/B gpu : baseline 24.90 ms, arm 19.92 ms, delta +5.05 ms (+20.26%); noise floor 2.00%
          per-pair: +5.18  +4.98  +5.05
          drift: 25.10 ms first half, 24.90 ms second (-0.78%) -- the machine held still
```

| | ms |
| --- | --- |
| ADR-390 §6's estimate | **≈ 0.5** |
| ADR-390 §6's design target | **≤ 0.8** |
| ADR-390's stated headroom at 60 fps | 2.4 |
| **measured** | **+5.05** |

It is **ten times the estimate, six times the target, and twice the entire budget.** Attribution, by
re-running the A/B with one component's density zeroed (2 pairs, 180 frames each):

| arm | delta | implied component |
| --- | --- | --- |
| full ocean | +5.05 | — |
| nebulae off | +2.95 | **nebulae ≈ 2.10** |
| dust off | +4.52 | dust ≈ 0.53 |
| stars off | +4.52 | stars ≈ 0.53 |
| planets + galaxies off | +5.24 | ≈ 0 |

Only the nebula figure is clear of the noise floor (±2% of ~25 ms is ±0.5 ms), so the honest reading
is: **the nebulae are ~2.1 ms and everything else is within the floor of each other**, with roughly
1.9 ms unattributed — the deep-space gradient, the palette, the atmospheric recede, the events and
the draw's own fixed cost, none of which any arm above removes.

**Why the estimate was wrong, and it is not arithmetic.** ADR-390 §6 calibrates from M3's
`glowmere-cosmos`: "~0.02 ms per full-1080p-frame per five-octave fBM". That shader's fBM is **2D
value noise over a `sin`-hash**. `coFbm` is **3D value noise** — eight corner hashes and a trilinear
blend per octave — and the nebulae **domain-warp**, which costs a second full fBM evaluation to
produce the warp vector before the first one is sampled. A per-fBM constant measured on the cheap
kind, applied to the expensive kind, is off by about the ratio of their hash counts. That is the
trap ADR-389 states in general form and this is a new instance of it: *a coefficient tuned against a
quantity is invalidated by any change to that quantity's distribution.* Here the quantity is "an
fBM" and the distribution is which fBM.

**Consequence for staging.** ADR-390's order is add, tune, A/B, and only then remove the four
`cosmos-*` nodes. The effect is added and A/B'd; **it is not tuned and it is not in the shipped
project.** The Tree of Life is untouched and the four `cosmos-*` nodes are untouched, which also
leaves `test_cosmic_key.cpp:144,160`'s hard-coded scene radius of 2737 valid. The arm that carries
the effect is `examples/treeisland/_oc-ocean.json`.

The tuning that budget implies is a real design question and not a slider pass: at ~2.1 ms the
nebulae alone are over ADR-390's whole target, so either the nebula term gets cheaper (fewer
octaves, a cheaper warp, or the low-resolution nebula buffer ADR-390 §5 explicitly parks as the
documented fallback) or the target moves. ADR-390 already says which: *"If the built shader misses
the budget, the target is the fallback and not the starting point."* It missed.

## 6. What else the run found

- **`avgen_render_tests` did not link.** `tests/CMakeLists.txt` never carried
  `src/app/frame_range.cpp`, which `render_settings.cpp` calls into. A suite that cannot start
  cannot report that it did not start, which is why this survived. Added.
- **`CosmicOcean::validate` refused a state the parameter system can reach.** `maskOuterAngle` and
  `maskInnerAngle` are independent registered parameters with their own 0..180 hard ranges, and
  `validate` runs inside `fromJson` — so any route, key or slider that inverted them produced a
  project that would not load again. Found by `effect_conformance`'s round-trip check on its first
  new kind, which is the thing it was built for. The ordering is resolved in `sanitiseCosmicOcean`
  and in `packCosmicOcean`; `validate` checks finiteness only. **A soft constraint between two knobs
  belongs where the value is used, not at the load gate.**
- **The ocean's JSON is written through its field tables**, so key == leaf and ADR-390 §2.1's "two
  lists maintained by hand" is one list for this kind. The comet's and the aurora's cannot be —
  their file format predates their tables and `speedScale`/`speed` are two namespaces rather than
  two spellings — but a new kind does not have to inherit the hazard.
- **A printf bug in the Render panel.** `control_panel.cpp`'s supersample tooltip is
  `IM_FMTARGS(1)` and contains two literal per-cent signs, so `"2.80% at"` was a `% a` conversion
  against an empty varargs list: undefined behaviour, and a tooltip printing a garbage hex float
  instead of the measurement. Found by the warning census below.

## 7. `AVGEN_WARNINGS_AS_ERRORS` should stay OFF, and what a census of it says

Asked because a fourth enumerator's safety net was assumed to be `-Werror` and is not. Measured by
touching every source and rebuilding `avgen` and `avgen_tests`:

| category | count |
| --- | --- |
| `-Wmissing-field-initializers` | **1465** |
| `-Wdouble-promotion` | 34 |
| `-Wunused-private-field` | 21 |
| `-Wshadow` | 16 |
| `-Wunused-result` | 3 |
| `-Wunused-function` | 3 |
| `-Wunused-const-variable` | 2 |
| **`-Wswitch`** | **2** |
| `-Wreorder-ctor` | 2 |
| **`-Wformat-insufficient-args`** | **1** |
| **total** | **1549** |

**1462 of the 1549 are in one file**, `src/ui/ui_logic.hpp`, and one category. Turning the flag on
today breaks the build in a file `agent/eyecam` is editing, which is the worst possible place for a
mass edit.

But the census is worth more than the verdict. The two `-Wswitch` warnings are
`scene/composition.cpp:3139` (`NodeKind::City` unhandled) and `ui/sequence_panel.cpp:1391`
(`Marquee`) — the exact class of defect this ADR opens with, sitting unread in the log. The single
`-Wformat-insufficient-args` was a live bug. **The recommendation is therefore not "turn it on" but
"turn these two on now and the rest when `ui_logic.hpp` is free":**

- add `-Werror=switch` and `-Werror=format` immediately: between them they cost three fixes, and
  both name defects rather than style;
- fix the three `-Wunused-result` (`entity/behaviors.cpp:1799`, `entity/decision.cpp:648`,
  `world_effects/effect_conformance.cpp:102`) — a discarded `[[nodiscard]]` is a `Result<void>`
  nobody checked;
- leave `AVGEN_WARNINGS_AS_ERRORS` itself OFF until `ui_logic.hpp`'s 1462 are dealt with as their
  own change, by whoever owns that file, in one pass.

A blanket `-Werror` that has to be fought is a flag somebody turns off again. Two targeted ones that
cost three fixes are a net that stays up.
