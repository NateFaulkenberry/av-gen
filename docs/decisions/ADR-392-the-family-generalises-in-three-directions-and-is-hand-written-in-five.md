# ADR-392: The family generalises in three directions and is hand-written in five

- Status: Accepted (2026-09-20)
- Extends ADR-230 (atmospheric effects), ADR-387 (the vortex is an effect), ADR-390 (the Cosmic
  Ocean is an atmospheric effect), ADR-207 (world effects), ADR-350 (a setting the application does
  not keep is not a setting), ADR-382 (a path a panel computes needs a test that computes it the
  same way), ADR-182 (a probe that cannot fail proves nothing), ADR-385 (a stated reason is not
  evidence), ADR-362 (do not print the failure you found).

## The question

The brief asks for a reusable environmental simulation layer and a World Effect registry with
universal modulation, and warns against reinventing what exists. ADR-387 answered that ADR-230's
atmospherics family already **is** that registry: two `constexpr` field tables and one case in each
of `floatFields` / `colorFields` / `boolFields` bought the vortex, with no bespoke code, a
registered and modulatable parameter per field, a Parameters-panel row, a modulation target, a
timeline key, a preset member, a save entry, apply and capture in both directions, default audio
routes, a place in the World Effects panel and serialisation as a scene-authored instance.

The question this ADR answers is whether that claim survives being held to. It mostly does, and the
part that does not is worth more than the part that does.

## What the tables actually buy, measured

The table collapses **three** directions into one row. `registerAtmosphericParameters`,
`applyAtmosphericParameters` and `captureAtmosphericParameters` are each one loop over
`floatFields(kind)` + `kSharedFloats`, and `copyParameters` serves apply and capture from the same
walk so the two cannot drift. That is real and it is the right design.

It buys **nothing else**. Beside the table, every one of these is a separate per-kind or per-field
list written by hand:

| list | where | what a forgotten entry does |
|---|---|---|
| `toJson` | `atmospherics.cpp:344` | the field does not save |
| `fromJson` | `atmospherics.cpp:455` | the field does not load |
| `sanitise` | `atmospheric_params.cpp` | a modulated divisor reaches zero |
| the five style presets | `atmospherics.cpp:763-990` | a style leaves the field where it was |
| `defaultAtmosphericRoutes` | `atmospheric_params.cpp:461` | a route aims at nothing |
| the panel's rows | `world_effects_panel.cpp`, `ui_logic.hpp` | the section draws an empty box |

Taking one aurora field as the census: `edgeBrightness` appears at **nine** sites that must agree by
hand — the struct and its default, `validate`, `toJson`, `fromJson`, the GPU pack, the field table,
the default route, the panel row, and five style presets. The table collapsed three of those into
one. Six remain, and **two of them are bare string literals** — the default route and the panel row
— which neither fail to compile nor throw. Those two are precisely ADR-387's "a parameter path is
three things at once," and they are where every defect found in this family lives.

The ADR-207 family (`worldfx/<name>/*`) is a generation behind: `effect_params.cpp` is three
parallel lists — register, apply, capture — which is the failure the tables were introduced to
prevent, and its own header comment says so.

## The three defects, and what they have in common

Two were also found independently by the Cosmic Ocean work (ADR-390 §2), which is worth recording:
the same three things were found twice, from two directions, without either investigation seeing
the other. That is evidence about the defects rather than about either investigation.

1. **`defaultAtmosphericRoutes` was `if aurora else comet`.** A vortex fell into the comet arm and
   was handed three routes aimed at `coreIntensity`, `tailIntensity` and `sparkleIntensity`, none of
   which a vortex registers. Nothing fails: a route naming an unregistered path binds to nothing,
   warns once at load, and is thereafter indistinguishable from an effect nobody automated.
2. **The function has no caller in `src/`.** The whole of the family's premise is that audio reaches
   an effect as a modulation route, and `engine.cpp:3439` states in a comment that
   `defaultAtmosphericRoutes` "is what implements it." It implements nothing: the World Effects
   panel's `append` adds an effect and attaches no routes, so "Add aurora" produces a silent aurora
   and six routes somebody must hand-author. ADR-385's stated reason that is not evidence, again,
   and in a comment rather than in code.
3. **`resolveAtmosphericEffects` dispatches `if comet / else if aurora / else`**, where the `else`
   is the vortex. A fourth kind forgotten there does not resolve as nothing — it resolves as a
   **vortex**, and is then dropped past the first one. That is a worse failure than a no-op and a
   quieter one, and it is live on the path the Cosmic Ocean is about to take.

What they have in common is not carelessness. It is that **the family is open under kind-addition
and nothing closes it.** `-Werror` is behind `AVGEN_WARNINGS_AS_ERRORS` and off in this build, so
even the exhaustive switches degrade to a warning in a five-thousand-line log. The one construction
in the family that is right — `ui::atmosphericBeatTarget`, an exhaustive switch with no `default`
that handles all three kinds — is right by the author's care, not by anything that would have caught
its being wrong.

## Decision

**The family is the environmental simulation layer, and it does generalise. What it lacked was a
statement of its own contract that a machine could check.** So: no `WorldEffect` base class, no
parallel registry, no new serialisation. Instead `src/world/world_effects/effect_conformance.*`
states the contract as five checks run per kind, and `tests/unit/test_effect_conformance.cpp` runs
them over every kind.

This was chosen over a base class deliberately. A base class would make the *next* kind safe by
making it inherit; it would not make the three existing kinds safe, it would not touch the five
hand-written lists, and it would be a second registry beside the one ADR-387 established — the
thing §11 and ADR-387 both refuse. A contract check makes the kinds that already exist safe today
and the ones that do not exist yet safe on the day they are added, and it costs no runtime.

### The five checks

1. **Default routes name paths the kind registers**, and are modulatable, and are inside the
   effect's own prefix.
2. **ADR-350's round trip, read back through the registrar.** Every registered component is set to
   a distinct non-default value, captured onto the authored effect, saved, loaded, and then the
   *reloaded effect is re-registered* and every parameter's default compared against what was set.
3. **Defaults lie inside hard ranges and soft ranges inside hard ranges.**
4. **An effect resolves as its own kind**, with the three kinds' resolve signatures required to be
   pairwise distinct.
5. **The kind's name survives** `atmosphereKindName` → `atmosphereKindFromName`, which is an
   if-chain rather than a switch.

### Two properties that make the difference between this and a test that agrees with itself

**The path set is read out of the registrar, not out of the tables.** `registeredPaths` registers a
probe effect into a scratch `ParameterSet` — the same call `Engine` makes — and returns the
registrar's own list of what it wrote. ADR-382's rule is that a path a panel computes needs a test
that computes it the same way; the generalisation is that a path *anything* computes needs a check
that obtains it the way the engine does. A conformance layer that read the field tables would agree
with the field tables and learn nothing.

**The round trip is not a comparison of two documents, and this is the finding worth keeping.** The
obvious check is `toJson(fromJson(toJson(e))) == toJson(e)`. It is insufficient, and in a way that
is invisible until you try to make it fail: **that identity holds when a field is missing from both
directions.** The key is absent from the first document, `fromJson` leaves the struct's default in
place, the second document is absent it too, and the two agree perfectly while the field has been
silently deleted from the file format. Reading the values back through registration instead — whose
defaults are taken from the effect's own fields — asks the only question worth asking: did this
number survive? Deleting `"filaments"` from the vortex's writer *and* its reader together is caught
by name; the document comparison passes.

That is the same family as this repository's recurring defect — a reader with no writer, a writer
with no reader, a setting the application does not keep — and it is the first check here that could
have found any of them.

### What was changed beside the new files

`defaultAtmosphericRoutes` becomes an exhaustive switch with a vortex arm. The defaults are the five
routes ADR-387 records the shipped Tree of Life project authoring on its funnel — bass to density
and breath, mid to turbulence, treble to filaments — because a default taken from the one scene that
has tuned a vortex is evidence and one invented for the function is not. The single departure: the
shipped project drives `emission` from `time.progress`, which ramps brightness across the song; that
is a composition decision rather than a property of vortices, so the brightness route here is
`beat.pulse`, on the same leaf `ui::atmosphericBeatTarget` picks for this kind — so the Beat response
slider finds the route this function wrote instead of writing a second one beside it.

### The enum is closed by reading the header, and why that is not lazy

`conformance::atmosphereKindIndex` is an exhaustive switch with no `default`, so a new enumerator is
a `-Wswitch` diagnostic. That is not enough, because `-Werror` is off. The enum has no reflection
and no sentinel, and adding a `Count` sentinel would be a kind that every switch in the engine then
has to handle and that every serialiser has to refuse.

So the test reads `enum class AtmosphereKind` out of `atmospherics.hpp` and requires the conformance
table to cover exactly the enumerators declared there. `test_lab_registry` already establishes that
a test in this repository may read the source tree to check a claim the source tree makes about
itself. The failure mode is the one that matters: adding `CosmicOcean` to the enum fails a named
test that prints `CosmicOcean`, rather than emitting a warning nobody reads.

## Consequences

**Every probe is shown capable of failing (ADR-182), and each was made to fail before it was
trusted.** The enum reader is shown finding the three kinds that are there and not finding one that
is not. The leaf check is shown reporting a leaf that does not exist and passing the shared ones
that do. The default-route check, run against the unfixed function, prints all three of the vortex's
dead targets by name — with `with expansion:` present, so it is a real failure and not ADR-362's
killed run. The round-trip check, run against a `filaments` deleted from both directions, names the
path. The resolve-dispatch check requires the three kinds' signatures to be pairwise distinct, which
is the assertion that would catch a fourth kind falling into the vortex arm; the per-kind form of
that check passes trivially if every kind resolves as the same thing, which is why the distinctness
form exists beside it.

**The suite is green and the count moved by the cases added here.** 2546 cases before, one
`[!shouldfail]` by design, four skipped.

**The Cosmic Ocean is the first beneficiary and was not waited for.** ADR-390 proposed fixing the
vortex's routes as part of adding its own kind, and proposed that the test loop over every
enumerator. Both are done here instead, ahead of that kind existing, so the guard is in place before
the thing it guards arrives rather than landing with it. When `AtmosphereKind::CosmicOcean` is
added, the enum test fails by name, and then the five checks fail one at a time until the kind is
wired — routes, serialisation, ranges, dispatch — which is the order somebody would want to be told.

**The comet's and the aurora's panel rows became data, and that is what took the row check from one
kind to three.** ADR-387 called that conversion mechanical and deferred it so it would not bury the
point it was making. It was mechanical, and deferring it had a cost that was invisible until there
was a mechanism to measure it: `checkLeavesExist` could be pointed at one of the family's three
kinds and not at the other two, because the other two's leaves were string literals inside an ImGui
call. Four things stay inline on purpose -- the two anchor combos, which write a `SkyAnchor` enum
rather than a parameter, and the sparkle and rainbow checkboxes, which are `enabled`-shaped bools
rather than value rows. Putting any of them in a table would put a leaf there that registration
does not produce, which is the opposite of the point.

**`defaultAtmosphericRoutes` got a caller.** Being correct for every kind is not enough when nothing
calls it, and a function that is correct and unreached is the exact defect this repository keeps
finding. `Engine::addDefaultAtmosphericRoutes` is the same shape as `addDefaultPostRoutes` --
guarded on nothing already targeting the effect's prefix, so pressing the button twice does not
stack two sets -- and the World Effects panel's "Add" buttons call it. It is deliberately **not**
inside `setAtmosphericEffects`: that call also runs when a project is opened, and a project whose
author deleted every route must not grow them back each time it loads. Adding an effect is a
gesture; loading one is not.

A route whose target does not resolve is skipped and logged rather than written, and that guard was
exercised rather than assumed: with the pre-ADR-392 defect put back, the vortex's three
comet-shaped targets were refused by name and nothing dead reached the project. A dead route in a
saved project is what ADR-387 spent a day on, and a button must not be able to introduce one.

**What this does not do.** It does not touch the ADR-207 family's three parallel lists --
`effect_params.cpp` is still register, apply and capture written out separately, which is the
failure the atmospheric tables were introduced to prevent, and `checkLeavesExist` is shaped to take
that family next. It does not add a kind, touch a shader, or change a rendered pixel. No shipped
scene or project gains a route, and the reason is simpler than the prefix guard: nothing on the
load path calls `addDefaultAtmosphericRoutes` at all. Only the three "Add" buttons do. The guard is
there for the person who presses one twice, not for the files.
