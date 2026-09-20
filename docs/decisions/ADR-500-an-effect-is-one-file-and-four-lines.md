# ADR-500: An effect is one file and four lines

- Status: Accepted (2026-09-20)
- Supersedes the mechanism of ADR-387 (the two `constexpr` field tables) and implements the survey
  in ADR-392 (the family generalises in three directions and is hand-written in five).
- Extends ADR-230 (atmospheric effects), ADR-207 (world effects), ADR-350 (a setting the
  application does not keep is not a setting), ADR-382 (a path a panel computes needs a test that
  computes it the same way), ADR-182 (a probe that cannot fail proves nothing), ADR-375 (a control
  that draws and does nothing), ADR-374/379/388/389 (the vortex's units and measured ranges),
  ADR-360 (the determinism contract), ADR-421 (a control that does nothing teaches an artist that
  the system is broken).

## The problem, measured

The owner has commissioned roughly **seventy** world effects across eight phases. There are three:
`Comet`, `Aurora`, `Vortex`, with a fourth on an unmerged branch.

`grep -rc 'AtmosphereKind::'` across `src/` returned **59 references for 3 kinds**, concentrated in
four files every effect author has to open at once:

```
src/world/atmospherics.cpp                        16
src/world/atmospheric_params.cpp                  12
src/world/world_effects/effect_conformance.cpp     6
src/ui/world_effects_panel.cpp                     4
```

ADR-392 counted the per-field cost rather than the per-kind one: **one aurora field appears at nine
sites that must agree by hand**, two of them bare string literals -- the default route's target and
the panel's row -- which neither fail to compile nor throw. At ~20 edits per effect, seventy effects
is ~1,400 hand edits, all of them in files that every other agent building an effect also has open.

That, not the rendering, was the blocker. Parallel effect work was impossible.

## Decision

**An effect declares itself in one file, and every shared file iterates a registry.**

`src/world/world_effects/effect_registry.hpp` defines an `EffectSchema`: a kind's serialisation key,
its display name and Add-button tooltip, its rows, its style presets, its default audio routes, its
beat target, its anchor accessors, its factory, and how it resolves per frame. Each kind's schema
lives in `src/world/world_effects/effects/<kind>_effect.cpp` and nowhere else.

**One row is nine things at once.** `EffectField` carries the leaf, the artist-facing label, the
hard and soft ranges, the sanitise clamps, the format, the tooltip, the panel section and page, and
the accessor pair -- and from that one declaration the engine derives the registered parameter, the
modulation target, the timeline key, the preset member, the save entry, apply, capture, the panel
slider and the JSON key. The nine sites ADR-392 counted are one line.

### What each shared file became

| file | before | after |
|---|---|---|
| `atmospheric_params.cpp` | 574 lines: four field tables, three `switch`es, `sanitise`'s fifteen clamps, `defaultAtmosphericRoutes`'s per-kind arms | 236 lines: register, apply, capture, and a loop that turns declared routes into `ModRoute`s |
| `atmospherics.cpp` `toJson`/`fromJson` | ~300 lines of key/member pairs in two directions | two loops over the same rows |
| `atmospherics.cpp` presets | ~380 lines of style bodies and factories | a kind-agnostic lookup; the bodies moved beside the rows they set |
| `atmospherics.cpp` resolve | a `switch` with an arm per kind | a `switch` over `EffectBucket`, which is per *GPU payload*, not per kind |
| `ui_logic.hpp` | nine row tables, 193 lines | deleted; the panel walks `EffectSchema::fields` |
| `world_effects_panel.cpp` | per-kind `isComet`/`isVortex` branching, three Add buttons, a per-kind beat leaf | one loop over the registry |

`AtmosphereKind::` in `src/` is now **49 references for 5 kinds**, and 25 of them are inside an
effect's own file or in the one list the registry keeps.

### The remaining cost, counted honestly: four lines in two files

1. an enumerator in `AtmosphereKind` (`world/atmospherics.hpp`);
2. an entry in `kAtmosphereKinds` (`effect_registry.hpp`);
3. a declaration of the schema accessor in `effect_registry.cpp`, and
4. the reference to it in `builtinSchemas()` on the next line.

It was six, in four files, when this ADR was first written -- and the count was wrong in the draft,
which is worth recording because the draft said three. Measuring it turned up two lines that were
buying nothing: a `builtin_effects.hpp` that existed only to declare five functions nothing outside
`builtinSchemas()` calls, and an exhaustive `switch` in `atmosphereKindIndex` sitting directly below
the array it indexes. The switch was the weaker of ADR-392's two guards -- a `-Wswitch` diagnostic,
which ADR-392 itself says is not a guard while `-Werror` is off -- so it became a search over the
array, and the guard that actually fires is unchanged.

**The list in (2) could be derived from the schemas, collapsing two more, and deliberately is not.**
If the enumerators came from the schemas, then "every enumerator has a schema" would be a check
asking the schemas about the schemas -- the self-agreeing shape ADR-392 spent its length warning
about. `kAtmosphereKinds` is a second, independent list, and the test holds it against the enum read
out of the header. One line per effect buys a guard that can fail.

`builtinSchemas()` is an explicit list rather than static self-registration through a global
constructor, and that is not style. These compile into a static library; a translation unit nothing
references is dropped by the linker. A registry that is complete in a debug build and short in a
release one is the worst failure this repository knows how to have.

### A forgotten piece fails loudly

The requirement was that an incomplete effect be a compile error or a test failure **by name**.
Both exist, and the split between them is deliberate.

**Compile time.** The row factories -- `floatField`, `colorField`, `boolField` -- are `consteval`
and take the accessors as *required positional arguments*. There is no way to spell a Float row that
reads nothing. That closes the failure at the point where it would otherwise be a row that registers
a parameter and moves no member.

**Test time, by name.** `checkRegistry()` walks every schema and reports: an enumerator with no
schema; two schemas claiming one enumerator; a duplicate or non-round-tripping serialisation key; a
row with no accessors, no label or the accessors of the wrong type; a duplicate leaf, or one that
collides with a shared row; a float default outside its hard range or a soft range escaping it; a
default route or a `beatLeaf` naming a leaf the kind does not declare; a kind with no factory, no
resolve or no styles. Every finding names the kind and the row.

**And ADR-392's headline defect is now unspellable.** Its worst failure was a panel row naming a
parameter nobody registered: it drew an empty box, indistinguishable from "this scene has no such
effect", and the owner reported that class of defect twice. The panel row *is* the registration now
-- one row, one parameter, one slider, one JSON key -- so the two cannot disagree. There is no
second list to be five characters wrong in.

### `effect_conformance` still works, and got easier

All six checks survive, and every one of them now runs over **five** kinds instead of three without
a line being added to `checkAtmosphericFamily`. Two properties were load-bearing and are preserved:

- **The path set is still read out of the registrar, not out of the tables.** `registeredPaths`
  registers a probe into a scratch `ParameterSet` -- the same call `Engine` makes -- and returns
  what the registrar wrote. A conformance layer reading the schema would agree with the schema.
- **The round trip is still read back through re-registration**, not compared as two documents.
  `toJson(fromJson(toJson(e))) == toJson(e)` passes when a field is missing from *both* directions,
  and that is exactly the defect this family keeps producing.

Three checks changed shape, and each change was bought by a failure rather than chosen:

1. **The resolve-dispatch check** used a `switch` per kind to pick which counter to compare. It now
   asks the schema which bucket the kind *says* it goes in and checks that it went there -- which is
   what keeps it able to fail for a kind that does not exist yet. It also learned that "exactly one
   record" is wrong: a meteor shower declares more, and the old assertion would have failed on
   correct code.
2. **The panel-row check was turned round, because the old one could no longer fail.** "Is every row
   the panel draws a parameter that kind has" was the right question while the rows were a second
   list. Now it is vacuous. The question that can still fail is the inverse, and it is also the
   owner's standing rule: **is every registered parameter a row somebody can reach?** A row marked
   `Hidden`, a row with no label, or a shared row the panel forgets now fails by path.
3. **The enum reader stopped matching on a coincidence.** ADR-392 lowercased the enumerator and
   compared it with `atmosphereKindName`. That worked for three kinds because "Comet" lowercases to
   "comet" *by accident*: an enumerator's spelling and a kind's file format are two different
   decisions. The first kind for which they differed -- `MeteorShower`, which serialises as
   `"meteors"` -- failed the test while being entirely correct. A guard that fires on working code is
   not a guard. `EffectSchema::enumName` declares the mapping, the sets are compared by it, and the
   check is now stronger in both directions: an enumerator with no schema *and* a schema claiming an
   enumerator that no longer exists are both named.

## No behaviour change, proved

Three shipped projects, one frame each at t=6.00, 960x540, rendered under `tools/gpu-lock.sh` on a
binary built from `493091b5` and on the same command after the port:

| arm | before | after |
|---|---|---|
| `examples/treeisland/tree-of-life-floating-island.json` (comet + vortex) | `aa07b653007073b2` | `aa07b653007073b2` |
| `examples/world/glowmere-stylized.json` | `fea3b72479938548` | `fea3b72479938548` |
| `examples/world/glowmere-valley-2.json` | `adc55bcf9a017e97` | `adc55bcf9a017e97` |

`tools/image_diff.py` on each pair: **0 of 518400 pixels differ, max channel delta 0.**

**Two of those three arms could not have failed, and saying so is the point.** Neither Glowmere
project authors an atmospheric effect at all -- `atmosphericEffects` is absent from both the project
and the scene. Their identical hashes are regression coverage for the rest of the engine (the
`AtmosphericEffect` struct grew a member, `validate` grew a call) and are *not* evidence about the
family. ADR-182's rule, applied to my own evidence before quoting it.

So two further arms were run, and these are the ones that carry the claim:

- **An aurora arm**, because no shipped scene has one. The Tree of Life project with a hand-authored
  aurora appended: `b99cf2a8565b1dbc` on both binaries, 0 of 518400 pixels. Its hash differs from
  the unmodified project's, which is what shows the aurora is actually in the picture rather than
  being dropped by both builds alike.
- **A control that moves.** The same Tree of Life frame with `--disable volume`, which removes the
  march the vortex is drawn in: `af9b699e83ddaf2f`, on both binaries, against `aa07b653007073b2`
  without it. The comparison is demonstrably sensitive to the atmospheric medium, so the byte
  identity above is a result rather than a tautology.

### Why byte identity was achievable at all

The port is a **move**, not a rewrite. Every accessor reads the member the old field table read;
every range, default and clamp is the number it was; every style body and factory body was carried
across unchanged; and the resolve arms are the same expressions in the same order.

Two things were nearly got wrong and are worth recording, because both would have been invisible:

- **The arc was almost transliterated twice.** A first draft of `comet_effect.cpp` wrote its own
  `directionFromSky` and `reparameterise`. Both were algebraically right and neither was
  bit-identical: the elevation clamp to ±89° was missing, and `p * (1 + k*p) / (1 + k)` is not the
  same float as `(p + a*p*p) / (1 + a)`. ADR-388 measured 62 pixels of movement from arranging one
  call site differently; two copies of a curve inside one language would be that with none of the
  excuse. The three helpers are now exported from `atmospherics.hpp` and there is one curve.
- **Timing is `double` on the effect and `float` in a parameter** (ADR-011). Routing the timing rows
  through the derived serialiser would have quietly rounded every authored delay. They register --
  so they are automatable and the conformance round trip still covers them -- and their file
  representation stays `timingToJson`'s `double`.

## The two new effects, and the honest boundary

Two were added as proof, each one file plus its three lines.

**Meteor shower** (`meteors`). N streaks thrown from a radiant, staggered, with per-meteor spread,
size variation and drift. It reaches the GPU with **no shader change at all**: a meteor *is* a comet
-- a head on a great-circle arc with a trail integrated along the view ray -- so the kind writes N
records into the comet bucket. `EffectResolve::count` exists for this and this is its first user.
Its streak aliases `e.comet`, so the packer is untouched and the file format gains no second copy of
thirteen colours; its own six numbers live in `AtmosphericEffect::values` under `meteors/`. Every
per-meteor number is a hash of `(seed, index)` and never a stream, because ADR-360's contract is
that two renders of a range may not differ and a stream would make meteor 4 depend on how many times
meteor 3 was asked.

**Volumetric fog** (`fog`). A placed bank of medium with a soft rim, a Gaussian vertical profile and
billowing interior. `shaders/volume.wgsl` already marches exactly this: `vortexShapeAt` with
`innerVoid` at 0 is a filled disc, `rim` fades smoothly past the radius, `wall` is a Gaussian in Y,
and with `swirl` and `funnelDepth` at 0 there is no spiral and no throat. So a fog bank **is** the
placed volumetric medium the engine has, marched with different numbers.

That makes it a different **authoring surface** onto one primitive rather than a second primitive,
and the distinction is what the registry is for. What an artist gets that "a vortex with the swirl
turned down" would not give them is everything a first-class effect is: its own Add button, its own
vocabulary ("Fog density", "Bank radius", "Bank height", "Billow", "Churn"), its own presets, its
own default audio routes, its own conformance entry -- and a panel with no Throat, Funnel depth or
Swirl on it to be confused by. Nine of a vortex's controls do nothing to a fog bank, and a control
that does nothing teaches an artist that the system is broken (ADR-421).

**The limit is stated rather than discovered.** The march has one medium slot, because ADR-374
measured the single vortex at +5.5 ms of a 13.5 ms frame. A fog bank and a cosmic vortex compete for
it, and the second is counted in `AtmosphericCounts::dropped` and reported.

**And this is the honest boundary of what a registry can buy.** Both proof effects reuse an
integrator the engine already has, which is deliberately the easy half. A kind that needs a *new*
integrator needs WGSL, and a `.wgsl` file is not a `.cpp` file. What ADR-500 removes is everything
either side of the pixels -- which is all of the twenty edits, and all of the contention.

## Consequences

**Parallel effect work is now possible, and that was the point.** Two agents adding two effects
touch two new files and collide on three one-line entries, instead of both editing
`atmospherics.cpp`, `atmospheric_params.cpp`, `ui_logic.hpp`, `world_effects_panel.cpp` and
`effect_conformance.cpp`.

**The suite.** 2677 cases before, all green but the one `[!shouldfail]` by design and four skipped;
the count moved by the cases added here. `tests/unit/test_effect_registry.cpp` is new. One failure
was produced deliberately along the way and is recorded above: the ADR-392 enum reader firing on
correct code, which is what led to `enumName`.

**What this does not do.** It does not touch the ADR-207 family (`worldfx/<name>/*`), whose
`effect_params.cpp` is still three parallel lists -- register, apply, capture -- which is the exact
failure the atmospheric tables were introduced to prevent. That family is the next thing shaped to
take this treatment, and `EffectField` is the shape it would take. It does not add a shader, raise
`kMaxGpuComets`, or change a rendered pixel of any shipped scene.

## Revisit triggers

- A kind that needs a GPU payload none of the three buckets provides. `EffectBucket` grows an arm,
  and the exhaustive `switch` in `resolveAtmosphericEffects` is the diagnostic that says where.
- A second volumetric medium slot. It costs the march what ADR-374 measured; the decision needs a
  measurement, not a preference.
- `AtmosphericEffect::values` growing past a few dozen rows per kind. The lookups are linear over a
  sorted vector, which is right for tens and wrong for thousands.
- The ADR-207 family taking the same treatment, at which point `EffectSchema` should move out of
  `world_effects/` and stop being spelled in atmospheric terms.
